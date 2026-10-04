#include "mulenet_core_impl.h"
#include "logos_sdk.h"
#include "qrcodegen.hpp"
#include "logos_sync/catchup.hpp"
#include <QTimer>
#include <QObject>
#include <openssl/evp.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

using namespace mulenet;
using ojson = nlohmann::ordered_json;

static const char* MULENET_VERSION = "0.1.0";
static constexpr long long CATCHUP_EVERY_MS = 30000;
static constexpr long long RECEIPT_RETENTION_MS = 90LL * DAY_MS;

// ---- small helpers -----------------------------------------------------------------------
static std::string b64std(const std::string& s) {
    std::string out(4 * ((s.size() + 2) / 3) + 1, '\0');
    int n = EVP_EncodeBlock((unsigned char*)out.data(), (const unsigned char*)s.data(), (int)s.size());
    out.resize(n < 0 ? 0 : n);
    return out;
}
static bool unb64std(const std::string& in, std::string& out) {
    std::string s;
    for (char c : in) if (!isspace((unsigned char)c)) s += c;
    if (s.empty() || s.size() % 4) return false;
    std::string buf(s.size() / 4 * 3 + 1, '\0');
    int n = EVP_DecodeBlock((unsigned char*)buf.data(), (const unsigned char*)s.data(), (int)s.size());
    if (n < 0) return false;
    size_t pad = 0;
    if (s.size() >= 1 && s[s.size() - 1] == '=') pad++;
    if (s.size() >= 2 && s[s.size() - 2] == '=') pad++;
    buf.resize(n - pad);
    out = buf;
    return true;
}
static std::string ok(json extra = json::object()) { extra["ok"] = true; return extra.dump(); }
static std::string realHome() {
    if (struct passwd* pw = getpwuid(getuid())) if (pw->pw_dir && *pw->pw_dir == '/') return pw->pw_dir;
    return "";
}
static bool dirWritable(const std::string& d) {
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    std::string probe = d + "/.write-test";
    std::ofstream f(probe);
    if (!f) return false;
    f << "ok";
    f.close();
    std::filesystem::remove(probe, ec);
    return true;
}
static json parseArg(const std::string& s) {
    // Basecamp 0.2.3 may hand strings over JSON-quoted; peel up to two layers.
    json j = json::parse(s, nullptr, false);
    for (int i = 0; i < 2 && j.is_string(); i++) j = json::parse(j.get<std::string>(), nullptr, false);
    return j;
}
static std::string unquote(std::string s) {
    while (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

MulenetCoreImpl::~MulenetCoreImpl() {
    if (m_timer) { m_timer->stop(); delete m_timer; }
}

long long MulenetCoreImpl::nowMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string MulenetCoreImpl::fail(const std::string& why) { return json{{"ok", false}, {"error", why}}.dump(); }

// ---- persistence -------------------------------------------------------------------------
void MulenetCoreImpl::setupDataDir() {
    std::vector<std::string> cands;
    if (const char* ov = std::getenv("MULENET_CORE_DATA")) cands.push_back(ov);
    else {
        if (!realHome().empty()) cands.push_back(realHome() + "/.mulenet-core");
        if (!instancePersistencePath().empty()) cands.push_back(instancePersistencePath());
    }
    for (const auto& c : cands) if (dirWritable(c)) { m_dataDir = c; return; }
    m_storageOk = false;
    m_dataDir = cands.empty() ? "/tmp/mulenet-core" : cands[0];
    m_storageNote = "Can't save anything to " + m_dataDir + " - nothing will survive a restart";
    fprintf(stderr, "[mulenet] STORAGE NOT WRITABLE: %s\n", m_dataDir.c_str());
}

json MulenetCoreImpl::loadFile(const std::string& name, const json& dflt) {
    std::ifstream f(m_dataDir + "/" + name);
    if (!f) return dflt;
    std::stringstream ss; ss << f.rdbuf();
    json j = json::parse(ss.str(), nullptr, false);
    return j.is_discarded() ? dflt : j;
}
void MulenetCoreImpl::saveFile(const std::string& name, const json& j) {
    if (!m_storageOk) return;
    std::string tmp = m_dataDir + "/" + name + ".tmp";
    { std::ofstream f(tmp); f << j.dump(); }
    std::error_code ec;
    std::filesystem::rename(tmp, m_dataDir + "/" + name, ec);
    if (ec) fprintf(stderr, "[mulenet] save %s failed: %s\n", name.c_str(), ec.message().c_str());
}

void MulenetCoreImpl::loadAll() {
    json id = loadFile("identity.json", json::object());
    Bytes priv;
    if (id.contains("priv") && id["priv"].is_string()) { try { priv = unhex(id["priv"]); } catch (...) {} }
    if (priv.size() != 32) {
        priv = logos_sync::generatePrivateKey();
        saveFile("identity.json", json{{"priv", hex(priv)}});
    }
    m_id = identityFrom(priv);

    json settings = loadFile("settings.json", json::object());
    m_roots.clear();
    if (settings.contains("roots") && settings["roots"].is_array()) {
        for (const auto& r : settings["roots"]) if (r.is_string()) m_roots.insert(r.get<std::string>());
    } else {
        m_roots.insert(m_id.address);   // you are your own trust root by default
    }

    m_log.clear();
    for (const auto& e : loadFile("registry.json", json::array())) m_log.push_back(logos_sync::eventFromJson(e));
    for (const auto& e : m_log) if (e.dev == m_id.address) m_id.clock.receive(e.hlc);
    m_receiptLog.clear();
    for (const auto& e : loadFile("receipts.json", json::array())) m_receiptLog.push_back(logos_sync::eventFromJson(e));

    json hub = loadFile("hub.json", json::object());
    m_isHub = hub.contains("boxPriv");
    if (m_isHub) {
        m_hub.address = m_id.address;
        m_hub.boxPriv = unhex(hub["boxPriv"]);
        m_labelKeys = hub.value("labelKeys", json::array());
        m_hub.labelPrivs.clear();
        for (const auto& k : m_labelKeys) m_hub.labelPrivs.push_back(unhex(k["priv"]));
        m_hubCard = hub.value("card", json::object());
        m_hub.policy = m_hubCard.value("policy", json::object());
        m_hub.boxGrams = hub.value("boxGrams", 120);
        for (const auto& t : hub.value("seen", json::array())) m_hub.seen.insert(t.get<std::string>());
    }
    m_parcels = loadFile("parcels.json", json::array());
    m_jobs = loadFile("jobs.json", json::array());
    m_mailboxes = loadFile("mailboxes.json", json::array());
    m_outbox = loadFile("outbox.json", json::array());
}

void MulenetCoreImpl::saveRegistry() {
    json a = json::array();
    for (const auto& e : m_log) a.push_back(logos_sync::eventToJson(e));
    saveFile("registry.json", a);
}
void MulenetCoreImpl::saveReceipts() {
    json a = json::array();
    for (const auto& e : m_receiptLog) a.push_back(logos_sync::eventToJson(e));
    saveFile("receipts.json", a);
    saveFile("outbox.json", m_outbox);
}
void MulenetCoreImpl::saveHub() {
    if (!m_isHub) { std::error_code ec; std::filesystem::remove(m_dataDir + "/hub.json", ec); return; }
    json seen = json::array();
    for (const auto& t : m_hub.seen) seen.push_back(t);
    saveFile("hub.json", json{{"boxPriv", hex(m_hub.boxPriv)}, {"labelKeys", m_labelKeys}, {"card", m_hubCard}, {"boxGrams", m_hub.boxGrams}, {"seen", seen}});
}
void MulenetCoreImpl::saveParcels() { saveFile("parcels.json", m_parcels); }
void MulenetCoreImpl::saveJobs() { saveFile("jobs.json", m_jobs); }
void MulenetCoreImpl::saveMailboxes() { saveFile("mailboxes.json", m_mailboxes); }
void MulenetCoreImpl::saveSettings() {
    json r = json::array();
    for (const auto& a : m_roots) r.push_back(a);
    saveFile("settings.json", json{{"roots", r}});
}

// ---- directory ---------------------------------------------------------------------------
void MulenetCoreImpl::refold() { m_dir = foldRegistry(m_log, m_roots); }

bool MulenetCoreImpl::ingestRegistryEvent(const Event& e) {
    if (!knownEventType(e.type) || !logos_sync::verifyEvent(DOMAIN, e)) return false;
    for (const auto& x : m_log) if (x.id == e.id) return false;
    m_log.push_back(e);
    if (e.dev == m_id.address) m_id.clock.receive(e.hlc);
    return true;
}

void MulenetCoreImpl::author(const std::string& type, const json& payload) {
    Event e = makeEvent(m_id, type, payload, nowMs());
    m_log.push_back(e);
    refold();
    saveRegistry();
    sendFrame(REGISTRY_TOPIC, json{{"t", "ev"}, {"e", logos_sync::eventToJson(e)}});
}

std::string MulenetCoreImpl::hubName(const std::string& address) const {
    auto it = m_dir.hubs.find(address);
    if (it == m_dir.hubs.end()) return "unknown hub";
    return it->second.name.empty() ? it->second.city : it->second.name;
}

// ---- receipts ----------------------------------------------------------------------------
// A receipt rides the receipts topic as an unsigned event whose id is the hash of its bytes
// (so RBSR catch-up can backfill receipts a sender missed while offline). The event's wall
// is the receipt's UTC day, not the moment it was posted.
static Event receiptEvent(const Bytes& bytes, long long dayMs) {
    Event e;
    e.v = 1;
    e.id = hex(slice(sha256(bytes), 0, 16));
    e.type = "receipt";
    e.hlc.wall = dayMs;
    e.payload = json{{"b", b64url(bytes)}};
    return e;
}

void MulenetCoreImpl::queueReceipt(const OutReceipt& r) {
    // MULENET_NO_RECEIPT_DELAY (tests, demos): post on the next tick instead of hours later.
    long long after = std::getenv("MULENET_NO_RECEIPT_DELAY") ? 0 : r.postAfterMs;
    m_outbox.push_back(json{{"b", b64url(r.bytes)}, {"after", after}});
}

void MulenetCoreImpl::flushOutbox() {
    long long now = nowMs();
    json keep = json::array();
    bool sent = false;
    for (const auto& o : m_outbox) {
        if (o.value("after", 0LL) > now) { keep.push_back(o); continue; }
        Bytes b;
        try { b = unb64url(o.value("b", "")); } catch (...) { continue; }
        Event e = receiptEvent(b, (now / DAY_MS) * DAY_MS);
        bool dup = false;
        for (const auto& x : m_receiptLog) if (x.id == e.id) dup = true;
        if (!dup) m_receiptLog.push_back(e);
        sendFrame(RECEIPTS_TOPIC, json{{"t", "ev"}, {"e", logos_sync::eventToJson(e)}});
        sent = true;
    }
    if (sent) { m_outbox = keep; saveReceipts(); }
}

// ---- transport ---------------------------------------------------------------------------
void MulenetCoreImpl::sendFrame(const std::string& topic, const json& frame) {
    if (!m_ready) return;   // catch-up delivers anything missed once we're connected
    m_tx++;
    try { modules().loam_core.sendSealedAsync(topic, b64std(frame.dump()), [](std::string) {}); }
    catch (const std::exception& e) { fprintf(stderr, "[mulenet] send failed: %s\n", e.what()); }
}

void MulenetCoreImpl::onFrame(const std::string& topic, const std::string& payloadB64) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    m_rx++;
    // Peers wrap a different number of base64 layers; peel until we reach the JSON frame.
    std::string s = payloadB64, dec;
    json f;
    for (int i = 0; i < 3; i++) {
        size_t p = s.find_first_not_of(" \n\r\t");
        if (p != std::string::npos && s[p] == '{') { f = json::parse(s, nullptr, false); break; }
        if (!unb64std(s, dec)) break;
        s = dec;
    }
    if (!f.is_object()) { m_rxBad++; return; }
    std::string t = f.value("t", "");
    bool reg = topic == REGISTRY_TOPIC, rec = topic == RECEIPTS_TOPIC;
    if (!reg && !rec) return;
    std::vector<Event>& log = reg ? m_log : m_receiptLog;
    bool changed = false;
    auto take = [&](const json& ej) {
        Event e = logos_sync::eventFromJson(ej);
        if (reg) { if (ingestRegistryEvent(e)) { m_rxEvents++; changed = true; } return; }
        // receipts: the id must be the hash of the bytes
        Bytes b;
        try { b = unb64url(e.payload.value("b", "")); } catch (...) { return; }
        if (e.type != "receipt" || b.size() < 29 || e.id != hex(slice(sha256(b), 0, 16))) return;
        for (const auto& x : m_receiptLog) if (x.id == e.id) return;
        if (e.hlc.wall < nowMs() - RECEIPT_RETENTION_MS) return;
        m_receiptLog.push_back(e);
        m_rxReceipts++;
        changed = true;
    };
    if (t == "ev" && f.contains("e")) take(f["e"]);
    else if (t == "fp" || t == "ids" || t == "need") {
        auto step = logos_sync::catchup::respond(log, f, m_id.address);
        for (const auto& r : step.replies) sendFrame(topic, r);
        for (const auto& e : step.serve) sendFrame(topic, json{{"t", "ev"}, {"e", logos_sync::eventToJson(e)}});
    }
    if (changed) {
        if (reg) { refold(); saveRegistry(); } else saveReceipts();
        publishState();
    }
}

void MulenetCoreImpl::catchupRound() {
    m_lastCatchupMs = nowMs();
    sendFrame(REGISTRY_TOPIC, logos_sync::catchup::buildInitial(m_log, m_id.address));
    sendFrame(RECEIPTS_TOPIC, logos_sync::catchup::buildInitial(m_receiptLog, m_id.address));
}

void MulenetCoreImpl::startTransport() {
    if (m_started) return;
    m_started = true;
    json cfg = json::object();
    if (const char* env = std::getenv("MULENET_DELIVERY_CFG")) {
        json p = json::parse(std::string(env), nullptr, false);
        if (p.is_object()) cfg = p;
    }
    if (cfg.empty()) {
        // The flat WakuNodeConf the deployed delivery 0.1.x accepts (same as scala's default).
        cfg = json{{"mode", "Core"}, {"preset", "logos.test"}, {"relay", true}, {"logLevel", "INFO"}, {"useChannels", true},
                   {"entryNodes", json::array({
                        "/dns4/node-01.do-ams3.logos.test.status.im/tcp/30303/p2p/16Uiu2HAmQ9X2xDfPG3uL77V9piYDhjq14JhKCtcmNYsTMKNqrKCj",
                        "/dns4/node-02.do-ams3.logos.test.status.im/tcp/30303/p2p/16Uiu2HAmB8NYprrfQrgWVzsJtYWkfjsXbmJEGNMG6othXsQ53BwG",
                        "/dns4/node-01.gc-us-central1-a.logos.test.status.im/tcp/30303/p2p/16Uiu2HAmF8WtwGPmeGHgYAX2277jHgy5cW9F7zsB8EqUjBZQAZQ3",
                        "/dns4/node-02.gc-us-central1-a.logos.test.status.im/tcp/30303/p2p/16Uiu2HAmUuXhUW9bdJpzN1kfDziFiUZo4bszTk66cvr7uuyCHXR7"})}};
    }
    try {
        modules().loam_core.onReceived([this](const std::string& topic, const std::string&, const std::string& payloadB64, int64_t) {
            onFrame(topic, payloadB64);
        });
        modules().loam_core.onStatusChanged([this](const std::string& s) {
            std::lock_guard<std::recursive_mutex> lk(m_mtx);
            m_status = s;
            if (s == "Connected" && !m_ready) {
                m_ready = true;
                modules().loam_core.joinAsync(REGISTRY_TOPIC, [](std::string) {});
                modules().loam_core.joinAsync(RECEIPTS_TOPIC, [](std::string) {});
                // The gossip mesh forms ~10 s after start: retry the first catch-up a few times.
                for (int ms : {3000, 10000, 25000})
                    QTimer::singleShot(ms, m_timer, [this] { std::lock_guard<std::recursive_mutex> l(m_mtx); catchupRound(); });
            } else if (s == "Connected") {
                catchupRound();
            }
            publishState();
        });
        modules().loam_core.setSenderIdAsync(m_id.address, [](std::string) {});
        modules().loam_core.startAsync(cfg.dump(), [this](std::string err) {
            if (!err.empty()) { std::lock_guard<std::recursive_mutex> lk(m_mtx); m_status = "Transport error: " + err; publishState(); }
        });
        m_status = "Connecting...";
    } catch (const std::exception& e) {
        m_status = std::string("loam_core unavailable: ") + e.what();
    }
}

void MulenetCoreImpl::tick() {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    flushOutbox();
    if (m_ready && nowMs() - m_lastCatchupMs > CATCHUP_EVERY_MS) catchupRound();
    // Drop receipts past retention.
    long long cutoff = nowMs() - RECEIPT_RETENTION_MS;
    size_t before = m_receiptLog.size();
    m_receiptLog.erase(std::remove_if(m_receiptLog.begin(), m_receiptLog.end(), [&](const Event& e) { return e.hlc.wall < cutoff; }), m_receiptLog.end());
    if (m_receiptLog.size() != before) saveReceipts();
}

void MulenetCoreImpl::onContextReady() {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    setupDataDir();
    loadAll();
    refold();
    // One timer on the module's event-loop thread drives everything (also headless).
    m_timer = new QTimer();
    QObject::connect(m_timer, &QTimer::timeout, m_timer, [this] { tick(); });
    int tickMs = 5000;
    if (const char* t = std::getenv("MULENET_TICK_MS")) tickMs = std::max(50, atoi(t));
    m_timer->start(tickMs);
    startTransport();
    publishState();
}

// ---- state -------------------------------------------------------------------------------
void MulenetCoreImpl::publishState() {
    try { stateChanged(buildSnapshot().dump()); } catch (...) {}
}

static json receiptsFor(const std::vector<Event>& log, const Bytes& token) {
    json out = json::array();
    for (const auto& e : log) {
        Bytes b;
        try { b = unb64url(e.payload.value("b", "")); } catch (...) { continue; }
        json body;
        if (openReceipt(token, b, body)) out.push_back(body);
    }
    return out;
}

json MulenetCoreImpl::buildSnapshot() {
    json s = json::object();
    s["ok"] = true;
    s["version"] = MULENET_VERSION;
    s["status"] = m_status;
    s["storage"] = json{{"ok", m_storageOk}, {"dir", m_dataDir}, {"note", m_storageNote}};
    json roots = json::array();
    for (const auto& r : m_roots) roots.push_back(r);
    s["me"] = json{{"address", m_id.address}, {"roots", roots}, {"isSteward", m_dir.stewards.count(m_id.address) > 0}};

    json hubs = json::array();
    for (const auto& [addr, h] : m_dir.hubs) {
        bool vouchedByMe = std::find(h.vouchedBy.begin(), h.vouchedBy.end(), m_id.address) != h.vouchedBy.end();
        hubs.push_back(json{{"address", addr}, {"name", h.name}, {"city", h.city}, {"country", h.country},
                            {"policy", h.policy}, {"intake", h.intake}, {"vouchedBy", h.vouchedBy.size()}, {"vouchedByMe", vouchedByMe},
                            {"mine", addr == m_id.address},
                            {"canShipTo", m_isHub && grantFor(m_dir, m_id.address, addr) != nullptr},
                            {"iGranted", m_isHub && grantFor(m_dir, addr, m_id.address) != nullptr}});
    }
    s["directory"] = json{{"hubs", hubs}, {"grants", m_dir.grants.size()}, {"mailboxes", m_dir.mailboxes.size()}, {"events", m_log.size()}};

    if (m_isHub) {
        json myGrants = json::array();
        for (const auto& [ref, g] : m_dir.grants) if (g.to == m_id.address) myGrants.push_back(json{{"ref", ref}, {"to", g.from}, {"toName", hubName(g.from)}});
        s["hub"] = json{{"address", m_id.address}, {"card", m_hubCard}, {"listed", m_dir.hubs.count(m_id.address) > 0}, {"grants", myGrants}};
    } else s["hub"] = nullptr;

    json parcels = json::array();
    for (const auto& p : m_parcels) {
        json q = p;
        json steps = json::array();
        const json& custody = p["custody"];
        const json& path = p["path"];
        for (size_t i = 0; i < custody.size(); i++) {
            std::string from = i == 0 ? "You" : hubName(path[i - 1]);
            std::string to = i == path.size() ? "recipient's pickup" : hubName(path[i]);
            steps.push_back(json{{"step", from + " -> " + to}, {"receipts", receiptsFor(m_receiptLog, unhex(custody[i]))}});
        }
        q["steps"] = steps;
        q.erase("custody");
        parcels.push_back(q);
    }
    s["parcels"] = parcels;

    json jobs = json::array();
    for (const auto& j : m_jobs) {
        json q = j;
        if (j.contains("out")) {
            // The next hop's "received"/"refused" on our custodyOut token is this hub's alibi
            // (our own "shipped" rides the same token; leave it out).
            json alibi = json::array();
            for (const auto& r : receiptsFor(m_receiptLog, unhex(j["out"])))
                if (r.value("kind", "") == "received" || r.value("kind", "") == "refused") alibi.push_back(r);
            q["alibi"] = alibi;
        }
        q.erase("in"); q.erase("out"); q.erase("notify");
        jobs.push_back(q);
    }
    s["jobs"] = jobs;

    json boxes = json::array();
    for (const auto& m : m_mailboxes) {
        json q = m;
        q["exitName"] = hubName(m.value("exit", ""));
        q["listed"] = m_dir.mailboxes.count(m.value("ref", "")) > 0;
        q["notices"] = receiptsFor(m_receiptLog, unhex(m.value("notify", std::string(32, '0'))));
        q.erase("notify");
        boxes.push_back(q);
    }
    s["mailboxes"] = boxes;

    json cats = json::array();
    for (const auto& [id, name] : categories()) cats.push_back(json{{"id", id}, {"name", name}});
    json bcs = json::array();
    for (const auto& [id, b] : boxClasses()) bcs.push_back(json{{"id", id}, {"name", b.name}, {"dims", b.dims}});
    json wcs = json::array();
    for (const auto& [id, w] : weightClasses()) wcs.push_back(json{{"id", id}, {"max", w.max}, {"target", w.target}});
    s["catalog"] = json{{"categories", cats}, {"boxClasses", bcs}, {"weightClasses", wcs}};
    s["counters"] = json{{"rx", m_rx}, {"tx", m_tx}, {"rxEvents", m_rxEvents}, {"rxReceipts", m_rxReceipts}, {"rxBad", m_rxBad},
                         {"receipts", m_receiptLog.size()}, {"outbox", m_outbox.size()}};
    return s;
}

std::string MulenetCoreImpl::snapshot() {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    try { return buildSnapshot().dump(); } catch (const std::exception& e) { return fail(std::string("snapshot failed: ") + e.what()); }
}

std::string MulenetCoreImpl::resync() {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    if (m_ready) catchupRound();
    publishState();
    return snapshot();
}

// ---- hub ---------------------------------------------------------------------------------
std::string MulenetCoreImpl::setupHub(std::string cardJson) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    try {
        json c = parseArg(cardJson);
        if (!c.is_object()) return fail("Hub details are not valid JSON");
        std::string name = c.value("name", ""), city = c.value("city", ""), country = c.value("country", "");
        if (name.empty() || city.empty() || country.size() != 2) return fail("A hub needs a name, a city and a two-letter country code");
        json pol = c.value("policy", json::object());
        if (!pol.contains("accepts") || !pol["accepts"].is_array() || pol["accepts"].empty()) return fail("Choose what this hub carries (at least one category, or anything)");
        if (!pol.contains("shipsTo")) pol["shipsTo"] = json::array({"*"});
        if (!pol.contains("holdMaxDays")) pol["holdMaxDays"] = 7;
        if (!m_isHub) {
            KeyPair label = newX25519(), box = newX25519();
            m_hub = HubKeys();
            m_hub.address = m_id.address;
            m_hub.boxPriv = box.priv;
            m_hub.labelPrivs = {label.priv};
            m_labelKeys = json::array({json{{"epoch", 1}, {"priv", hex(label.priv)}, {"pub", hex(label.pub)}, {"notAfter", nowMs() + 365 * DAY_MS}}});
            m_isHub = true;
        }
        json pubKeys = json::array();
        for (const auto& k : m_labelKeys) pubKeys.push_back(json{{"epoch", k["epoch"]}, {"pub", k["pub"]}, {"notAfter", k["notAfter"]}});
        m_hubCard = json{{"name", name}, {"city", city}, {"country", country}, {"policy", pol},
                         {"intake", c.value("intake", json{{"kind", "none"}})}, {"labelKeys", pubKeys},
                         {"boxPub", hex(x25519Public(m_hub.boxPriv))}};
        m_hub.policy = pol;
        if (c.contains("boxGrams") && c["boxGrams"].is_number_integer()) m_hub.boxGrams = c["boxGrams"];
        saveHub();
        author("hub.announce", m_hubCard);
        // You are your own trust root: vouch for your own hub so your planner can use it.
        if (m_dir.stewards.count(m_id.address) && m_dir.hubs.count(m_id.address)) {
            auto& vb = m_dir.hubs.at(m_id.address).vouchedBy;
            if (std::find(vb.begin(), vb.end(), m_id.address) == vb.end()) author("vouch", json{{"hub", m_id.address}});
        }
        publishState();
        return ok(json{{"address", m_id.address}});
    } catch (const std::exception& e) { return fail(std::string("Couldn't set up the hub: ") + e.what()); }
}

