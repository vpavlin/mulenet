// Cross-implementation engine test: the C++ engine folds the JS-built directory, walks
// a JS-planned parcel through C++ hub logic, then plans and walks its own parcel.
#include "mulenet_engine.hpp"
#include <fstream>
#include <iostream>

using namespace mulenet;
static int fails = 0, passes = 0;
#define CHECK(cond, what) do { if (cond) passes++; else { fails++; std::cerr << "FAIL: " << what << "\n"; } } while (0)

struct Walk { std::vector<Arrival> steps; std::vector<OutReceipt> inbox; };

static Walk walk(std::vector<HubKeys>& hubs, const Directory& d, const std::vector<std::string>& path,
                 std::string label, const std::vector<std::string>& sleeves, long long t0) {
    Walk w;
    long long at = t0;
    for (size_t i = 0; i < path.size(); i++) {
        HubKeys* h = nullptr;
        for (auto& x : hubs) if (x.address == path[i]) h = &x;
        Arrival a = processArrival(*h, d, label, sleeves[i], 430, 120, at);
        w.inbox.insert(w.inbox.end(), a.receipts.begin(), a.receipts.end());
        w.steps.push_back(a);
        if (a.action != "relay") break;
        label = a.nextLabel;
        at = dayStartMs(a.shipDay) + 2 * DAY_MS;
    }
    return w;
}

int main(int argc, char** argv) {
    std::ifstream f(argc > 1 ? argv[1] : "vectors.json");
    json v = json::parse(f);
    const json& N = v["network"];

    std::vector<Event> log;
    for (const auto& e : N["log"]) log.push_back(logos_sync::eventFromJson(e));
    std::set<std::string> roots;
    for (const auto& r : N["roots"]) roots.insert(r.get<std::string>());
    Directory d = foldRegistry(log, roots);
    CHECK(d.hubs.size() == 5, "five hubs folded");
    CHECK(d.grants.size() == 20, "every pairwise grant folded (" << d.grants.size() << ")");
    CHECK(d.mailboxes.count(N["mailboxRef"].get<std::string>()) == 1, "mailbox folded");
    for (const auto& [a, h] : d.hubs) CHECK(h.vouchedBy.size() == 1, "hub vouched");

    std::vector<HubKeys> hubs;
    for (const auto& h : N["hubs"]) {
        HubKeys k;
        k.address = h["address"];
        for (const auto& p : h["labelPrivs"]) k.labelPrivs.push_back(unhex(p));
        k.boxPriv = unhex(h["boxPriv"]);
        k.policy = h["policy"];
        hubs.push_back(k);
    }

    // 1) the JS-planned parcel, processed by C++ hubs
    std::vector<std::string> path, sleeves;
    for (const auto& p : N["route"]["path"]) path.push_back(p);
    for (const auto& s : N["route"]["sleeves"]) sleeves.push_back(s);
    Walk w = walk(hubs, d, path, N["route"]["label"], sleeves, N["T0"]);
    CHECK(w.steps.size() == 3 && w.steps[0].action == "relay" && w.steps[1].action == "relay" && w.steps[2].action == "exit", "JS parcel relays twice then exits");
    CHECK(w.steps[0].nextHub == path[1] && w.steps[1].nextHub == path[2], "each hub opens the grant for the right next hop");
    CHECK(w.steps[2].address.value("locker", "") == "MAD-CITYLOCKER-7", "exit opens the JS-sealed mailbox");
    CHECK(!w.steps[2].address.contains("notify"), "notify token is not shown to the hub user");
    std::vector<Bytes> custody;
    for (const auto& c : N["route"]["custody"]) custody.push_back(unhex(c));
    int matched = 0;
    for (const auto& r : w.inbox) for (const auto& t : custody) { json b; if (openReceipt(t, r.bytes, b)) matched++; }
    CHECK(matched == 6, "sender opens every custody receipt (" << matched << ")");
    json ready;
    bool gotReady = false;
    for (const auto& r : w.inbox) if (openReceipt(unhex(N["notify"]), r.bytes, ready) && ready["kind"] == "ready") gotReady = true;
    CHECK(gotReady, "recipient gets a ready notice");

    // replay
    Arrival again = processArrival(hubs[0], d, N["route"]["label"], sleeves[0], 430, 120, N["T0"]);
    CHECK(again.action == "refuse" && again.reason.find("replay") != std::string::npos, "replay refused");

    // 2) a C++-planned parcel, walked by C++ hubs
    Parcel p;
    p.category = 1; p.boxClass = 1; p.weightClass = 1; p.coreGrams = 180;
    Plan plan = planRoute(d, p, hubs[1].address, N["mailboxRef"], 4, 1, 3, N["T0"]);
    CHECK(plan.path.size() == 4 && plan.path.front() == hubs[1].address, "C++ plans a 4-hop route");
    std::vector<std::string> cs;
    for (const auto& h : plan.hops) cs.push_back(sleeveCodeText(h.routing.sleeveCode));
    Walk w2 = walk(hubs, d, plan.path, labelToText(plan.label), cs, N["T0"]);
    CHECK(w2.steps.size() == 4 && w2.steps.back().action == "exit", "C++ parcel arrives");

    // 3) tamper: wrong sleeve at hop 2
    Plan plan3 = planRoute(d, p, hubs[0].address, N["mailboxRef"], 3, 1, 3, N["T0"]);
    std::vector<std::string> bad;
    for (const auto& h : plan3.hops) bad.push_back(sleeveCodeText(h.routing.sleeveCode));
    bad[1] = "0000-0000-00000";
    Walk w3 = walk(hubs, d, plan3.path, labelToText(plan3.label), bad, N["T0"]);
    CHECK(w3.steps.size() == 2 && w3.steps[1].action == "refuse", "swapped sleeve refused");

    // 4) policy errors are specific
    Parcel und = p; und.category = 99;
    std::string err;
    try { planRoute(d, und, hubs[0].address, N["mailboxRef"], 3, 1, 3, N["T0"]); } catch (const std::exception& e) { err = e.what(); }
    CHECK(err.empty(), "fixture hubs carry anything");
    HubKeys& prague = hubs[0];
    prague.policy["accepts"] = json::array({2});
    Plan plan4 = planRoute(d, p, hubs[0].address, N["mailboxRef"], 3, 1, 3, N["T0"]);
    Arrival a4 = processArrival(prague, d, labelToText(plan4.label), sleeveCodeText(plan4.hops[0].routing.sleeveCode), 430, 120, N["T0"]);
    CHECK(a4.action == "refuse" && a4.reason.find("category") != std::string::npos, "hub refuses a category it doesn't carry");

    // 5) forged event is ignored
    json forged = N["log"][0];
    forged["payload"]["city"] = "Nowhere";
    log.push_back(logos_sync::eventFromJson(forged));
    Directory d2 = foldRegistry(log, roots);
    CHECK(d2.hubs.at(N["hubs"][0]["address"].get<std::string>()).city == "Prague", "forged announce ignored");

    std::cout << passes << " passed, " << fails << " failed\n";
    return fails ? 1 : 0;
}
