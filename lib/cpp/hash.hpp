// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/hash.hpp
//
// Standard library: hash (C++-backed backend).
// 标准库：hash（C++ 底层实现）。
//
// Provides hashing helpers on std::String:
//   sha256(text) -> String   (FIPS 180-4, lowercase hex, 64 chars)
//   crc32(text)  -> Number   (IEEE 802.3 polynomial, exact < 2^32)
//   fnv1a(text)  -> String   (FNV-1a 64-bit, lowercase hex, 16 chars)
// The C++ twin of lib/hash.synl. Self-registered under "hash".
// 提供针对 std::String 的哈希：
//   sha256(text) -> String（FIPS 180-4，小写十六进制，64 字符）
//   crc32(text)  -> Number（IEEE 802.3 多项式，< 2^32 精确）
//   fnv1a(text)  -> String（FNV-1a 64 位，小写十六进制，16 字符）
// 本文件是 lib/hash.synl 的 C++ 孪生体，以 "hash" 自注册。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <sstream>
#include <iomanip>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_hash {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // ---- SHA-256 (FIPS 180-4) / 实现 ----

    static inline uint32_t sha_rotr(uint32_t x, uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }

    static inline void sha256(const std::string& msg, uint8_t digest[32]) {
        const uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,
            0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
            0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
            0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
            0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
            0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
            0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
            0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
            0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };
        uint32_t H[8] = {
            0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,
            0x1f83d9ab,0x5be0cd19
        };

        // Pre-processing: padding.
        uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8;
        std::vector<uint8_t> data(msg.begin(), msg.end());
        data.push_back(0x80);
        while (data.size() % 64 != 56) {
            data.push_back(0x00);
        }
        for (int i = 7; i >= 0; --i) {
            data.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFF));
        }

        std::vector<uint32_t> W(64);
        for (size_t off = 0; off < data.size(); off += 64) {
            for (int t = 0; t < 16; ++t) {
                W[t] = (static_cast<uint32_t>(data[off + t*4]) << 24)
                     | (static_cast<uint32_t>(data[off + t*4 + 1]) << 16)
                     | (static_cast<uint32_t>(data[off + t*4 + 2]) << 8)
                     | (static_cast<uint32_t>(data[off + t*4 + 3]));
            }
            for (int t = 16; t < 64; ++t) {
                uint32_t s0 = sha_rotr(W[t-15],7) ^ sha_rotr(W[t-15],18)
                            ^ (W[t-15] >> 3);
                uint32_t s1 = sha_rotr(W[t-2],17) ^ sha_rotr(W[t-2],19)
                            ^ (W[t-2] >> 10);
                W[t] = W[t-16] + s0 + W[t-7] + s1;
            }

            uint32_t a = H[0], b = H[1], c = H[2], d = H[3];
            uint32_t e = H[4], f = H[5], g = H[6], h = H[7];

            for (int t = 0; t < 64; ++t) {
                uint32_t S1 = sha_rotr(e,6) ^ sha_rotr(e,11) ^ sha_rotr(e,25);
                uint32_t ch = (e & f) ^ ((~e) & g);
                uint32_t t1 = h + S1 + ch + K[t] + W[t];
                uint32_t S0 = sha_rotr(a,2) ^ sha_rotr(a,13) ^ sha_rotr(a,22);
                uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                uint32_t t2 = S0 + maj;
                h = g; g = f; f = e; e = d + t1;
                d = c; c = b; b = a; a = t1 + t2;
            }

            H[0] += a; H[1] += b; H[2] += c; H[3] += d;
            H[4] += e; H[5] += f; H[6] += g; H[7] += h;
        }

        for (int i = 0; i < 8; ++i) {
            digest[i*4 + 0] = static_cast<uint8_t>((H[i] >> 24) & 0xFF);
            digest[i*4 + 1] = static_cast<uint8_t>((H[i] >> 16) & 0xFF);
            digest[i*4 + 2] = static_cast<uint8_t>((H[i] >> 8) & 0xFF);
            digest[i*4 + 3] = static_cast<uint8_t>((H[i]) & 0xFF);
        }
    }

    static inline std::string to_hex(const uint8_t* bytes, std::size_t n) {
        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        for (std::size_t i = 0; i < n; ++i) {
            ss << std::setw(2) << static_cast<int>(bytes[i]);
        }
        return ss.str();
    }

    // ---- CRC32 (IEEE 802.3) / 实现 ----

    static inline uint32_t crc32(const std::string& s) {
        uint32_t crc = 0xFFFFFFFFu;
        for (unsigned char ch : s) {
            crc ^= static_cast<uint32_t>(ch);
            for (int i = 0; i < 8; ++i) {
                uint32_t mask = -(crc & 1u);
                crc = (crc >> 1) ^ (0xEDB88320u & mask);
            }
        }
        return ~crc;
    }

    // ---- FNV-1a 64-bit / 实现 ----

    static inline uint64_t fnv1a_64(const std::string& s) {
        uint64_t h = 0xcbf29ce484222325ull;
        const uint64_t prime = 0x100000001b3ull;
        for (unsigned char ch : s) {
            h ^= static_cast<uint64_t>(ch);
            h *= prime;
        }
        return h;
    }

    // ---- native methods / 原生方法 ----

    inline rt_basic::Callable method_hash_sha256() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) {
                    return rb::list_of({rb::native_error(
                        "hash.sha256 requires a std::String")});
                }
                uint8_t digest[32];
                sha256(*text, digest);
                return rb::list_of({rb::make_string(to_hex(digest, 32))});
            },
            rb::make_sign("sha256", {{"text", "std::String"}},
                {{"digest", "std::String"}})
        );
    }

    inline rt_basic::Callable method_hash_crc32() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) {
                    return rb::list_of({rb::native_error(
                        "hash.crc32 requires a std::String")});
                }
                return rb::list_of({rb::make_int(
                    static_cast<std::int64_t>(crc32(*text)))});
            },
            rb::make_sign("crc32", {{"text", "std::String"}},
                {{"checksum", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_hash_fnv1a() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) {
                    return rb::list_of({rb::native_error(
                        "hash.fnv1a requires a std::String")});
                }
                uint64_t h = fnv1a_64(*text);
                uint8_t bytes[8];
                for (int i = 7; i >= 0; --i) {
                    bytes[i] = static_cast<uint8_t>(h & 0xFF);
                    h >>= 8;
                }
                return rb::list_of({rb::make_string(to_hex(bytes, 8))});
            },
            rb::make_sign("fnv1a", {{"text", "std::String"}},
                {{"digest", "std::String"}})
        );
    }

    // ---- registration / 登记 ----
    inline void init_hash_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(
            ::stdRT.getcls("Object")
        );
        proto->set_method("sha256", method_hash_sha256());
        proto->set_method("crc32",  method_hash_crc32());
        proto->set_method("fnv1a",  method_hash_fnv1a());

        runtime::Prototypes p;
        p.regcls("Hash", proto);
        ::stdRT.add_protos(p);
    }

    // Self-register so the interpreter can initialize this library.
    // 自注册，使解释器能够初始化本库。
    inline bool _registered =
        (rt_builtin::register_native_lib("hash", &init_hash_stdlib), true);

} // namespace rt_lib_hash