std::string MulenetCoreImpl::retireHub() {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    if (!m_isHub) return fail("This device isn't running a hub");
    author("hub.retire", json::object());
    m_isHub = false;
    m_hub = HubKeys();
    m_hubCard = json::object();
    m_labelKeys = json::array();
    saveHub();
    publishState();
    return ok();
}

std::string MulenetCoreImpl::grantAddress(std::string hubAddress, std::string addressJson) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    hubAddress = unquote(hubAddress);
    if (!m_isHub) return fail("Set up your hub first");
    auto it = m_dir.hubs.find(hubAddress);
    if (it == m_dir.hubs.end()) return fail("That hub isn't in your directory yet");
    if (hubAddress == m_id.address) return fail("You can't grant your address to yourself");
    if (it->second.boxPub.size() != 64) return fail("That hub's card has no box key");
    json a = parseArg(addressJson);
    if (!a.is_object() || a.value("locker", "").empty() && a.value("street", "").empty())
        return fail("Give a handoff address: at least a parcel locker or a street address");
    try {
        std::string ref = hex(osRandom(16));
        Bytes sealed = sealJsonTo(unhex(it->second.boxPub), a, grantContext(hubAddress, m_id.address));
        author("hub.grant", json{{"to", hubAddress}, {"ref", ref}, {"sealed", b64url(sealed)}});
        publishState();
        return ok(json{{"ref", ref}});
    } catch (const std::exception& e) { return fail(std::string("Couldn't seal the address: ") + e.what()); }
}

