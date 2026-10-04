#pragma once
// mulenet_engine.hpp - the hub directory fold, the route planner, and what a hub does
// when a parcel arrives. Qt-free; C++ mirror of packages/core/src/{registry,planner,
// hub}.mjs (read those for the reasoning). docs/SPEC.md sections 4, 8, 9.
#include "mulenet_label.hpp"
#include "mulenet_seal.hpp"
#include "mulenet_physical.hpp"
#include "logos_sync/event.hpp"
#include "logos_sync/merge.hpp"
#include "logos_sync/signing.hpp"
#include <set>
#include <map>
#include <algorithm>

namespace mulenet {

inline const std::string DOMAIN = "mulenet";
inline const std::string REGISTRY_TOPIC = "/mulenet/1/registry/proto";
using logos_sync::Event;

inline bool knownEventType(const std::string& t) {
    for (const char* x : {"hub.announce", "hub.retire", "hub.grant", "hub.revoke", "steward.add", "vouch", "unvouch", "mailbox.create"})
        if (t == x) return true;
    return false;
}

// ---- the folded directory ------------------------------------------------------------
struct LabelKey { int epoch = 0; std::string pub; long long notAfter = 0; };
struct HubCard {
    std::string address, pub, name, city, country, boxPub;
    std::vector<LabelKey> labelKeys;
    json policy = json::object(), intake = json::object();
    std::vector<std::string> vouchedBy;
    long long updated = 0;
};
struct Grant { std::string ref, from, to, sealed; };
struct Mailbox { std::string ref, exit, sealed; };
struct Directory {
    std::map<std::string, HubCard> hubs;
    std::map<std::string, Grant> grants;
    std::map<std::string, Mailbox> mailboxes;
    std::set<std::string> stewards;
};

inline HubCard cardFrom(const Event& e) {
    const json& p = e.payload;
    HubCard h;
    h.address = e.dev; h.pub = e.pub; h.updated = e.hlc.wall;
    h.name = p.value("name", ""); h.city = p.value("city", ""); h.country = p.value("country", "");
    h.boxPub = p.value("boxPub", "");
    if (p.contains("labelKeys") && p["labelKeys"].is_array())
        for (const auto& k : p["labelKeys"]) {
            if (!k.is_object() || !k.contains("pub") || !k["pub"].is_string() || k["pub"].get<std::string>().size() != 64) continue;
            h.labelKeys.push_back({k.value("epoch", 0), k["pub"].get<std::string>(), k.value("notAfter", 0LL)});
        }
    if (p.contains("policy") && p["policy"].is_object()) h.policy = p["policy"];
    if (p.contains("intake") && p["intake"].is_object()) h.intake = p["intake"];
    else h.intake = json{{"kind", "none"}};
    return h;
}

inline Directory foldRegistry(const std::vector<Event>& log, const std::set<std::string>& roots) {
    std::vector<Event> events;
    for (const auto& e : logos_sync::mergeEvents(log, {}))
        if (logos_sync::verifyEvent(DOMAIN, e)) events.push_back(e);
    Directory d;
    d.stewards = roots;
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& e : events) {
            if (e.type != "steward.add" || !d.stewards.count(e.dev)) continue;
            std::string s = e.payload.value("steward", "");
            if (!s.empty() && !d.stewards.count(s)) { d.stewards.insert(s); grew = true; }
        }
    }
    std::set<std::string> retired, revoked;
    std::map<std::string, bool> vouches;   // steward|hub -> latest
    for (const auto& e : events) {
        const json& p = e.payload;
        if (e.type == "hub.announce") d.hubs[e.dev] = cardFrom(e);
        else if (e.type == "hub.retire") retired.insert(e.dev);
        else if (e.type == "hub.grant") {
            std::string ref = p.value("ref", "");
            if (!ref.empty() && !d.grants.count(ref)) d.grants[ref] = {ref, p.value("to", ""), e.dev, p.value("sealed", "")};
        } else if (e.type == "hub.revoke") revoked.insert(e.dev + "|" + p.value("ref", ""));
        else if (e.type == "vouch" || e.type == "unvouch") {
            if (d.stewards.count(e.dev)) vouches[e.dev + "|" + p.value("hub", "")] = e.type == "vouch";
        } else if (e.type == "mailbox.create") {
            std::string ref = p.value("ref", "");
            if (!ref.empty() && !d.mailboxes.count(ref)) d.mailboxes[ref] = {ref, p.value("exit", ""), p.value("sealed", "")};
        }
    }
    // Tombstones last, so they commute with a late-arriving original.
    for (const auto& a : retired) d.hubs.erase(a);
    for (auto it = d.grants.begin(); it != d.grants.end();)
        it = (revoked.count(it->second.to + "|" + it->first) || !d.hubs.count(it->second.to)) ? d.grants.erase(it) : std::next(it);
    for (auto it = d.mailboxes.begin(); it != d.mailboxes.end();)
        it = !d.hubs.count(it->second.exit) ? d.mailboxes.erase(it) : std::next(it);
    for (auto& [addr, h] : d.hubs)
        for (const auto& [k, on] : vouches) {
            size_t bar = k.find('|');
            if (on && k.substr(bar + 1) == addr) h.vouchedBy.push_back(k.substr(0, bar));
        }
    return d;
}

