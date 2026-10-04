#pragma once
// mulenet_physical.hpp - box classes, weight bands, content categories, sleeve seal
// codes. C++ mirror of packages/core/src/physical.mjs (docs/SPEC.md sections 5-6).
#include "mulenet_crypto.hpp"
#include <map>
#include <cctype>

namespace mulenet {

struct BoxClass { const char* name; const char* dims; };
inline const std::map<int, BoxClass>& boxClasses() {
    static const std::map<int, BoxClass> m = {
        {1, {"S", "25x18x8 cm"}}, {2, {"M", "35x25x15 cm"}}, {3, {"L", "50x35x20 cm"}}};
    return m;
}
struct WeightClass { int max, target; };
inline const std::map<int, WeightClass>& weightClasses() {
    static const std::map<int, WeightClass> m = {{1, {500, 450}}, {2, {1000, 900}}, {3, {2000, 1800}}, {4, {5000, 4500}}};
    return m;
}
constexpr int WEIGHT_TOLERANCE_G = 25;
constexpr int DEFAULT_SLEEVE_G = 20;

inline const std::map<int, std::string>& categories() {
    static const std::map<int, std::string> m = {
        {1, "printed-object"}, {2, "paper"}, {3, "textiles"}, {4, "electronics-no-battery"},
        {5, "electronics-with-battery"}, {6, "hardware-tools"}, {7, "sealed-food"}, {99, "undeclared"}};
    return m;
}
inline std::string categoryName(int id) {
    auto it = categories().find(id);
    return it == categories().end() ? std::to_string(id) : it->second;
}

inline int weightClassFor(int grams, int packagingG = 150) {
    for (const auto& [k, w] : weightClasses()) if (grams + packagingG <= w.target) return k;
    return 0;   // too heavy
}
inline int paddingFor(int weightClass, int contentsG, int boxG) {
    auto it = weightClasses().find(weightClass);
    if (it == weightClasses().end()) throw std::runtime_error("unknown weight class");
    int pad = it->second.target - contentsG - boxG;
    if (pad < -WEIGHT_TOLERANCE_G) throw std::runtime_error("contents exceed their weight class");
    return pad < 0 ? 0 : pad;
}

// Sleeve seal codes: 8 bytes as Crockford base32, 13 chars printed 4-4-5.
inline std::string sleeveCodeText(const Bytes& b) {
    static const char* C = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    unsigned __int128 bits = 0;
    for (unsigned char x : b) bits = (bits << 8) | x;
    std::string s(13, '0');
    for (int i = 12; i >= 0; i--) { s[i] = C[(int)(bits & 31)]; bits >>= 5; }
    return s.substr(0, 4) + "-" + s.substr(4, 4) + "-" + s.substr(8);
}
inline bool sleeveCodeFromText(const std::string& in, Bytes& out) {
    static const std::string C = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    std::string t;
    for (char c : in) {
        if (!std::isalnum((unsigned char)c)) continue;
        char u = (char)std::toupper((unsigned char)c);
        if (u == 'O') u = '0';
        if (u == 'I' || u == 'L') u = '1';
        t += u;
    }
    if (t.size() != 13) return false;
    unsigned __int128 bits = 0;
    for (char c : t) {
        size_t v = C.find(c);
        if (v == std::string::npos) return false;
        bits = (bits << 5) | v;
    }
    out.assign(8, 0);
    for (int i = 7; i >= 0; i--) { out[i] = (unsigned char)(bits & 255); bits >>= 8; }
    return true;
}

} // namespace mulenet