std::string MulenetCoreImpl::revokeGrant(std::string ref) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    ref = unquote(ref);
    auto it = m_dir.grants.find(ref);
    if (it == m_dir.grants.end() || it->second.to != m_id.address) return fail("No such grant from this hub");
    author("hub.revoke", json{{"ref", ref}});
    publishState();
    return ok();
}

std::string MulenetCoreImpl::scanArrival(std::string scanJson) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    if (!m_isHub) return fail("Set up your hub first");
    json s = parseArg(scanJson);
    if (!s.is_object()) return fail("Scan details are not valid JSON");
    std::string label = s.value("label", ""), sleeve = s.value("sleeveCode", "");
    if (label.empty()) return fail("Paste or scan the label first");
    try {
        Arrival a = processArrival(m_hub, m_dir, label, sleeve, s.value("grossGrams", 0), s.value("boxGrams", m_hub.boxGrams), nowMs());
        json job = json{{"id", hex(osRandom(8))}, {"at", nowMs()}, {"action", a.action}, {"reason", a.reason}};
        if (a.haveRouting) {
            job["in"] = hex(a.routing.custodyIn);
            job["out"] = hex(a.routing.custodyOut);
            job["category"] = categoryName(a.routing.category);
            job["boxClass"] = boxClasses().count(a.routing.boxClass) ? boxClasses().at(a.routing.boxClass).name : "?";
            job["weightClass"] = a.routing.weightClass;
        }
        if (a.action == "relay" || a.action == "exit") {
            job["status"] = "to ship";
            job["shipDay"] = a.shipDay;
            job["removeSleeve"] = a.removeSleeve;
            job["address"] = a.address;
            job["paddingGrams"] = a.paddingGrams;
            job["nextLabel"] = a.nextLabel;
            job["nextHubName"] = a.action == "relay" ? hubName(a.nextHub) : "recipient's pickup point";
            if (!a.notify.empty()) job["notify"] = a.notify;
        } else job["status"] = "refused";
        // "received"/"refused" go out now (after their random delay); "shipped" waits for markShipped.
        for (const auto& r : a.receipts) if (r.kind == "received" || r.kind == "refused") queueReceipt(r);
        m_jobs.push_back(job);
        saveHub(); saveJobs(); saveReceipts();
        publishState();
        json shown = job;
        shown.erase("in"); shown.erase("out"); shown.erase("notify");
        return ok(json{{"job", shown}});
    } catch (const std::exception& e) { return fail(std::string("Couldn't process the parcel: ") + e.what()); }
}

