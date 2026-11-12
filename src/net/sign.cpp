// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Board signing for the nets: Ed25519 through the vendored Monocypher, hex helpers, and the
// short board id (first 16 hex characters of SHA-256 over the public key).
#include "net/sign.h"

extern "C" {
#include "monocypher.h"
#include "sha256.h"
}

namespace axis::net {

KeyPair keyFromSeed(const uint8_t seed[32]) {
    KeyPair k;
    uint8_t tmp[32];
    for (int i = 0; i < 32; ++i) tmp[i] = seed[i];  // key_pair wipes its seed argument
    crypto_eddsa_key_pair(k.secret, k.pub, tmp);
    crypto_wipe(tmp, sizeof tmp);
    return k;
}

std::string toHex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

static int nib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool fromHex(const std::string& hex, uint8_t* out, size_t n) {
    if (hex.size() != n * 2) return false;
    for (size_t i = 0; i < n; ++i) {
        int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>(hi << 4 | lo);
    }
    return true;
}

void wipe(KeyPair& k) { crypto_wipe(&k, sizeof k); }

std::string signHex(const KeyPair& k, const std::string& msg) {
    uint8_t sig[64];
    crypto_eddsa_sign(sig, k.secret, reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
    return toHex(sig, sizeof sig);
}

bool verifyHex(const std::string& pubHex, const std::string& msg, const std::string& sigHex) {
    uint8_t pub[32], sig[64];
    if (!fromHex(pubHex, pub, sizeof pub) || !fromHex(sigHex, sig, sizeof sig)) return false;
    return crypto_eddsa_check(sig, pub, reinterpret_cast<const uint8_t*>(msg.data()), msg.size()) == 0;
}

std::string boardId(const std::string& pubHex) {
    uint8_t pub[32];
    if (!fromHex(pubHex, pub, sizeof pub)) return std::string();
    SHA256_CTX c;
    uint8_t h[SHA256_BLOCK_SIZE];
    sha256_init(&c);
    sha256_update(&c, pub, sizeof pub);
    sha256_final(&c, h);
    return toHex(h, 8);
}

}  // namespace axis::net
