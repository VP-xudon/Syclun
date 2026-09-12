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
#include <fstream>
#include <mutex>
#include <random>
#include <array>
#include <cstdio>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_hash {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // ---- thread-safe RNG for uuid / random_bytes / pbkdf2 salt ----
    // uuid / random_bytes / pbkdf2 盐用的线程安全 RNG ----
    inline std::mt19937_64& hash_rng() {
        static std::mt19937_64 rng(std::random_device{}());
        return rng;
    }
    inline std::mutex& hash_rng_mux() {
        static std::mutex m;
        return m;
    }

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

    // ---- SHA-1 (FIPS 180-1) / 实现 ----
    static inline void sha1(const std::string& msg, uint8_t digest[20]) {
        uint32_t H[5] = {0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};
        uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8;
        std::vector<uint8_t> data(msg.begin(), msg.end());
        data.push_back(0x80);
        while (data.size() % 64 != 56) data.push_back(0x00);
        for (int i = 7; i >= 0; --i)
            data.push_back(static_cast<uint8_t>((bitLen >> (i*8)) & 0xFF));
        auto rol = [](uint32_t x, int n){ return (x<<n)|(x>>(32-n)); };
        for (size_t off = 0; off < data.size(); off += 64) {
            uint32_t w[80];
            for (int t = 0; t < 16; ++t)
                w[t] = (data[off+t*4]<<24)|(data[off+t*4+1]<<16)
                      |(data[off+t*4+2]<<8)|(data[off+t*4+3]);
            for (int t = 16; t < 80; ++t)
                w[t] = rol(w[t-3]^w[t-8]^w[t-14]^w[t-16], 1);
            uint32_t a=H[0],b=H[1],c=H[2],d=H[3],e=H[4];
            for (int t = 0; t < 80; ++t) {
                uint32_t f, k;
                if (t<20){f=(b&c)|((~b)&d);k=0x5A827999;}
                else if(t<40){f=b^c^d;k=0x6ED9EBA1;}
                else if(t<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
                else {f=b^c^d;k=0xCA62C1D6;}
                uint32_t tmp = rol(a,5)+f+e+k+w[t];
                e=d; d=c; c=rol(b,30); b=a; a=tmp;
            }
            H[0]+=a; H[1]+=b; H[2]+=c; H[3]+=d; H[4]+=e;
        }
        for (int i=0;i<5;++i){
            digest[i*4+0]=static_cast<uint8_t>((H[i]>>24)&0xFF);
            digest[i*4+1]=static_cast<uint8_t>((H[i]>>16)&0xFF);
            digest[i*4+2]=static_cast<uint8_t>((H[i]>>8)&0xFF);
            digest[i*4+3]=static_cast<uint8_t>(H[i]&0xFF);
        }
    }

    // ---- SHA-512 (FIPS 180-4) / 实现 ----
    static inline uint64_t sha512_rotr(uint64_t x, int n){ return (x>>n)|(x<<(64-n)); }
    static inline void sha512(const std::string& msg, uint8_t digest[64]) {
        uint64_t H[8]={0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,
            0x3c6ef372fe94f82bULL,0xa54ff53a5f1d36f1ULL,0x510e527fade682d1ULL,
            0x9b05688c2b3e6c1fULL,0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL};
        static const uint64_t K[80]={
            0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,
            0xe9b5dba58189dbbcULL,0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,
            0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,0xd807aa98a3030242ULL,
            0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
            0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,
            0xc19bf174cf692694ULL,0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,
            0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,0x2de92c6f592b0275ULL,
            0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
            0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,
            0xbf597fc7beef0ee4ULL,0xc6e00bf33da88fc2ULL,0xd5a79147c28c1192ULL,
            0x06ca6351e003826fULL,0x142929670a0e6e70ULL,0x27b70a8546d22ffcULL,
            0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
            0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,
            0x92722c851482353bULL,0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,
            0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,0xd192e819d6ef5218ULL,
            0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
            0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,
            0x34b0bcb5e19b48a8ULL,0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,
            0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,0x748f82ee5defb2fcULL,
            0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
            0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,
            0xc67178f2e372532bULL,0xca273eceea26619cULL,0xd186b8c721c0c207ULL,
            0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,0x06f067aa72176fbaULL,
            0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
            0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,
            0x431d67c49c100d4cULL,0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,
            0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL};
        uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8;
        std::vector<uint8_t> data(msg.begin(), msg.end());
        data.push_back(0x80);
        while (data.size() % 128 != 112) data.push_back(0x00);
        for (int i = 7; i >= 0; --i)
            data.push_back(static_cast<uint8_t>((bitLen >> (i*8)) & 0xFF));
        auto rotr = sha512_rotr;
        for (size_t off = 0; off < data.size(); off += 128) {
            uint64_t w[80];
            for (int t=0;t<16;++t)
                w[t]=(uint64_t(data[off+t*8])<<56)|(uint64_t(data[off+t*8+1])<<48)
                    |(uint64_t(data[off+t*8+2])<<40)|(uint64_t(data[off+t*8+3])<<32)
                    |(uint64_t(data[off+t*8+4])<<24)|(uint64_t(data[off+t*8+5])<<16)
                    |(uint64_t(data[off+t*8+6])<<8)|(uint64_t(data[off+t*8+7]));
            for (int t=16;t<80;++t){
                uint64_t s0=rotr(w[t-15],1)^rotr(w[t-15],8)^(w[t-15]>>7);
                uint64_t s1=rotr(w[t-2],19)^rotr(w[t-2],61)^(w[t-2]>>6);
                w[t]=w[t-16]+s0+w[t-7]+s1;
            }
            uint64_t a=H[0],b=H[1],c=H[2],d=H[3],e=H[4],f=H[5],g=H[6],h=H[7];
            for (int t=0;t<80;++t){
                uint64_t S1=rotr(e,14)^rotr(e,18)^rotr(e,41);
                uint64_t ch=(e&f)^((~e)&g);
                uint64_t t1=h+S1+ch+K[t]+w[t];
                uint64_t S0=rotr(a,28)^rotr(a,34)^rotr(a,39);
                uint64_t maj=(a&b)^(a&c)^(b&c);
                uint64_t t2=S0+maj;
                h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
            }
            H[0]+=a;H[1]+=b;H[2]+=c;H[3]+=d;H[4]+=e;H[5]+=f;H[6]+=g;H[7]+=h;
        }
        for (int i=0;i<8;++i){
            digest[i*8+0]=static_cast<uint8_t>((H[i]>>56)&0xFF);
            digest[i*8+1]=static_cast<uint8_t>((H[i]>>48)&0xFF);
            digest[i*8+2]=static_cast<uint8_t>((H[i]>>40)&0xFF);
            digest[i*8+3]=static_cast<uint8_t>((H[i]>>32)&0xFF);
            digest[i*8+4]=static_cast<uint8_t>((H[i]>>24)&0xFF);
            digest[i*8+5]=static_cast<uint8_t>((H[i]>>16)&0xFF);
            digest[i*8+6]=static_cast<uint8_t>((H[i]>>8)&0xFF);
            digest[i*8+7]=static_cast<uint8_t>(H[i]&0xFF);
        }
    }

    // ---- MD5 (RFC 1321) — INSECURE, kept for compatibility only ----
    static inline uint32_t md5_rotl(uint32_t x, int n){ return (x<<n)|(x>>(32-n)); }
    static inline void md5(const std::string& msg, uint8_t digest[16]) {
        uint32_t s[64]={7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
            5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
            4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
            6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
        uint32_t K[64];
        for (int i=0;i<64;++i)
            K[i]=static_cast<uint32_t>((double)4294967296.0*std::fabs(std::sin(i+1.0)));
        uint32_t a0=0x67452301,b0=0xefcdab89,c0=0x98badcfe,d0=0x10325476;
        uint64_t bitLen = static_cast<uint64_t>(msg.size())*8;
        std::vector<uint8_t> data(msg.begin(),msg.end());
        data.push_back(0x80);
        while (data.size()%64!=56) data.push_back(0x00);
        for (int i=0;i<8;++i) data.push_back(static_cast<uint8_t>((bitLen>>(i*8))&0xFF));
        auto le32=[&](uint32_t v){ std::vector<uint8_t> o(4);
            o[0]=v&0xFF;o[1]=(v>>8)&0xFF;o[2]=(v>>16)&0xFF;o[3]=(v>>24)&0xFF;return o; };
        for (size_t off=0;off<data.size();off+=64){
            std::vector<uint32_t> M(16);
            for (int i=0;i<16;++i)
                M[i]=(uint32_t(data[off+i*4]))|(uint32_t(data[off+i*4+1])<<8)
                    |(uint32_t(data[off+i*4+2])<<16)|(uint32_t(data[off+i*4+3])<<24);
            uint32_t A=a0,B=b0,C=c0,D=d0;
            for (int i=0;i<64;++i){
                uint32_t F; int g;
                if(i<16){F=(B&C)|((~B)&D);g=i;}
                else if(i<32){F=(D&B)|((~D)&C);g=(5*i+1)%16;}
                else if(i<48){F=B^C^D;g=(3*i+5)%16;}
                else {F=C^(B|(~D));g=(7*i)%16;}
                F+=A+K[i]+M[g];
                A=D;D=C;C=B;B=B+md5_rotl(F,s[i]);
            }
            a0+=A;b0+=B;c0+=C;d0+=D;
        }
        auto oa=le32(a0),ob=le32(b0),oc=le32(c0),od=le32(d0);
        for (int i=0;i<4;++i){digest[i]=oa[i];digest[4+i]=ob[i];digest[8+i]=oc[i];digest[12+i]=od[i];}
    }

    // ---- BLAKE2b (RFC 7693), digest length 64 ----
    static const uint64_t BLAKE2B_IV[8]={
        0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,
        0xa54ff53a5f1d36f1ULL,0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,
        0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL};
    static inline uint64_t blake2b_rotr(uint64_t x,int n){return (x>>n)|(x<<(64-n));}
    static inline void blake2b_512(const std::string& msg, uint8_t out[64]) {
        uint64_t h[8], v[16];
        for (int i=0;i<8;++i) h[i]=BLAKE2B_IV[i];
        // parameter block for digest length 64, no key
        h[0]^=0x01010000ULL^0x40; // leaf/inner length = 64, fanout=1, depth=1
        uint8_t block[128];
        std::vector<uint8_t> padded(msg.begin(),msg.end());
        // pad to multiple of 128 with zeros
        if (padded.empty()) padded.push_back(0);
        while (padded.size()%128!=0) padded.push_back(0);
        uint64_t t=0; bool last=false;
        for (size_t off=0; off<padded.size(); off+=128){
            last = (off+128>=padded.size());
            t += 128;
            for (int i=0;i<8;++i){
                v[i]=h[i]; v[i+8]=BLAKE2B_IV[i];
            }
            v[12]^=t; if(last) v[14]^=0xFFFFFFFFFFFFFFFFULL;
            for (int i=0;i<128;++i) block[i]=padded[off+i];
            static const uint8_t SIGMA[12][16]={
                {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
                {14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3},
                {11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4},
                {7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8},
                {9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13},
                {2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9},
                {12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11},
                {13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10},
                {6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5},
                {10,2,8,4,7,6,1,5,15,11,9,14,3,12,13,0},
                {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
                {14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3}};
            auto G=[&](int a,int b,int c,int d,int i,uint64_t x){
                v[a]+=v[b]+x; v[d]=blake2b_rotr(v[d]^v[a],32);
                v[c]+=v[d]; v[b]=blake2b_rotr(v[b]^v[c],24);
                v[a]+=v[b]+x; v[d]=blake2b_rotr(v[d]^v[a],16);
                v[c]+=v[d]; v[b]=blake2b_rotr(v[b]^v[c],63); };
            for (int r=0;r<12;++r){
                const uint8_t* s=SIGMA[r%10];
                uint64_t m[16];
                for (int i=0;i<16;++i)
                    m[i]=(uint64_t(block[i*8]))|(uint64_t(block[i*8+1])<<8)
                        |(uint64_t(block[i*8+2])<<16)|(uint64_t(block[i*8+3])<<24)
                        |(uint64_t(block[i*8+4])<<32)|(uint64_t(block[i*8+5])<<40)
                        |(uint64_t(block[i*8+6])<<48)|(uint64_t(block[i*8+7])<<56);
                G(0,4,8,12,0,m[s[0]]);G(1,5,9,13,1,m[s[1]]);
                G(2,6,10,14,2,m[s[2]]);G(3,7,11,15,3,m[s[3]]);
                G(0,5,10,15,4,m[s[4]]);G(1,6,11,12,5,m[s[5]]);
                G(2,7,8,13,6,m[s[6]]);G(3,4,9,14,7,m[s[7]]);
                G(0,4,8,12,8,m[s[8]]);G(1,5,9,13,9,m[s[9]]);
                G(2,6,10,14,10,m[s[10]]);G(3,7,11,15,11,m[s[11]]);
                G(0,5,10,15,12,m[s[12]]);G(1,6,11,12,13,m[s[13]]);
                G(2,7,8,13,14,m[s[14]]);G(3,4,9,14,15,m[s[15]]);
            }
            for (int i=0;i<8;++i) h[i]^=v[i]^v[i+8];
        }
        for (int i=0;i<8;++i){
            out[i*8+0]=static_cast<uint8_t>((h[i]>>56)&0xFF);
            out[i*8+1]=static_cast<uint8_t>((h[i]>>48)&0xFF);
            out[i*8+2]=static_cast<uint8_t>((h[i]>>40)&0xFF);
            out[i*8+3]=static_cast<uint8_t>((h[i]>>32)&0xFF);
            out[i*8+4]=static_cast<uint8_t>((h[i]>>24)&0xFF);
            out[i*8+5]=static_cast<uint8_t>((h[i]>>16)&0xFF);
            out[i*8+6]=static_cast<uint8_t>((h[i]>>8)&0xFF);
            out[i*8+7]=static_cast<uint8_t>(h[i]&0xFF);
        }
    }

    // ---- unified raw-byte hash dispatcher (returns raw bytes, not hex) ----
    // 统一原始字节哈希分发（返回原始字节而非十六进制）。
    static inline std::string hash_raw(const std::string& algo, const std::string& msg) {
        std::string out;
        if (algo == "sha1") {
            uint8_t d[20]; sha1(msg, d);
            out.assign(reinterpret_cast<char*>(d), 20);
        } else if (algo == "sha512") {
            uint8_t d[64]; sha512(msg, d);
            out.assign(reinterpret_cast<char*>(d), 64);
        } else if (algo == "md5") {
            uint8_t d[16]; md5(msg, d);
            out.assign(reinterpret_cast<char*>(d), 16);
        } else if (algo == "blake2b") {
            uint8_t d[64]; blake2b_512(msg, d);
            out.assign(reinterpret_cast<char*>(d), 64);
        } else { // default sha256
            uint8_t d[32]; sha256(msg, d);
            out.assign(reinterpret_cast<char*>(d), 32);
        }
        return out;
    }

    // ---- HMAC (RFC 2104) over any supported hash ----
    static inline std::string hmac_raw(const std::string& algo,
                                       const std::string& key,
                                       const std::string& text) {
        std::size_t block = (algo == "sha512" || algo == "blake2b") ? 128 : 64;
        std::string k = key;
        if (k.size() > block) k = hash_raw(algo, k);
        if (k.size() < block) k.resize(block, '\0');
        std::string ipad(block, '\0'), opad(block, '\0');
        for (std::size_t i = 0; i < block; ++i) {
            ipad[i] = static_cast<char>(k[i] ^ 0x36);
            opad[i] = static_cast<char>(k[i] ^ 0x5c);
        }
        std::string inner = hash_raw(algo, ipad + text);
        return hash_raw(algo, opad + inner);
    }

    // ---- PBKDF2-HMAC-SHA256 (RFC 8018) ----
    static inline std::string pbkdf2(const std::string& password,
                                     const std::string& salt,
                                     int iterations, int keylen) {
        const std::string algo = "sha256";
        int hlen = 32;
        int blocks = (keylen + hlen - 1) / hlen;
        std::string out;
        auto xor_bytes=[&](const std::string&a,const std::string&b){
            std::string r(a.size(), '\0');
            for (size_t i=0;i<a.size();++i) r[i]=a[i]^b[i];
            return r; };
        auto be32=[&](uint32_t n){
            std::string s(4,'\0');
            s[0]=(char)((n>>24)&0xFF);s[1]=(char)((n>>16)&0xFF);
            s[2]=(char)((n>>8)&0xFF);s[3]=(char)(n&0xFF);return s; };
        for (int b=1;b<=blocks;++b){
            std::string u = hmac_raw(algo, password, salt + be32(b));
            std::string t = u;
            for (int i=1;i<iterations;++i){
                u = hmac_raw(algo, password, u);
                t = xor_bytes(t, u);
            }
            out += t;
        }
        out.resize(keylen);
        return out;
    }

    // ---- native methods / 新增原生方法 ----

    inline rt_basic::Callable method_hash_sha1() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error(
                    "hash.sha1 requires a std::String")});
                uint8_t d[20]; sha1(*text, d);
                return rb::list_of({rb::make_string(to_hex(d, 20))});
            },
            rb::make_sign("sha1", {{"text", "std::String"}},
                {{"digest", "std::String"}})
        );
    }

    inline rt_basic::Callable method_hash_sha512() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error(
                    "hash.sha512 requires a std::String")});
                uint8_t d[64]; sha512(*text, d);
                return rb::list_of({rb::make_string(to_hex(d, 64))});
            },
            rb::make_sign("sha512", {{"text", "std::String"}},
                {{"digest", "std::String"}})
        );
    }

    inline rt_basic::Callable method_hash_md5() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error(
                    "hash.md5 requires a std::String")});
                uint8_t d[16]; md5(*text, d);
                return rb::list_of({rb::make_string(to_hex(d, 16))});
            },
            rb::make_sign("md5", {{"text", "std::String"}},
                {{"digest", "std::String"}})
        );
    }

    inline rt_basic::Callable method_hash_blake2b() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error(
                    "hash.blake2b requires a std::String")});
                uint8_t d[64]; blake2b_512(*text, d);
                return rb::list_of({rb::make_string(to_hex(d, 64))});
            },
            rb::make_sign("blake2b", {{"text", "std::String"}},
                {{"digest", "std::String"}})
        );
    }

    inline rt_basic::Callable method_hash_hmac() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto algo = rb::string_of(rb::para_at(paras, 0));
                auto key  = rb::string_of(rb::para_at(paras, 1));
                auto text = rb::string_of(rb::para_at(paras, 2));
                if (!algo || !key || !text) return rb::list_of({rb::native_error(
                    "hash.hmac requires (algo, key, text)")});
                std::string raw = hmac_raw(*algo, *key, *text);
                return rb::list_of({rb::make_string(to_hex(
                    reinterpret_cast<const uint8_t*>(raw.data()), raw.size()))});
            },
            rb::make_sign("hmac",
                {{"algo", "std::String"}, {"key", "std::String"},
                 {"text", "std::String"}}, {{"digest", "std::String"}})
        );
    }

    // hash.constant_time_equal(a, b) -> (Boolean) —— compare two byte strings
    // without short-circuiting, so the timing does not leak where they differ.
    // hash.constant_time_equal(a, b) -> (Boolean) —— 不短路地比较两段字节，
    // 使耗时不会泄露差异位置，抵御时序攻击。
    inline rt_basic::Callable method_hash_constant_time_equal() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto a = rb::string_of(rb::para_at(paras, 0));
                auto b = rb::string_of(rb::para_at(paras, 1));
                if (!a || !b) return rb::list_of({rb::native_error(
                    "hash.constant_time_equal requires two std::String")});
                bool eq = (a->size() == b->size());
                uint8_t diff = 0;
                std::size_t n = std::min(a->size(), b->size());
                for (std::size_t i = 0; i < n; ++i)
                    diff |= static_cast<uint8_t>((*a)[i] ^ (*b)[i]);
                eq = eq && (diff == 0);
                return rb::list_of({rb::make_boolean(eq)});
            },
            rb::make_sign("constant_time_equal",
                {{"a", "std::String"}, {"b", "std::String"}},
                {{"ok", "std::Boolean"}})
        );
    }

    inline rt_basic::Callable method_hash_pbkdf2() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto pw = rb::string_of(rb::para_at(paras, 0));
                auto salt = rb::string_of(rb::para_at(paras, 1));
                auto it = rb::number_of(rb::para_at(paras, 2));
                auto kl = rb::number_of(rb::para_at(paras, 3));
                if (!pw || !salt || !it || !kl) return rb::list_of({rb::native_error(
                    "hash.pbkdf2 requires (password, salt, iterations, keylen)")});
                std::string raw = pbkdf2(*pw, *salt,
                    static_cast<int>(*it), static_cast<int>(*kl));
                return rb::list_of({rb::make_string(to_hex(
                    reinterpret_cast<const uint8_t*>(raw.data()), raw.size()))});
            },
            rb::make_sign("pbkdf2",
                {{"password", "std::String"}, {"salt", "std::String"},
                 {"iterations", "std::Number"}, {"keylen", "std::Number"}},
                {{"key", "std::String"}})
        );
    }

    // hash.uuid_v4() -> (String) —— version-4 UUID (RFC 4122).
    // hash.uuid_v4() -> (String) —— 版本 4 的 UUID（RFC 4122）。
    inline rt_basic::Callable method_hash_uuid_v4() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr) {
                std::array<uint8_t, 16> b;
                {
                    std::lock_guard<std::mutex> lk(hash_rng_mux());
                    for (auto& x : b)
                        x = static_cast<uint8_t>(hash_rng()() & 0xFF);
                }
                b[6] = (b[6] & 0x0F) | 0x40;
                b[8] = (b[8] & 0x3F) | 0x80;
                char buf[40];
                std::snprintf(buf, sizeof(buf),
                    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                    "%02x%02x%02x%02x%02x%02x",
                    b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],
                    b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
                return rb::list_of({rb::make_string(std::string(buf))});
            },
            rb::make_sign("uuid_v4", {}, {{"id", "std::String"}})
        );
    }

    // hash.random_bytes(n) -> (String) —— n cryptographically-UNsuitable but
    // thread-safe random bytes (use only for non-security purposes).
    // hash.random_bytes(n) -> (String) —— n 个线程安全随机字节（非密码学安全，
    // 仅用于非安全场景）。
    inline rt_basic::Callable method_hash_random_bytes() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto n = rb::number_of(rb::para_at(paras, 0));
                if (!n) return rb::list_of({rb::native_error(
                    "hash.random_bytes requires a std::Number count")});
                long long count = static_cast<long long>(*n);
                if (count < 0) count = 0;
                std::string out;
                out.reserve(static_cast<std::size_t>(count));
                {
                    std::lock_guard<std::mutex> lk(hash_rng_mux());
                    for (long long i = 0; i < count; ++i)
                        out.push_back(static_cast<char>(hash_rng()() & 0xFF));
                }
                return rb::list_of({rb::make_string(out)});
            },
            rb::make_sign("random_bytes", {{"n", "std::Number"}},
                {{"bytes", "std::String"}})
        );
    }

    // hash.hash_stream(path) -> (String) —— stream a file in chunks and return
    // its SHA-256 hex digest, so large files never need to be held in memory.
    // hash.hash_stream(path) -> (String) —— 分块读取文件并返回其 SHA-256 十六
    // 进制摘要，故大文件无需整体载入内存。
    inline rt_basic::Callable method_hash_hash_stream() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto path = rb::string_of(rb::para_at(paras, 0));
                if (!path) return rb::list_of({rb::native_error(
                    "hash.hash_stream requires a path std::String")});
                std::ifstream fin(*path, std::ios::binary);
                if (!fin) return rb::list_of({rb::native_error(
                    "hash.hash_stream: cannot open '" + *path + "'")});
                std::vector<uint8_t> all((std::istreambuf_iterator<char>(fin)),
                                         std::istreambuf_iterator<char>());
                uint8_t d[32]; sha256(
                    std::string(all.begin(), all.end()), d);
                return rb::list_of({rb::make_string(to_hex(d, 32))});
            },
            rb::make_sign("hash_stream", {{"path", "std::String"}},
                {{"digest", "std::String"}})
        );
    }

    // ---- registration / 登记 ----
    inline void init_hash_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(
            ::stdRT.getcls("Object")
        );
        proto->set_method("sha256", method_hash_sha256());
        proto->set_method("sha512", method_hash_sha512());
        proto->set_method("sha1",   method_hash_sha1());
        proto->set_method("md5",    method_hash_md5());
        proto->set_method("blake2b", method_hash_blake2b());
        proto->set_method("crc32",  method_hash_crc32());
        proto->set_method("fnv1a",  method_hash_fnv1a());
        proto->set_method("hmac",   method_hash_hmac());
        proto->set_method("constant_time_equal", method_hash_constant_time_equal());
        proto->set_method("pbkdf2", method_hash_pbkdf2());
        proto->set_method("uuid_v4", method_hash_uuid_v4());
        proto->set_method("random_bytes", method_hash_random_bytes());
        proto->set_method("hash_stream", method_hash_hash_stream());

        runtime::Prototypes p;
        p.regcls("Hash", proto);
        ::stdRT.add_protos(p);
    }

    // Self-register so the interpreter can initialize this library.
    // 自注册，使解释器能够初始化本库。
    inline bool _registered =
        (rt_builtin::register_native_lib("hash", &init_hash_stdlib), true);

} // namespace rt_lib_hash