std::string MulenetCoreImpl::markShipped(std::string jobId) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    jobId = unquote(jobId);
    for (auto& j : m_jobs) {
        if (j.value("id", "") != jobId) continue;
        if (j.value("status", "") != "to ship") return fail("This parcel isn't waiting to be shipped");
        long long now = nowMs();
        auto delay = [] { Bytes b = osRandom(1); return (long long)(2 + b[0] % 29) * 3600000LL; };
        bool exit = j.value("action", "") == "exit";
        ojson body = {{"kind", exit ? "delivered" : "shipped"}, {"day", dayOf(now)}};
        queueReceipt({makeReceipt(unhex(j["out"]), body), now + delay(), body["kind"]});
        if (exit && j.contains("notify")) {
            ojson ready = {{"kind", "ready"}, {"day", dayOf(now)}};
            queueReceipt({makeReceipt(unhex(j["notify"]), ready), now + delay(), "ready"});
        }
        j["status"] = "shipped";
        j["shippedAt"] = now;
        j.erase("nextLabel");    // the label is on the box now; don't keep it around
        j.erase("address");      // nor the next hop's private address
        saveJobs(); saveReceipts();
        publishState();
        return ok();
    }
    return fail("No such parcel");
}

std::string MulenetCoreImpl::dismissJob(std::string jobId) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    jobId = unquote(jobId);
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it)
        if (it->value("id", "") == jobId) { m_jobs.erase(it); saveJobs(); publishState(); return ok(); }
    return fail("No such parcel");
}