inline const Grant* grantFor(const Directory& d, const std::string& from, const std::string& to) {
    for (const auto& [ref, g] : d.grants) if (g.from == from && g.to == to) return &g;
    return nullptr;
}
inline Bytes liveLabelKey(const HubCard& h, long long atMs) {
    const LabelKey* best = nullptr;
    for (const auto& k : h.labelKeys)
        if ((!k.notAfter || k.notAfter > atMs) && (!best || k.epoch > best->epoch)) best = &k;
    return best ? unhex(best->pub) : Bytes();
}

// ---- authoring registry events ---------------------------------------------------------
struct Identity {
    Bytes priv;
    std::string address;
    logos_sync::Clock clock{""};
};
inline Identity identityFrom(const Bytes& priv) {
    Identity id;
    id.priv = priv;
    id.address = logos_sync::address(logos_sync::pubFromPriv(priv));
    id.clock = logos_sync::Clock(id.address);
    return id;
}
inline Event makeEvent(Identity& id, const std::string& type, const json& payload, long long nowMs, const Rng& rng = defaultRng()) {
    if (!knownEventType(type)) throw std::runtime_error("unknown registry event type " + type);
    Event e;
    e.v = 1;
    e.id = hex(rng(16));
    e.type = type;
    e.hlc = id.clock.send(nowMs);
    e.dev = id.address;
    e.payload = payload;
    logos_sync::SoftwareSigner s(id.priv);
    logos_sync::signEvent(s, DOMAIN, e);
    return e;
}

// ---- policy + planner ---------------------------------------------------------------------
struct Parcel { int category = 1, boxClass = 1, weightClass = 1, coreGrams = 180, sleeveGrams = DEFAULT_SLEEVE_G; };

