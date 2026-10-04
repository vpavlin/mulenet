// Byte-parity between the C++ core and the JS reference (packages/core).
// Vectors: node packages/core/test/gen-vectors.mjs > mulenet_core/test/vectors.json
// Run:     mulenet_core/test/run-tests.sh
#include "mulenet_label.hpp"
#include "mulenet_seal.hpp"
#include "mulenet_physical.hpp"
#include "logos_sync/signing.hpp"
#include <fstream>
#include <iostream>

using namespace mulenet;
static int fails = 0, passes = 0;
#define CHECK(cond, what) do { if (cond) passes++; else { fails++; std::cerr << "FAIL: " << what << "\n"; } } while (0)

// The same sha256 counter stream the JS generator uses.
static Rng stream(const std::string& seed) {
    auto ctr = std::make_shared<long>(0);
    return [seed, ctr](size_t n) {
        Bytes out;
        while (out.size() < n) {
            Bytes h = sha256(str(seed + "|" + std::to_string((*ctr)++)));
            out.insert(out.end(), h.begin(), h.begin() + (long)std::min<size_t>(32, n - out.size()));
        }
        return out;
    };
}

static Routing routingFrom(const json& j) {
    Routing r;
    r.type = j["type"]; r.holdMin = j["holdMin"]; r.holdMax = j["holdMax"];
    r.boxClass = j["boxClass"]; r.weightClass = j["weightClass"];
    r.category = j["category"]; r.netGrams = j["netGrams"];
    r.sleeveCode = unhex(j["sleeveCode"]); r.custodyIn = unhex(j["custodyIn"]);
    r.custodyOut = unhex(j["custodyOut"]); r.ref = unhex(j["ref"]);
    return r;
}

int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : "vectors.json";
    std::ifstream f(path);
    if (!f) { std::cerr << "can't open " << path << "\n"; return 2; }
    json v = json::parse(f);

    for (const auto& L : v["labels"]) {
        std::vector<HopSpec> hops;
        for (const auto& h : L["hops"]) hops.push_back({unhex(h["labelPub"]), routingFrom(h["routing"])});
        size_t n = hops.size();
        Bytes built = buildLabel(hops, stream(L["seed"]));
        CHECK(hex(built) == L["header"].get<std::string>(), "label build bytes, " << n << " hops");
        Bytes h = unhex(L["header"]);
        for (size_t i = 0; i < n; i++) {
            Peeled p = peelLabel(h, unhex(L["privs"][i]));
            const json& want = L["peels"][i];
            CHECK(hex(p.replayTag) == want["replayTag"].get<std::string>(), "replay tag hop " << i);
            CHECK(hex(encodeRouting(p.routing)) == hex(encodeRouting(routingFrom(L["hops"][i]["routing"]))), "routing hop " << i);
            if (want["next"].is_null()) CHECK(!p.hasNext, "exit has no next");
            else { CHECK(p.hasNext && hex(p.next) == want["next"].get<std::string>(), "next header hop " << i); h = p.next; }
        }
        bool threw = false;
        try { peelLabel(unhex(L["header"]), unhex(L["privs"][n - 1])); } catch (...) { threw = true; }
        CHECK(n == 1 || threw, "wrong key rejected");
    }

    const json& S = v["seal"];
    Bytes blob = sealTo(unhex(S["recipientPub"]), str(S["plaintext"]), S["context"], stream(S["seed"]));
    CHECK(hex(blob) == S["blob"].get<std::string>(), "seal bytes");
    Bytes opened;
    CHECK(openSealed(unhex(S["recipientPriv"]), unhex(S["blob"]), S["context"], opened) && text(opened) == S["plaintext"].get<std::string>(), "open JS-sealed blob");
    CHECK(!openSealed(unhex(S["recipientPriv"]), unhex(S["blob"]), "other-context", opened), "context is bound");

    const json& R = v["receipt"];
    nlohmann::ordered_json rbody = {{"kind", R["body"]["kind"]}, {"day", R["body"]["day"]}};   // JS key order
    Bytes rb = makeReceipt(unhex(R["token"]), rbody, stream(R["seed"]));
    CHECK(hex(rb) == R["bytes"].get<std::string>(), "receipt bytes");
    json body;
    CHECK(openReceipt(unhex(R["token"]), unhex(R["bytes"]), body) && body["kind"] == "shipped", "open JS receipt");

    const json& SL = v["sleeve"];
    CHECK(sleeveCodeText(unhex(SL["bytes"])) == SL["text"].get<std::string>(), "sleeve code text");
    Bytes back;
    CHECK(sleeveCodeFromText(SL["text"], back) && hex(back) == SL["bytes"].get<std::string>(), "sleeve code parse");

    logos_sync::Event e = logos_sync::eventFromJson(v["event"]);
    CHECK(logos_sync::verifyEvent("mulenet", e), "verify JS-signed registry event");
    e.payload["city"] = "Nowhere";
    CHECK(!logos_sync::verifyEvent("mulenet", e), "tampered event rejected");

    std::cout << passes << " passed, " << fails << " failed\n";
    return fails ? 1 : 0;
}