// ---- trust -------------------------------------------------------------------------------
std::string MulenetCoreImpl::addTrustRoot(std::string address) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    address = unquote(address);
    if (address.size() != 42 || address.rfind("0x", 0) != 0) return fail("A trust root is a 0x... address (42 characters)");
    m_roots.insert(address);
    saveSettings(); refold(); publishState();
    return ok();
}
std::string MulenetCoreImpl::removeTrustRoot(std::string address) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    m_roots.erase(unquote(address));
    saveSettings(); refold(); publishState();
    return ok();
}
std::string MulenetCoreImpl::vouch(std::string hubAddress) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    hubAddress = unquote(hubAddress);
    if (!m_dir.hubs.count(hubAddress)) return fail("That hub isn't in your directory");
    if (!m_dir.stewards.count(m_id.address)) return fail("Only a steward can vouch; add yourself as a trust root first");
    author("vouch", json{{"hub", hubAddress}});
    publishState();
    return ok();
}
std::string MulenetCoreImpl::unvouch(std::string hubAddress) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    hubAddress = unquote(hubAddress);
    author("unvouch", json{{"hub", hubAddress}});
    publishState();
    return ok();
}

// ---- receiving -----------------------------------------------------------------------------
std::string MulenetCoreImpl::createMailbox(std::string exitHub, std::string pickupJson) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    exitHub = unquote(exitHub);
    auto it = m_dir.hubs.find(exitHub);
    if (it == m_dir.hubs.end()) return fail("Pick an exit hub from the directory");
    json pickup = parseArg(pickupJson);
    if (!pickup.is_object() || pickup.value("locker", "").empty()) return fail("Give the pickup point (a parcel locker the exit hub can ship to)");
    try {
        std::string ref = hex(osRandom(16));
        Bytes notify = osRandom(16);
        json sealedBody = pickup;
        sealedBody["notify"] = hex(notify);
        Bytes sealed = sealJsonTo(unhex(it->second.boxPub), sealedBody, mailboxContext(exitHub, ref));
        // Signed by a ONE-TIME key: the mailbox must not be linkable to your identity.
        Identity oneTime = identityFrom(logos_sync::generatePrivateKey());
        Event e = makeEvent(oneTime, "mailbox.create", json{{"ref", ref}, {"exit", exitHub}, {"sealed", b64url(sealed)}}, nowMs());
        m_log.push_back(e);
        refold(); saveRegistry();
        sendFrame(REGISTRY_TOPIC, json{{"t", "ev"}, {"e", logos_sync::eventToJson(e)}});
        std::string card = "MNBOX1." + ref + "." + exitHub;
        m_mailboxes.push_back(json{{"ref", ref}, {"exit", exitHub}, {"notify", hex(notify)}, {"pickup", pickup}, {"card", card}, {"createdAt", nowMs()}});
        saveMailboxes();
        publishState();
        return ok(json{{"card", card}});
    } catch (const std::exception& e) { return fail(std::string("Couldn't create the mailbox: ") + e.what()); }
}