inline bool listHas(const json& arr, const json& v) {
    if (!arr.is_array()) return false;
    for (const auto& x : arr) if (x == v) return true;
    return false;
}
inline bool policyAccepts(const HubCard& h, const Parcel& p) {
    const json& pol = h.policy;
    json acc = pol.value("accepts", json::array());
    if (!(listHas(acc, "*") || listHas(acc, p.category))) return false;
    if (pol.contains("boxClasses") && pol["boxClasses"].is_array() && !listHas(pol["boxClasses"], p.boxClass)) return false;
    int maxW = pol.value("maxWeightClass", 0);
    if (maxW && p.weightClass > maxW) return false;
    return true;
}
inline bool shipsTo(const HubCard& h, const std::string& country) {
    json s = h.policy.value("shipsTo", json::array());
    return listHas(s, "*") || listHas(s, country);
}
inline bool eligible(const HubCard& h, const Parcel& p, size_t minVouches) {
    return h.vouchedBy.size() >= minVouches && policyAccepts(h, p) && !h.labelKeys.empty();
}
inline uint32_t rngU32(const Rng& rng) {
    Bytes b = rng(4);
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

inline std::vector<std::string> findPath(const Directory& d, const std::string& entry, const std::string& exit, size_t hops,
                                         const Parcel& parcel, size_t minVouches, const Rng& rng) {
    auto hub = [&](const std::string& a) -> const HubCard* { auto it = d.hubs.find(a); return it == d.hubs.end() ? nullptr : &it->second; };
    if (!hub(entry) || !hub(exit) || !eligible(*hub(entry), parcel, minVouches) || !eligible(*hub(exit), parcel, minVouches)) return {};
    if (hops == 1) return entry == exit ? std::vector<std::string>{entry} : std::vector<std::string>{};
    std::vector<std::string> path{entry};
    std::function<bool()> dfs = [&]() -> bool {
        const std::string cur = path.back();
        auto inPath = [&](const std::string& a) { return std::find(path.begin(), path.end(), a) != path.end(); };
        if (path.size() == hops - 1) {
            if (grantFor(d, cur, exit) && shipsTo(*hub(cur), hub(exit)->country) && !inPath(exit)) { path.push_back(exit); return true; }
            return false;
        }
        std::vector<std::string> cand;
        for (const auto& [a, h] : d.hubs) {
            if (a == exit || inPath(a) || !eligible(h, parcel, minVouches) || !grantFor(d, cur, a) || !shipsTo(*hub(cur), h.country)) continue;
            bool sameCity = false;
            for (const auto& p : path) if (hub(p)->city == h.city) sameCity = true;
            if (!sameCity) cand.push_back(a);
        }
        for (size_t i = cand.size(); i > 1; i--) std::swap(cand[i - 1], cand[rngU32(rng) % i]);
        for (const auto& a : cand) {
            path.push_back(a);
            if (dfs()) return true;
            path.pop_back();
        }
        return false;
    };
    return dfs() ? path : std::vector<std::string>{};
}

struct PlannedHop { std::string hub; Routing routing; };
struct Plan {
    std::vector<std::string> path;
    std::vector<PlannedHop> hops;
    Bytes label;
    std::vector<Bytes> custody;   // custody[i] shared by hop i-1 (out) and hop i (in)
};

// Throws a human-readable error when no route exists.
inline Plan planRoute(const Directory& d, const Parcel& parcel, const std::string& entry, const std::string& mailboxRef,
                      size_t hops, int holdMinDays, int holdMaxDays, long long atMs, size_t minVouches = 1, const Rng& rng = defaultRng()) {
    if (hops < 1 || hops > MAX_HOPS) throw std::runtime_error("route length must be 1..5 hops");
    auto mb = d.mailboxes.find(mailboxRef);
    if (mb == d.mailboxes.end()) throw std::runtime_error("unknown mailbox (has the recipient's mailbox synced yet?)");
    for (auto [role, addr] : {std::pair<const char*, std::string>{"entry", entry}, {"exit", mb->second.exit}}) {
        auto it = d.hubs.find(addr);
        if (it == d.hubs.end()) throw std::runtime_error(std::string(role) + " hub is not in the directory");
        if (it->second.vouchedBy.size() < minVouches) throw std::runtime_error(std::string(role) + " hub " + it->second.name + " has no steward vouch");
        if (!policyAccepts(it->second, parcel)) throw std::runtime_error(std::string(role) + " hub " + it->second.name + " does not carry this parcel (category/size policy)");
    }
    Plan plan;
    plan.path = findPath(d, entry, mb->second.exit, hops, parcel, minVouches, rng);
    if (plan.path.empty()) throw std::runtime_error("no route satisfies the constraints (try fewer hops, another entry hub, or a broader category)");
    for (size_t i = 0; i <= hops; i++) plan.custody.push_back(rng(16));
    std::vector<HopSpec> specs;
    for (size_t i = 0; i < hops; i++) {
        const HubCard& h = d.hubs.at(plan.path[i]);
        Bytes lk = liveLabelKey(h, atMs);
        if (lk.empty()) throw std::runtime_error("hub " + h.name + " has no live label key");
        int cap = h.policy.value("holdMaxDays", 14);
        int lo = std::min(holdMinDays, cap);
        int hi = std::min(std::max(holdMaxDays, lo), cap);
        bool isExit = i == hops - 1;
        Routing r;
        r.type = isExit ? HOP_EXIT : HOP_RELAY;
        r.holdMin = lo; r.holdMax = hi;
        r.boxClass = parcel.boxClass; r.weightClass = parcel.weightClass;
        r.category = parcel.category;
        r.netGrams = parcel.coreGrams + (int)(hops - i) * parcel.sleeveGrams;
        r.sleeveCode = rng(8);
        r.custodyIn = plan.custody[i];
        r.custodyOut = plan.custody[i + 1];
        r.ref = unhex(isExit ? mailboxRef : grantFor(d, plan.path[i], plan.path[i + 1])->ref);
        plan.hops.push_back({plan.path[i], r});
        specs.push_back({lk, r});
    }
    plan.label = buildLabel(specs, rng);
    return plan;
}

// ---- what a hub does when a parcel arrives -------------------------------------------------
struct HubKeys {
    std::string address;
    std::vector<Bytes> labelPrivs;
    Bytes boxPriv;
    json policy = json::object();
    std::set<std::string> seen;   // replay tags (hex)
    int boxGrams = 120, sleeveGrams = DEFAULT_SLEEVE_G;
};
struct OutReceipt { Bytes bytes; long long postAfterMs = 0; };
struct Arrival {
    std::string action;   // relay | exit | refuse
    std::string reason;
    Routing routing;
    bool haveRouting = false;
    std::string removeSleeve, shipDay, nextHub, nextLabel;
    json address;         // where to ship (relay: next hub's handoff; exit: recipient pickup)
    int paddingGrams = 0;
    std::vector<OutReceipt> receipts;
};

constexpr long long DAY_MS = 86400000LL;

inline long long nextBatchDay(long long ms, const json& batchDays) {
    if (!batchDays.is_array() || batchDays.empty()) return ms;
    for (int dd = 0; dd < 7; dd++) {
        long long t = ms + dd * DAY_MS;
        time_t s = (time_t)(t / 1000);
        struct tm g; gmtime_r(&s, &g);
        if (listHas(batchDays, g.tm_wday)) return t;
    }
    return ms;
}

inline Arrival processArrival(HubKeys& hub, const Directory& d, const std::string& labelText, const std::string& sleeveCode,
                              int grossGrams, int boxGrams, long long atMs, const Rng& rng = defaultRng()) {
    Arrival out;
    Peeled peeled;
    try { peeled = peelWithAnyKey(labelFromText(labelText), hub.labelPrivs); }
    catch (const std::exception& e) { out.action = "refuse"; out.reason = std::string("label can't be read by this hub: ") + e.what(); return out; }
    const Routing& r = peeled.routing;
    out.routing = r; out.haveRouting = true;
    auto uniform = [&](int lo, int hi) { return hi <= lo ? lo : lo + (int)(rngU32(rng) % (uint32_t)(hi - lo + 1)); };
    auto post = [&](const Bytes& token, nlohmann::ordered_json body, long long whenMs) {
        body["day"] = dayOf(whenMs);
        long long delay = (long long)uniform(2, 30) * 3600000LL;
        out.receipts.push_back({makeReceipt(token, body, rng), whenMs + delay});
    };
    auto refuse = [&](const std::string& why) {
        post(r.custodyIn, {{"kind", "refused"}, {"reason", why}}, atMs);
        out.action = "refuse"; out.reason = why;
        return out;
    };
    std::string tag = hex(peeled.replayTag);
    if (hub.seen.count(tag)) { out.action = "refuse"; out.reason = "replay: this label was already processed"; return out; }
    hub.seen.insert(tag);

    json acc = hub.policy.value("accepts", json::array());
    if (!(listHas(acc, "*") || listHas(acc, r.category))) return refuse("category not accepted: " + categoryName(r.category));
    Bytes typed;
    if (!sleeveCodeFromText(sleeveCode, typed)) return refuse("sleeve seal code unreadable");
    if (!equalCt(typed, r.sleeveCode)) return refuse("sleeve seal code does not match (possible tampering)");
    if (grossGrams > 0 && boxGrams >= 0) {
        auto band = weightClasses().find(r.weightClass);
        int net = grossGrams - boxGrams;
        if (band == weightClasses().end() || net > band->second.target + WEIGHT_TOLERANCE_G)
            return refuse("overweight for its class (" + std::to_string(net) + " g)");
    }
    post(r.custodyIn, {{"kind", "received"}, {"sleeve", "intact"}}, atMs);

    int holdDays = uniform(r.holdMin, r.holdMax);
    long long shipMs = nextBatchDay(atMs + holdDays * DAY_MS, hub.policy.value("batchDays", json::array()));
    out.shipDay = dayOf(shipMs);
    out.removeSleeve = sleeveCodeText(r.sleeveCode);
    try { out.paddingGrams = paddingFor(r.weightClass, r.netGrams - hub.sleeveGrams, hub.boxGrams); }
    catch (const std::exception& e) { return refuse(e.what()); }

    std::string ref = hex(r.ref);
    if (peeled.hasNext) {
        auto g = d.grants.find(ref);
        if (g == d.grants.end() || g->second.from != hub.address) return refuse("no address grant for the next hop (revoked?)");
        Bytes blob;
        try { blob = unb64url(g->second.sealed); } catch (...) { return refuse("next hop's address grant is malformed"); }
        if (!openSealedJson(hub.boxPriv, blob, grantContext(g->second.from, g->second.to), out.address)) return refuse("next hop's address grant can't be opened");
        post(r.custodyOut, {{"kind", "shipped"}}, shipMs);
        out.action = "relay";
        out.nextHub = g->second.to;
        out.nextLabel = labelToText(peeled.next);
        return out;
    }
    auto m = d.mailboxes.find(ref);
    if (m == d.mailboxes.end() || m->second.exit != hub.address) return refuse("unknown mailbox");
    Bytes blob;
    try { blob = unb64url(m->second.sealed); } catch (...) { return refuse("mailbox is malformed"); }
    if (!openSealedJson(hub.boxPriv, blob, mailboxContext(m->second.exit, ref), out.address)) return refuse("mailbox can't be opened");
    post(r.custodyOut, {{"kind", "delivered"}}, shipMs);
    std::string notify = out.address.value("notify", "");
    if (notify.size() == 32) {
        nlohmann::ordered_json b = {{"kind", "ready"}, {"day", dayOf(shipMs)}};
        out.receipts.push_back({makeReceipt(unhex(notify), b, rng), shipMs});
    }
    out.address.erase("notify");
    out.action = "exit";
    return out;
}

} // namespace mulenet
