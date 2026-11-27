// Minimal base64 decoder (RFC 4648, ignores whitespace and padding) for preset-embedded audio.
#pragma once
#include <string>
#include <vector>
namespace vv {
inline std::vector<unsigned char> base64Decode(const std::string& s) {
    static signed char T[256]; static bool init = false;
    if (!init) { for (int i = 0; i < 256; i++) T[i] = -1; const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; for (int i = 0; i < 64; i++) T[(unsigned char)A[i]] = (signed char)i; T[(unsigned char)'-'] = 62; T[(unsigned char)'_'] = 63; init = true; }
    std::vector<unsigned char> out; out.reserve(s.size() * 3 / 4 + 3); unsigned acc = 0; int bits = 0;
    for (unsigned char c : s) { const int v = T[c]; if (v < 0) continue; acc = (acc << 6) | (unsigned)v; bits += 6; if (bits >= 8) { bits -= 8; out.push_back((unsigned char)((acc >> bits) & 0xff)); } }
    return out;
}
}
