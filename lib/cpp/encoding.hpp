// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/encoding.hpp
//
// Standard library: encoding (C++-backed backend).
// 标准库：encoding（C++ 底层实现）。
//
// Dependency-free text codecs: base64 (RFC 4648), hex (lower/upper case
// tolerant on decode), and URL percent-encoding (RFC 3986). All operate on
// std::String and return std::String, so binary payloads are carried as raw
// byte strings. The C++ twin of lib/encoding.synl. Self-registered under
// "encoding".
// 零依赖文本编解码：base64（RFC 4648）、hex（解码容错大小写）、URL 百分号
// 编码（RFC 3986）。全部以 std::String 输入输出，故二进制载荷以原始字节串
// 承载。本文件是 lib/encoding.synl 的 C++ 孪生体，以 "encoding" 自注册。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <cctype>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_encoding {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // ---- base64 (RFC 4648) / 实现 ----
    static const char* B64_CHARS =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    inline std::string b64_encode(const std::string& in) {
        std::string out;
        out.reserve(((in.size() + 2) / 3) * 4);
        for (std::size_t i = 0; i < in.size(); i += 3) {
            std::uint32_t n = (static_cast<unsigned char>(in[i]) << 16);
            int len = static_cast<int>(in.size()) - static_cast<int>(i);  // 1..3
            if (len > 1) n |= (static_cast<unsigned char>(in[i + 1]) << 8);
            if (len > 2) n |= (static_cast<unsigned char>(in[i + 2]));
            out += B64_CHARS[(n >> 18) & 63];
            out += B64_CHARS[(n >> 12) & 63];
            if (len > 1) out += B64_CHARS[(n >> 6) & 63];
            else out += '=';
            if (len > 2) out += B64_CHARS[n & 63];
            else out += '=';
        }
        return out;
    }

    inline int b64_val(char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    inline std::string b64_decode(const std::string& in) {
        std::string out;
        int buf = 0, bits = 0;
        for (char c : in) {
            if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t')
                continue;
            int v = b64_val(c);
            if (v < 0) throw std::runtime_error("encoding.base64_decode: invalid character");
            buf = (buf << 6) | v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out += static_cast<char>((buf >> bits) & 0xFF);
            }
        }
        return out;
    }

    // ---- hex (RFC 4648 style, lowercase output) / 实现 ----
    inline std::string hex_encode(const std::string& in) {
        static const char* H = "0123456789abcdef";
        std::string out;
        out.reserve(in.size() * 2);
        for (unsigned char c : in) {
            out += H[c >> 4];
            out += H[c & 0xF];
        }
        return out;
    }

    inline int hex_val(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    inline std::string hex_decode(const std::string& in) {
        if (in.size() % 2 != 0)
            throw std::runtime_error("encoding.unhex: odd-length hex string");
        std::string out;
        out.reserve(in.size() / 2);
        for (std::size_t i = 0; i < in.size(); i += 2) {
            int hi = hex_val(in[i]);
            int lo = hex_val(in[i + 1]);
            if (hi < 0 || lo < 0)
                throw std::runtime_error("encoding.unhex: invalid hex digit");
            out += static_cast<char>((hi << 4) | lo);
        }
        return out;
    }

    // ---- url percent-encoding (RFC 3986) / 实现 ----
    inline bool url_unreserved(unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_' ||
               c == '.' || c == '~';
    }

    inline std::string url_encode(const std::string& in) {
        static const char* HEX = "0123456789ABCDEF";
        std::string out;
        out.reserve(in.size());
        for (unsigned char c : in) {
            if (url_unreserved(c)) {
                out += static_cast<char>(c);
            } else {
                out += '%';
                out += HEX[c >> 4];
                out += HEX[c & 0xF];
            }
        }
        return out;
    }

    inline std::string url_decode(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (std::size_t i = 0; i < in.size(); ++i) {
            char c = in[i];
            if (c == '%' && i + 2 < in.size()) {
                int hi = hex_val(in[i + 1]);
                int lo = hex_val(in[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    out += static_cast<char>((hi << 4) | lo);
                    i += 2;
                    continue;
                }
            }
            out += c;
        }
        return out;
    }

    // ---- native methods / 原生方法 ----

    inline rt_basic::Callable method_encoding_base64_encode() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto data = rb::string_of(rb::para_at(paras, 0));
                if (!data) {
                    return rb::list_of({rb::native_error(
                        "encoding.base64_encode requires a std::String")});
                }
                return rb::list_of({rb::make_string(b64_encode(*data))});
            },
            rb::make_sign("base64_encode",
                {{"data", "std::String"}}, {{"text", "std::String"}})
        );
    }

    inline rt_basic::Callable method_encoding_base64_decode() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) {
                    return rb::list_of({rb::native_error(
                        "encoding.base64_decode requires a std::String")});
                }
                try {
                    return rb::list_of({rb::make_string(b64_decode(*text))});
                } catch (const std::exception& e) {
                    return rb::list_of({rb::native_error(
                        std::string("encoding.base64_decode: ") + e.what())});
                }
            },
            rb::make_sign("base64_decode",
                {{"text", "std::String"}}, {{"data", "std::String"}})
        );
    }

    inline rt_basic::Callable method_encoding_hex() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto data = rb::string_of(rb::para_at(paras, 0));
                if (!data) {
                    return rb::list_of({rb::native_error(
                        "encoding.hex requires a std::String")});
                }
                return rb::list_of({rb::make_string(hex_encode(*data))});
            },
            rb::make_sign("hex",
                {{"data", "std::String"}}, {{"text", "std::String"}})
        );
    }

    inline rt_basic::Callable method_encoding_unhex() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) {
                    return rb::list_of({rb::native_error(
                        "encoding.unhex requires a std::String")});
                }
                try {
                    return rb::list_of({rb::make_string(hex_decode(*text))});
                } catch (const std::exception& e) {
                    return rb::list_of({rb::native_error(
                        std::string("encoding.unhex: ") + e.what())});
                }
            },
            rb::make_sign("unhex",
                {{"text", "std::String"}}, {{"data", "std::String"}})
        );
    }

    inline rt_basic::Callable method_encoding_url_encode() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) {
                    return rb::list_of({rb::native_error(
                        "encoding.url_encode requires a std::String")});
                }
                return rb::list_of({rb::make_string(url_encode(*text))});
            },
            rb::make_sign("url_encode",
                {{"text", "std::String"}}, {{"text", "std::String"}})
        );
    }

    inline rt_basic::Callable method_encoding_url_decode() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) {
                    return rb::list_of({rb::native_error(
                        "encoding.url_decode requires a std::String")});
                }
                return rb::list_of({rb::make_string(url_decode(*text))});
            },
            rb::make_sign("url_decode",
                {{"text", "std::String"}}, {{"text", "std::String"}})
        );
    }

    // ---- registration / 登记 ----
    inline void init_encoding_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(
            ::stdRT.getcls("Object")
        );
        proto->set_method("base64_encode", method_encoding_base64_encode());
        proto->set_method("base64_decode", method_encoding_base64_decode());
        proto->set_method("hex",           method_encoding_hex());
        proto->set_method("unhex",         method_encoding_unhex());
        proto->set_method("url_encode",    method_encoding_url_encode());
        proto->set_method("url_decode",    method_encoding_url_decode());

        runtime::Prototypes p;
        // Centralized registration MUST carry the package index `encoding::` (the
        // "集中的必须加" rule): this is what makes `encoding` a known set and lets
        // `encoding::Encoding` resolve. The NAME itself is `Encoding`; the `::`-prefixed
        // part is only a package locator. The module's own lib/encoding.synl declares
        // the preset object with the BARE name `Encoding`.
        // 集中登记须带包索引 `encoding::`（「集中的必须加」）：这才能使 `encoding` 成为已知集、
        // `encoding::Encoding` 可解析。名字本身仍是 `Encoding`，`::` 前缀只是包定位符。
        // 模块自身的 lib/encoding.synl 以裸名 `Encoding` 声明预置对象。
        p.regcls("Encoding", proto);
        ::stdRT.add_protos(p);
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("encoding", &init_encoding_stdlib), true);

} // namespace rt_lib_encoding
