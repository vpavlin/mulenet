#pragma once
// mulenet_label.hpp - the shipping label: a fixed-size, Sphinx-style onion header.
// C++ mirror of packages/core/src/label.mjs (read that file for the design notes);
// byte-identical, pinned by test/parity_test.cpp. docs/SPEC.md section 3.
#include "mulenet_crypto.hpp"

namespace mulenet {

constexpr int LABEL_VERSION = 1;
constexpr size_t MAX_HOPS = 5;
constexpr size_t ROUTING_SIZE = 80;
constexpr size_t KEY_SIZE = 32;
constexpr size_t MAC_SIZE = 16;
constexpr size_t SLOT_SIZE = KEY_SIZE + MAC_SIZE + ROUTING_SIZE;           // 128
constexpr size_t BETA_SIZE = MAX_HOPS * SLOT_SIZE;                         // 640
constexpr size_t HEADER_SIZE = 2 + 1 + KEY_SIZE + MAC_SIZE + BETA_SIZE;    // 691
inline const std::string LABEL_PREFIX = "MN1.";

constexpr int HOP_RELAY = 1;
constexpr int HOP_EXIT = 2;

struct Routing {
    int type = HOP_RELAY;
    int holdMin = 0, holdMax = 0;
    int boxClass = 0, weightClass = 0;
    int category = 0, netGrams = 0;
    Bytes sleeveCode = Bytes(8, 0);
    Bytes custodyIn = Bytes(16, 0);
    Bytes custodyOut = Bytes(16, 0);
    Bytes ref = Bytes(16, 0);
};

inline Bytes encodeRouting(const Routing& r) {
    auto need = [](const Bytes& b, size_t n, const char* name) {
        if (b.size() != n) throw std::runtime_error(std::string("routing: ") + name + " has the wrong size");
    };
    need(r.sleeveCode, 8, "sleeveCode"); need(r.custodyIn, 16, "custodyIn");
    need(r.custodyOut, 16, "custodyOut"); need(r.ref, 16, "ref");
    Bytes o(ROUTING_SIZE, 0);
    o[0] = (unsigned char)r.type;
    o[2] = (unsigned char)r.holdMin;
    o[3] = (unsigned char)r.holdMax;
    o[4] = (unsigned char)r.boxClass;
    o[5] = (unsigned char)r.weightClass;
    o[6] = (unsigned char)((r.category >> 8) & 255); o[7] = (unsigned char)(r.category & 255);
    o[8] = (unsigned char)((r.netGrams >> 8) & 255); o[9] = (unsigned char)(r.netGrams & 255);
    std::copy(r.sleeveCode.begin(), r.sleeveCode.end(), o.begin() + 10);
    std::copy(r.custodyIn.begin(), r.custodyIn.end(), o.begin() + 18);
    std::copy(r.custodyOut.begin(), r.custodyOut.end(), o.begin() + 34);
    std::copy(r.ref.begin(), r.ref.end(), o.begin() + 50);
    return o;
}

inline Routing decodeRouting(const Bytes& b) {
    if (b.size() != ROUTING_SIZE) throw std::runtime_error("routing: wrong size");
    Routing r;
    r.type = b[0];
    if (r.type != HOP_RELAY && r.type != HOP_EXIT) throw std::runtime_error("routing: unknown hop type");
    r.holdMin = b[2]; r.holdMax = b[3];
    r.boxClass = b[4]; r.weightClass = b[5];
    r.category = (b[6] << 8) | b[7];
    r.netGrams = (b[8] << 8) | b[9];
    r.sleeveCode = slice(b, 10, 18);
    r.custodyIn = slice(b, 18, 34);
    r.custodyOut = slice(b, 34, 50);
    r.ref = slice(b, 50, 66);
    return r;
}

namespace detail {
struct HopKeys { Bytes mac, stream; };
inline HopKeys hopKeys(const Bytes& shared, const Bytes& alpha) {
    Bytes okm = hkdf(shared, str("mulenet-label-v1"), alpha, 64);
    return { slice(okm, 0, 32), slice(okm, 32, 64) };
}
inline Bytes header3() { return Bytes{'M', 'N', (unsigned char)LABEL_VERSION}; }
inline Bytes mac(const Bytes& key, const Bytes& alpha, const Bytes& beta) {
    Bytes h = header3();
    Bytes m = hmac256(key, cat({&h, &alpha, &beta}));
    m.resize(MAC_SIZE);
    return m;
}
} // namespace detail

inline std::string labelToText(const Bytes& header) { return LABEL_PREFIX + b64url(header); }
inline Bytes labelFromText(const std::string& t0) {
    std::string t = t0;
    while (!t.empty() && (t.back() == ' ' || t.back() == '\n' || t.back() == '\r' || t.back() == '\t')) t.pop_back();
    size_t s = 0;
    while (s < t.size() && (t[s] == ' ' || t[s] == '\n' || t[s] == '\r' || t[s] == '\t')) s++;
    t = t.substr(s);
    if (t.rfind(LABEL_PREFIX, 0) != 0) throw std::runtime_error("not a MuleNet label");
    return unb64url(t.substr(LABEL_PREFIX.size()));
}

struct HopSpec { Bytes labelPub; Routing routing; };

// Build the label for a route (travel order; last hop must be the exit).
inline Bytes buildLabel(const std::vector<HopSpec>& hops, const Rng& rng = defaultRng()) {
    using namespace detail;
    const size_t n = hops.size();
    if (n < 1 || n > MAX_HOPS) throw std::runtime_error("route must have 1..5 hops");
    for (size_t i = 0; i < n; i++) {
        int want = i == n - 1 ? HOP_EXIT : HOP_RELAY;
        if (hops[i].routing.type != want) throw std::runtime_error(want == HOP_EXIT ? "last hop must be the exit" : "only the last hop may be the exit");
    }
    std::vector<Bytes> eph, alphas;
    std::vector<HopKeys> keys;
    std::vector<Bytes> streams;
    for (size_t i = 0; i < n; i++) eph.push_back(rng(32));
    for (size_t i = 0; i < n; i++) alphas.push_back(x25519Public(eph[i]));
    for (size_t i = 0; i < n; i++) keys.push_back(hopKeys(x25519Shared(eph[i], hops[i].labelPub), alphas[i]));
    for (size_t i = 0; i < n; i++) streams.push_back(chachaStream(keys[i].stream, BETA_SIZE + SLOT_SIZE));

    Bytes filler;
    for (size_t i = 1; i < n; i++) {
        Bytes z(SLOT_SIZE, 0);
        Bytes ext = cat({&filler, &z});
        filler = xorBytes(ext, slice(streams[i - 1], BETA_SIZE + SLOT_SIZE - i * SLOT_SIZE, BETA_SIZE + SLOT_SIZE));
    }

    Bytes zk(KEY_SIZE, 0), zm(MAC_SIZE, 0);
    Bytes exitR = encodeRouting(hops[n - 1].routing);
    Bytes exitSlot = cat({&zk, &zm, &exitR});
    Bytes pad = rng((MAX_HOPS - n) * SLOT_SIZE);
    size_t headLen = (MAX_HOPS - n + 1) * SLOT_SIZE;
    Bytes head = xorBytes(cat({&exitSlot, &pad}), slice(streams[n - 1], 0, headLen));
    Bytes beta = cat({&head, &filler});
    Bytes gamma = mac(keys[n - 1].mac, alphas[n - 1], beta);

    for (size_t k = n - 1; k-- > 0;) {
        Bytes r = encodeRouting(hops[k].routing);
        Bytes rest = slice(beta, 0, BETA_SIZE - SLOT_SIZE);
        Bytes plain = cat({&alphas[k + 1], &gamma, &r, &rest});
        beta = xorBytes(plain, slice(streams[k], 0, BETA_SIZE));
        gamma = mac(keys[k].mac, alphas[k], beta);
    }
    Bytes h = header3();
    return cat({&h, &alphas[0], &gamma, &beta});
}

struct Peeled {
    Routing routing;
    Bytes replayTag;
    bool hasNext = false;
    Bytes next;   // next header (relay only)
};

// Peel this hop's layer. Throws on a malformed label or a MAC failure.
inline Peeled peelLabel(const Bytes& h, const Bytes& labelPriv) {
    using namespace detail;
    if (h.size() != HEADER_SIZE) throw std::runtime_error("label has the wrong size");
    if (h[0] != 'M' || h[1] != 'N' || h[2] != LABEL_VERSION) throw std::runtime_error("not a MuleNet v1 label");
    Bytes alpha = slice(h, 3, 3 + KEY_SIZE);
    Bytes gamma = slice(h, 3 + KEY_SIZE, 3 + KEY_SIZE + MAC_SIZE);
    Bytes beta = slice(h, 3 + KEY_SIZE + MAC_SIZE, HEADER_SIZE);
    Bytes shared = x25519Shared(labelPriv, alpha);
    HopKeys k = hopKeys(shared, alpha);
    if (!equalCt(mac(k.mac, alpha, beta), gamma)) throw std::runtime_error("label MAC mismatch (not for this hub, or tampered)");
    Bytes z(SLOT_SIZE, 0);
    Bytes b = xorBytes(cat({&beta, &z}), chachaStream(k.stream, BETA_SIZE + SLOT_SIZE));
    Peeled p;
    p.routing = decodeRouting(slice(b, KEY_SIZE + MAC_SIZE, SLOT_SIZE));
    Bytes tagIn = str("mulenet-replay-v1");
    p.replayTag = sha256(cat({&tagIn, &shared}));
    if (p.routing.type == HOP_EXIT) return p;
    Bytes nextAlpha = slice(b, 0, KEY_SIZE);
    Bytes nextGamma = slice(b, KEY_SIZE, KEY_SIZE + MAC_SIZE);
    Bytes nextBeta = slice(b, SLOT_SIZE, b.size());
    Bytes h3 = header3();
    p.hasNext = true;
    p.next = cat({&h3, &nextAlpha, &nextGamma, &nextBeta});
    return p;
}

// Try every live label key (rotation keeps old epochs for a grace period).
inline Peeled peelWithAnyKey(const Bytes& h, const std::vector<Bytes>& privs) {
    std::string last = "no label keys";
    for (const auto& k : privs) {
        try { return peelLabel(h, k); } catch (const std::exception& e) { last = e.what(); }
    }
    throw std::runtime_error(last);
}

} // namespace mulenet