// ---- sending -------------------------------------------------------------------------------
std::string MulenetCoreImpl::planParcel(std::string parcelJson) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    json p = parseArg(parcelJson);
    if (!p.is_object()) return fail("Parcel details are not valid JSON");
    std::string box = unquote(p.value("mailbox", ""));
    std::string ref = box;
    if (box.rfind("MNBOX1.", 0) == 0) {
        size_t dot = box.find('.', 7);
        ref = box.substr(7, dot == std::string::npos ? std::string::npos : dot - 7);
    }
    if (ref.size() != 32) return fail("Paste the recipient's mailbox card (MNBOX1....)");
    Parcel parcel;
    parcel.category = p.value("category", 1);
    parcel.boxClass = p.value("boxClass", 1);
    parcel.coreGrams = p.value("coreGrams", 0);
    if (parcel.coreGrams <= 0) return fail("Enter the item's weight in grams, packed in its inner bag");
    parcel.weightClass = p.value("weightClass", 0);
    int hops = p.value("hops", 3);
    if (!parcel.weightClass) parcel.weightClass = weightClassFor(parcel.coreGrams + hops * DEFAULT_SLEEVE_G);
    if (!parcel.weightClass) return fail("Too heavy for MuleNet (5 kg maximum, packed)");
    std::string entry = unquote(p.value("entry", ""));
    try {
        Plan plan = planRoute(m_dir, parcel, entry, ref, (size_t)hops, p.value("holdMin", 1), p.value("holdMax", 4), nowMs());
        json custody = json::array();
        for (const auto& c : plan.custody) custody.push_back(hex(c));
        json hopsOut = json::array();
        for (size_t i = 0; i < plan.hops.size(); i++) {
            const auto& h = m_dir.hubs.at(plan.hops[i].hub);
            hopsOut.push_back(json{{"hub", plan.hops[i].hub}, {"name", h.name}, {"city", h.city}, {"sleeve", i + 1},
                                   {"sleeveCode", sleeveCodeText(plan.hops[i].routing.sleeveCode)}});
        }
        const auto& entryHub = m_dir.hubs.at(entry);
        json parcelOut = json{{"id", hex(osRandom(8))}, {"title", p.value("title", "Parcel")}, {"createdAt", nowMs()},
                              {"category", categoryName(parcel.category)}, {"boxClass", boxClasses().at(parcel.boxClass).name},
                              {"weightClass", parcel.weightClass}, {"path", plan.path}, {"hops", hopsOut},
                              {"label", labelToText(plan.label)}, {"custody", custody},
                              {"dropOff", entryHub.intake}, {"entryName", entryHub.name}};
        m_parcels.push_back(parcelOut);
        saveParcels();
        publishState();
        json shown = parcelOut;
        shown.erase("custody");
        return ok(json{{"parcel", shown}});
    } catch (const std::exception& e) { return fail(e.what()); }
}

