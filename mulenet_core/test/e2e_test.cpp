// End-to-end: five real MulenetCoreImpl instances (a steward/sender, three hubs, a
// recipient) over the fake loam_core bus. A parcel goes Prague -> Berlin -> Madrid with
// receipts flowing back, then a late joiner catches up the directory by RBSR.
//   mulenet_core/test/run-tests.sh
#include "mulenet_core_impl.h"
#include "logos_sdk.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <iostream>
#include <filesystem>

using json = nlohmann::json;
// The builder generates event emitters; the fake SDK needs a body.
void MulenetCoreImpl::stateChanged(const std::string&) {}
static int fails = 0, passes = 0;
#define CHECK(cond, what) do { if (cond) passes++; else { fails++; std::cerr << "FAIL: " << what << "\n"; } } while (0)

struct Peer {
    std::string name;
    FakeLoamNode node;
    MulenetCoreImpl core;
    json snap() { return json::parse(core.snapshot()); }
};

static void pump(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); }
}
static json call(const std::string& r) { json j = json::parse(r); if (!j.value("ok", false)) std::cerr << "  call failed: " << r << "\n"; return j; }

static Peer* spawn(const std::string& name, const std::string& root) {
    std::string dir = root + "/" + name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    setenv("MULENET_CORE_DATA", dir.c_str(), 1);
    Peer* p = new Peer();
    p->name = name;
    p->node.name = name;
    p->core.modules().loam_core.node = &p->node;
    FakeLoamBus::get().nodes.push_back(&p->node);
    p->core.fakeStart();
    return p;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    setenv("MULENET_NO_RECEIPT_DELAY", "1", 1);
    setenv("MULENET_TICK_MS", "100", 1);
    std::string root = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") + "/mulenet-e2e";

    Peer* alice = spawn("alice", root);   // steward + sender
    Peer* prg = spawn("prague", root);
    Peer* ber = spawn("berlin", root);
    Peer* mad = spawn("madrid", root);
    Peer* bob = spawn("bob", root);       // recipient
    pump(300);
    CHECK(alice->snap()["status"] == "Connected", "transport connected");

    std::string steward = alice->snap()["me"]["address"];
    for (Peer* p : {prg, ber, mad, bob}) call(p->core.addTrustRoot(steward));
    auto card = [](const char* n, const char* c, const char* cc) {
        return json{{"name", n}, {"city", c}, {"country", cc},
                    {"policy", {{"accepts", json::array({"*"})}, {"maxWeightClass", 3}, {"batchDays", json::array()}, {"holdMaxDays", 0}}},
                    {"intake", {{"kind", "meetup"}, {"text", std::string(c) + " circle meetup"}}}}.dump();
    };
    call(prg->core.setupHub(card("Prague mule", "Prague", "CZ")));
    call(ber->core.setupHub(card("Berlin mule", "Berlin", "DE")));
    call(mad->core.setupHub(card("Madrid mule", "Madrid", "ES")));
    pump(500);
    json dir = alice->snap()["directory"]["hubs"];
    CHECK(dir.size() == 3, "steward sees three hubs (" << dir.size() << ")");
    for (const auto& h : dir) call(alice->core.vouch(h["address"]));
    std::map<std::string, std::string> addr;
    for (Peer* p : {prg, ber, mad}) addr[p->name] = p->snap()["me"]["address"];
    pump(500);

    // Address grants: every hub lets every other hub ship to it.
    for (Peer* to : {prg, ber, mad}) for (Peer* from : {prg, ber, mad}) if (to != from)
        call(to->core.grantAddress(addr[from->name], json{{"carrier", "locker"}, {"locker", to->name + "-locker"}}.dump()));
    pump(500);
    CHECK(alice->snap()["directory"]["grants"] == 6, "six grants synced to the sender");

    // Bob's mailbox at Madrid.
    json mb = call(bob->core.createMailbox(addr["madrid"], json{{"carrier", "locker"}, {"locker", "MAD-PICKUP-1"}, {"name", "Bob"}}.dump()));
    std::string boxCard = mb.value("card", "");
    pump(500);

    // Alice plans a parcel Prague -> ? -> Madrid. With three hubs the middle is Berlin.
    json pl = call(alice->core.planParcel(json{{"title", "mule figurine"}, {"category", 1}, {"boxClass", 1}, {"coreGrams", 180},
                                               {"entry", addr["prague"]}, {"mailbox", boxCard}, {"hops", 3}, {"holdMin", 0}, {"holdMax", 0}}.dump()));
    CHECK(pl.value("ok", false), "parcel planned");
    json parcel = pl["parcel"];
    CHECK(parcel["path"].size() == 3 && parcel["path"][1] == addr["berlin"], "route goes through Berlin");
    CHECK(!pl.dump().empty() && pl.dump().find("locker") == std::string::npos, "the sender's plan holds no hub or pickup address");

    std::string label = parcel["label"];
    std::vector<Peer*> hops = {prg, ber, mad};
    for (size_t i = 0; i < 3; i++) {
        json r = call(hops[i]->core.scanArrival(json{{"label", label}, {"sleeveCode", parcel["hops"][i]["sleeveCode"]}, {"grossGrams", 430}, {"boxGrams", 120}}.dump()));
        json job = r["job"];
        CHECK(job["status"] == "to ship", hops[i]->name << " accepts the parcel (" << job.value("reason", "") << ")");
        if (i < 2) {
            CHECK(job["address"]["locker"] == hops[i + 1]->name + "-locker", hops[i]->name << " sees exactly the next hub's locker");
            label = job["nextLabel"];
        } else {
            CHECK(job["address"]["locker"] == "MAD-PICKUP-1", "exit opens Bob's pickup point");
            CHECK(!job["address"].contains("notify"), "exit never shows the notify token");
        }
        call(hops[i]->core.markShipped(job["id"]));
        pump(400);
    }

    json sent = alice->snap()["parcels"][0]["steps"];
    std::vector<std::string> kinds;
    for (const auto& s : sent) { std::string k; for (const auto& r : s["receipts"]) k += r["kind"].get<std::string>() + ","; kinds.push_back(k); }
    CHECK(sent.size() == 4, "four custody steps");
    CHECK(kinds.size() == 4 && kinds[0].find("received") != std::string::npos && kinds[3].find("delivered") != std::string::npos,
          "sender sees received ... delivered");
    json notices = bob->snap()["mailboxes"][0]["notices"];
    CHECK(notices.size() == 1 && notices[0]["kind"] == "ready", "Bob gets a 'ready' notice");
    json pjobs = prg->snap()["jobs"];
    CHECK(pjobs.size() == 1 && pjobs[0]["alibi"].size() == 1 && pjobs[0]["alibi"][0]["kind"] == "received", "Prague holds Berlin's 'received' as its alibi");
    CHECK(!pjobs[0].contains("address") && !pjobs[0].contains("nextLabel"), "shipped jobs forget the next address + label");

    // Tamper + replay at the hub.
    json again = call(prg->core.scanArrival(json{{"label", parcel["label"]}, {"sleeveCode", parcel["hops"][0]["sleeveCode"]}}.dump()));
    CHECK(again["job"]["status"] == "refused", "replayed label refused");

    // Late joiner catches up the whole directory by RBSR.
    Peer* carol = spawn("carol", root);
    call(carol->core.addTrustRoot(steward));
    pump(400);
    call(carol->core.resync());
    pump(1500);
    json cd = carol->snap();
    CHECK(cd["directory"]["hubs"].size() == 3 && cd["directory"]["grants"] == 6, "late joiner catches up the directory ("
          << cd["directory"]["hubs"].size() << " hubs, " << cd["directory"]["grants"] << " grants)");

    // Restart: a new instance on Prague's data dir is the same hub with the same jobs.
    std::string pragueAddr = addr["prague"];
    setenv("MULENET_CORE_DATA", (root + "/prague").c_str(), 1);
    MulenetCoreImpl reloaded;
    FakeLoamNode rn;
    reloaded.modules().loam_core.node = &rn;
    reloaded.fakeStart();
    json rs = json::parse(reloaded.snapshot());
    CHECK(rs["me"]["address"] == pragueAddr && !rs["hub"].is_null() && rs["jobs"].size() == 2, "hub identity, keys and jobs survive a restart");

    std::cout << passes << " passed, " << fails << " failed\n";
    return fails ? 1 : 0;
}
