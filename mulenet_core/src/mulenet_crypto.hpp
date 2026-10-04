#pragma once
// mulenet_crypto.hpp - the primitives MuleNet needs, over OpenSSL 3. Byte-identical to
// the JS reference (packages/core, @noble): X25519, HKDF-SHA256, HMAC-SHA256, the
// ChaCha20 stream (IETF, counter 0), ChaCha20-Poly1305 (ct || tag), SHA-256 and
// unpadded base64url. Parity is pinned by test/parity_test.cpp against vectors the JS
// side generates (packages/core/test/gen-vectors.mjs).
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <cstdint>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include "logos_sync/crypto.hpp"

namespace mulenet {

using Bytes = std::vector<unsigned char>;
// Injectable randomness: the CSPRNG in production, a fixed stream in parity tests.
using Rng = std::function<Bytes(size_t)>;

inline Bytes osRandom(size_t n) {
    Bytes b(n);
    if (n && RAND_bytes(b.data(), (int)n) != 1) throw std::runtime_error("RAND_bytes failed");
    return b;
}
inline Rng defaultRng() { return osRandom; }

inline Bytes str(const std::string& s) { return Bytes(s.begin(), s.end()); }
inline std::string text(const Bytes& b) { return std::string(b.begin(), b.end()); }

inline Bytes cat(std::initializer_list<const Bytes*> parts) {
    Bytes out;
    for (const Bytes* p : parts) out.insert(out.end(), p->begin(), p->end());
    return out;
}
inline Bytes slice(const Bytes& b, size_t from, size_t to) {
    if (from > to || to > b.size()) throw std::runtime_error("slice out of range");
    return Bytes(b.begin() + (long)from, b.begin() + (long)to);
}
inline Bytes xorBytes(const Bytes& a, const Bytes& b) {
    if (a.size() != b.size()) throw std::runtime_error("xor: length mismatch");
    Bytes o(a.size());
    for (size_t i = 0; i < a.size(); i++) o[i] = a[i] ^ b[i];
    return o;
}
inline bool equalCt(const Bytes& a, const Bytes& b) {
    if (a.size() != b.size()) return false;
    unsigned char d = 0;
    for (size_t i = 0; i < a.size(); i++) d |= a[i] ^ b[i];
    return d == 0;
}

inline std::string hex(const Bytes& b) { return logos_sync::crypto::hexs(b); }
inline Bytes unhex(const std::string& s) {
    if (s.size() % 2) throw std::runtime_error("hex: odd length");
    Bytes o(s.size() / 2);
    for (size_t i = 0; i < o.size(); i++) {
        auto v = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            throw std::runtime_error("hex: bad digit");
        };
        o[i] = (unsigned char)(v(s[2 * i]) * 16 + v(s[2 * i + 1]));
    }
    return o;
}

inline const std::string& b64alphabet() {
    static const std::string A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    return A;
}
inline std::string b64url(const Bytes& b) {
    const std::string& A = b64alphabet();
    std::string s;
    size_t i = 0;
    for (; i + 2 < b.size(); i += 3) {
        uint32_t n = (b[i] << 16) | (b[i + 1] << 8) | b[i + 2];
        s += A[n >> 18]; s += A[(n >> 12) & 63]; s += A[(n >> 6) & 63]; s += A[n & 63];
    }
    size_t rest = b.size() - i;
    if (rest == 1) { uint32_t n = b[i] << 16; s += A[n >> 18]; s += A[(n >> 12) & 63]; }
    else if (rest == 2) { uint32_t n = (b[i] << 16) | (b[i + 1] << 8); s += A[n >> 18]; s += A[(n >> 12) & 63]; s += A[(n >> 6) & 63]; }
    return s;
}
inline Bytes unb64url(const std::string& s) {
    const std::string& A = b64alphabet();
    Bytes out;
    uint32_t buf = 0; int bits = 0;
    for (char c : s) {
        size_t v = A.find(c);
        if (v == std::string::npos) throw std::runtime_error("base64url: bad character");
        buf = (buf << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((unsigned char)((buf >> bits) & 255)); }
    }
    return out;
}

inline Bytes sha256(const Bytes& in) {
    Bytes o(32);
    SHA256(in.data(), in.size(), o.data());
    return o;
}
inline Bytes hkdf(const Bytes& ikm, const Bytes& salt, const Bytes& info, size_t len) {
    return logos_sync::crypto::hkdf(ikm, salt, info, len);
}
inline Bytes hmac256(const Bytes& key, const Bytes& data) { return logos_sync::crypto::hmac256(key, data); }