std::string MulenetCoreImpl::forgetParcel(std::string parcelId) {
    std::lock_guard<std::recursive_mutex> lk(m_mtx);
    parcelId = unquote(parcelId);
    for (auto it = m_parcels.begin(); it != m_parcels.end(); ++it)
        if (it->value("id", "") == parcelId) { m_parcels.erase(it); saveParcels(); publishState(); return ok(); }
    return fail("No such parcel");
}

// ---- labels --------------------------------------------------------------------------------
std::string MulenetCoreImpl::labelQr(std::string text) {
    text = unquote(text);
    try {
        auto qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        int n = qr.getSize();
        std::string cells;
        cells.reserve((size_t)n * n);
        for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) cells += qr.getModule(x, y) ? '1' : '0';
        return ok(json{{"n", n}, {"cells", cells}});
    } catch (const std::exception& e) { return fail(std::string("Couldn't make the QR code: ") + e.what()); }
}

std::string MulenetCoreImpl::exportLabel(std::string text, std::string name) {
    text = unquote(text); name = unquote(name);
    try {
        auto qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        int n = qr.getSize(), border = 4, mm = 70;
        std::string safe;
        for (char c : name) safe += isalnum((unsigned char)c) || c == '-' || c == '_' ? c : '-';
        if (safe.empty()) safe = "label";
        std::string dir = (realHome().empty() ? m_dataDir : realHome() + "/MuleNet") + "/labels";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::string path = dir + "/" + safe + ".svg";
        std::ofstream f(path);
        // Nothing but the code: the label must not say where it came from or where it goes.
        f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << mm << "mm\" height=\"" << mm
          << "mm\" viewBox=\"0 0 " << n + 2 * border << " " << n + 2 * border << "\" shape-rendering=\"crispEdges\">\n"
          << "<rect width=\"100%\" height=\"100%\" fill=\"#fff\"/>\n<path fill=\"#000\" d=\"";
        for (int y = 0; y < n; y++) for (int x = 0; x < n; x++)
            if (qr.getModule(x, y)) f << "M" << x + border << "," << y + border << "h1v1h-1z";
        f << "\"/>\n</svg>\n";
        if (!f) return fail("Couldn't write " + path);
        return ok(json{{"path", path}});
    } catch (const std::exception& e) { return fail(std::string("Couldn't export the label: ") + e.what()); }
}
