#pragma once
// mulenet_seal.hpp - anonymous public-key sealing (address grants, mailboxes) and
// custody receipts. C++ mirror of packages/core/src/seal.mjs + receipts.mjs.
#include "mulenet_crypto.hpp"
#include <nlohmann/json.hpp>
#include <ctime>

namespace mulenet {
using json = nlohmann::json;

// ---- sealing to one hub's box key --------------------------------------------------
inline Bytes sealKey(const Bytes& shared, const Bytes& ephPub, const Bytes& recipientPub) {
    return hkdf(shared, str("mulenet-seal-v1"), cat({&ephPub, &recipientPub}), 32);
}
inline Bytes sealTo(const Bytes& recipientPub, const Bytes& plaintext, const std::string& context, const Rng& rng = defaultRng()) {
    Bytes eph = rng(32);
    Bytes ephPub = x25519Public(eph);
    Bytes k = sealKey(x25519Shared(eph, recipientPub), ephPub, recipientPub);
    Bytes ct = aeadSeal(k, Bytes(12, 0), str(context), plaintext);
    return cat({&ephPub, &ct});
}
inline Bytes sealJsonTo(const Bytes& recipientPub, const json& value, const std::string& context, const Rng& rng = defaultRng()) {
    return sealTo(recipientPub, str(value.dump()), context, rng);
}
// Returns false if the blob isn't for this key / context.
inline bool openSealed(const Bytes& recipientPriv, const Bytes& blob, const std::string& context, Bytes& out) {
    if (blob.size() < 32 + 16) return false;
    try {
        Bytes ephPub = slice(blob, 0, 32);
        Bytes recipientPub = x25519Public(recipientPriv);
        Bytes k = sealKey(x25519Shared(recipientPriv, ephPub), ephPub, recipientPub);
        return aeadOpen(k, Bytes(12, 0), str(context), slice(blob, 32, blob.size()), out);
    } catch (...) { return false; }
}
inline bool openSealedJson(const Bytes& recipientPriv, const Bytes& blob, const std::string& context, json& out) {
    Bytes pt;
    if (!openSealed(recipientPriv, blob, context, pt)) return false;
    out = json::parse(text(pt), nullptr, false);
    return !out.is_discarded();
}

inline std::string grantContext(const std::string& from, const std::string& to) { return "mulenet-grant-v1|" + from + "|" + to; }
inline std::string mailboxContext(const std::string& exit, const std::string& ref) { return "mulenet-mailbox-v1|" + exit + "|" + ref; }

// ---- custody receipts ----------------------------------------------------------------
inline const std::string RECEIPTS_TOPIC = "/mulenet/1/receipts/proto";

inline Bytes custodyTag(const Bytes& token) {
    Bytes p = str("mulenet-custody-tag-v1");
    Bytes h = sha256(cat({&p, &token}));
    h.resize(16);
    return h;
}
inline Bytes custodyKey(const Bytes& token) { return hkdf(token, str("mulenet-custody-key-v1"), Bytes(), 32); }

inline bool validReceiptKind(const std::string& k) {
    for (const char* x : {"received", "shipped", "refused", "delivered", "ready", "collected"}) if (k == x) return true;
    return false;
}

// body must contain "kind" (and usually "day"). The JSON key order matches the JS
// reference ({v, id, ...body}) so a receipt reads the same on both sides.
inline Bytes makeReceipt(const Bytes& token, const nlohmann::ordered_json& body, const Rng& rng = defaultRng()) {
    if (!body.contains("kind") || !validReceiptKind(body.value("kind", ""))) throw std::runtime_error("receipt: unknown kind");
    Bytes tag = custodyTag(token);
    Bytes nonce = rng(12);
    nlohmann::ordered_json o;
    o["v"] = 1;
    o["id"] = hex(rng(8));
    for (auto it = body.begin(); it != body.end(); ++it) o[it.key()] = it.value();
    Bytes ct = aeadSeal(custodyKey(token), nonce, tag, str(o.dump()));
    return cat({&tag, &nonce, &ct});
}
inline bool receiptMatches(const Bytes& token, const Bytes& bytes) {
    return bytes.size() > 28 && equalCt(slice(bytes, 0, 16), custodyTag(token));
}
inline bool openReceipt(const Bytes& token, const Bytes& bytes, json& out) {
    if (!receiptMatches(token, bytes)) return false;
    Bytes pt;
    if (!aeadOpen(custodyKey(token), slice(bytes, 16, 28), slice(bytes, 0, 16), slice(bytes, 28, bytes.size()), pt)) return false;
    out = json::parse(text(pt), nullptr, false);
    return !out.is_discarded();
}

// UTC day of a millisecond timestamp, YYYY-MM-DD - the only time resolution receipts carry.
inline std::string dayOf(long long ms) {
    time_t t = (time_t)(ms / 1000);
    struct tm g;
    gmtime_r(&t, &g);
    char buf[16];
    strftime(buf, sizeof buf, "%Y-%m-%d", &g);
    return buf;
}
inline long long dayStartMs(const std::string& day) {
    struct tm g = {};
    if (sscanf(day.c_str(), "%d-%d-%d", &g.tm_year, &g.tm_mon, &g.tm_mday) != 3) return 0;
    g.tm_year -= 1900; g.tm_mon -= 1;
    return (long long)timegm(&g) * 1000;
}

} // namespace mulenet