// ---- X25519 ----------------------------------------------------------------------
inline Bytes x25519Public(const Bytes& priv) {
    EVP_PKEY* k = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, priv.data(), priv.size());
    if (!k) throw std::runtime_error("x25519: bad private key");
    Bytes pub(32); size_t n = 32;
    bool ok = EVP_PKEY_get_raw_public_key(k, pub.data(), &n) == 1 && n == 32;
    EVP_PKEY_free(k);
    if (!ok) throw std::runtime_error("x25519: public key");
    return pub;
}
inline Bytes x25519Shared(const Bytes& priv, const Bytes& peerPub) {
    if (peerPub.size() != 32) throw std::runtime_error("x25519: bad public key");
    EVP_PKEY* k = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, priv.data(), priv.size());
    EVP_PKEY* p = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peerPub.data(), peerPub.size());
    EVP_PKEY_CTX* c = k ? EVP_PKEY_CTX_new(k, nullptr) : nullptr;
    Bytes out(32); size_t n = 32;
    bool ok = c && p && EVP_PKEY_derive_init(c) == 1 && EVP_PKEY_derive_set_peer(c, p) == 1 &&
              EVP_PKEY_derive(c, out.data(), &n) == 1 && n == 32;
    if (c) EVP_PKEY_CTX_free(c);
    if (p) EVP_PKEY_free(p);
    if (k) EVP_PKEY_free(k);
    if (!ok) throw std::runtime_error("x25519: derive failed");
    return out;
}
struct KeyPair { Bytes priv, pub; };
inline KeyPair newX25519(const Rng& rng = defaultRng()) {
    KeyPair kp; kp.priv = rng(32); kp.pub = x25519Public(kp.priv);
    return kp;
}

// ---- ChaCha20 stream (key used once, nonce zero) -----------------------------------
inline Bytes chachaStream(const Bytes& key, size_t len, const Bytes& nonce12 = Bytes(12, 0)) {
    unsigned char iv[16] = {0};   // OpenSSL IV = 32-bit LE block counter (0) || 96-bit nonce
    for (int i = 0; i < 12; i++) iv[4 + i] = nonce12[i];
    Bytes zeros(len, 0), out(len + 16);
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    int n = 0, m = 0;
    bool ok = c && EVP_EncryptInit_ex(c, EVP_chacha20(), nullptr, key.data(), iv) == 1 &&
              EVP_EncryptUpdate(c, out.data(), &n, zeros.data(), (int)len) == 1 &&
              EVP_EncryptFinal_ex(c, out.data() + n, &m) == 1;
    if (c) EVP_CIPHER_CTX_free(c);
    if (!ok) throw std::runtime_error("chacha20 failed");
    out.resize(len);
    return out;
}

// ---- ChaCha20-Poly1305 (output = ct || tag16, like @noble) -------------------------
inline Bytes aeadSeal(const Bytes& key, const Bytes& nonce12, const Bytes& aad, const Bytes& pt) {
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    Bytes out(pt.size() + 16);
    int n = 0, m = 0;
    bool ok = c && EVP_EncryptInit_ex(c, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr) == 1 &&
              EVP_EncryptInit_ex(c, nullptr, nullptr, key.data(), nonce12.data()) == 1 &&
              (aad.empty() || EVP_EncryptUpdate(c, nullptr, &n, aad.data(), (int)aad.size()) == 1) &&
              EVP_EncryptUpdate(c, out.data(), &n, pt.data(), (int)pt.size()) == 1 &&
              EVP_EncryptFinal_ex(c, out.data() + n, &m) == 1 &&
              EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_GET_TAG, 16, out.data() + pt.size()) == 1;
    if (c) EVP_CIPHER_CTX_free(c);
    if (!ok) throw std::runtime_error("aead seal failed");
    return out;
}
// Returns false on authentication failure.
inline bool aeadOpen(const Bytes& key, const Bytes& nonce12, const Bytes& aad, const Bytes& ctTag, Bytes& pt) {
    if (ctTag.size() < 16) return false;
    size_t len = ctTag.size() - 16;
    Bytes tag(ctTag.end() - 16, ctTag.end());
    pt.assign(len + 16, 0);
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    int n = 0, m = 0;
    bool ok = c && EVP_DecryptInit_ex(c, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr) == 1 &&
              EVP_DecryptInit_ex(c, nullptr, nullptr, key.data(), nonce12.data()) == 1 &&
              (aad.empty() || EVP_DecryptUpdate(c, nullptr, &n, aad.data(), (int)aad.size()) == 1) &&
              EVP_DecryptUpdate(c, pt.data(), &n, ctTag.data(), (int)len) == 1 &&
              EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_SET_TAG, 16, tag.data()) == 1 &&
              EVP_DecryptFinal_ex(c, pt.data() + n, &m) == 1;
    if (c) EVP_CIPHER_CTX_free(c);
    if (!ok) { pt.clear(); return false; }
    pt.resize(len);
    return true;
}

} // namespace mulenet
