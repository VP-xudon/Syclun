// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/json.hpp
//
// Standard library: json (C++-backed backend).
// 标准库：json（C++ 底层实现）。
//
// A dependency-free, hand-written recursive-descent JSON codec that maps JSON
// values onto native Synth-OOP values and back:
//   JSON object  -> std::Dict
//   JSON array   -> std::Array
//   JSON string  -> std::String
//   JSON number  -> std::Number (integer-ness preserved on parse)
//   JSON boolean -> std::Boolean
//   JSON null    -> std::Object (the root zero-instance)
// The C++ twin of lib/json.synl. Self-registered under "json".
// 一个零依赖、手写的递归下降 JSON 编解码器，把 JSON 值映射为原生
// Synth-OOP 值：对象->Dict、数组->Array、字符串->String、数字->Number
// （解析时保留整数性）、布尔->Boolean、null->Object（万物之源零实例）。
// 本文件是 lib/json.synl 的 C++ 孪生体，以 "json" 自注册。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_json {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // --------------------------------------------------------
    // Stringification helpers / 序列化辅助
    // --------------------------------------------------------

    // Escape a std::String into a JSON string literal (including the quotes).
    // 把 std::String 转义为 JSON 字符串字面量（含两端引号）。
    inline std::string json_quote(const std::string& s) {
        std::string out = "\"";
        for (unsigned char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b";  break;
                case '\f': out += "\\f";  break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out += static_cast<char>(c);   // pass UTF-8 bytes through
                    }
            }
        }
        out += "\"";
        return out;
    }

    // Render a std::Number as JSON text, preserving integer shape when integral.
    // 把一个 std::Number 渲染为 JSON 文本；整型值时保留整数形态。
    inline std::string json_number_text(double d) {
        if (d == static_cast<long long>(d) &&
            std::fabs(d) < 9.0e15) {
            return std::to_string(static_cast<long long>(d));
        }
        std::ostringstream oss;
        oss << std::setprecision(15) << d;
        return oss.str();
    }

    inline std::string json_indent(int level) {
        return std::string(static_cast<size_t>(level) * 2u, ' ');
    }

    // Value-kind classification for serialization.
    // The runtime registers builtin prototypes under both the bare name and the
    // `std::`-prefixed name (e.g. "Dict" / "std::Dict"), so accept either.
    // 序列化用的值种类判定。运行时会以裸名与 `std::`-前缀名两种形式登记内建
    // 原型（如 "Dict" / "std::Dict"），故两者皆接受。
    inline bool json_is_dict(const RuntimeObjectPtr& v) {
        auto* rc = dynamic_cast<RuntimeClass*>(v.get());
        if (!rc || !rc->get_prototype()) return false;
        const std::string& pn = rc->get_prototype()->name;
        return pn == "std::Dict" || pn == "Dict";
    }
    inline bool json_is_array(const RuntimeObjectPtr& v) {
        auto* rc = dynamic_cast<RuntimeClass*>(v.get());
        if (!rc || !rc->get_prototype()) return false;
        const std::string& pn = rc->get_prototype()->name;
        return pn == "std::Array" || pn == "Array";
    }

    inline void json_dump(const RuntimeObjectPtr& v, std::string& out,
                          bool pretty, int level);

    inline void json_dump_array(const RuntimeObjectPtr& v, std::string& out,
                                bool pretty, int level) {
        auto* cls = dynamic_cast<RuntimeClass*>(v.get());
        if (!cls) { out += "[]"; return; }
        auto& env = cls->get_attributes();
        std::size_t n = rb::container_size(env);
        if (n == 0) { out += "[]"; return; }
        if (!pretty) {
            out += "[";
            for (std::size_t i = 0; i < n; ++i) {
                if (i) out += ",";
                auto it = env.find(rb::elem_key(i));
                json_dump(it != env.end() ? it->second : rb::make_object(),
                          out, false, level);
            }
            out += "]";
        } else {
            out += "[\n";
            for (std::size_t i = 0; i < n; ++i) {
                if (i) out += ",\n";
                out += json_indent(level + 1);
                auto it = env.find(rb::elem_key(i));
                json_dump(it != env.end() ? it->second : rb::make_object(),
                          out, true, level + 1);
            }
            out += "\n";
            out += json_indent(level);
            out += "]";
        }
    }

    inline void json_dump_dict(const RuntimeObjectPtr& v, std::string& out,
                               bool pretty, int level) {
        auto* cls = dynamic_cast<RuntimeClass*>(v.get());
        if (!cls) { out += "{}"; return; }
        auto& env = cls->get_attributes();
        struct KV { std::string enc; std::string key; RuntimeObjectPtr val; };
        std::vector<KV> items;
        items.reserve(env.size());
        for (const auto& [attr, val] : env) {
            if (attr.size() > rb::DICT_VALPRE.size() &&
                attr.compare(0, rb::DICT_VALPRE.size(), rb::DICT_VALPRE) == 0) {
                std::string enc = attr.substr(rb::DICT_VALPRE.size());
                auto kit = env.find(rb::DICT_KEYPRE + enc);
                std::string key = kit != env.end()
                    ? *rb::string_of(kit->second) : std::string();
                items.push_back({enc, key, val});
            }
        }
        std::sort(items.begin(), items.end(),
                  [](const KV& a, const KV& b) { return a.enc < b.enc; });
        if (items.empty()) { out += "{}"; return; }
        if (!pretty) {
            out += "{";
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i) out += ",";
                out += json_quote(items[i].key);
                out += ":";
                json_dump(items[i].val, out, false, level);
            }
            out += "}";
        } else {
            out += "{\n";
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i) out += ",\n";
                out += json_indent(level + 1);
                out += json_quote(items[i].key);
                out += ": ";
                json_dump(items[i].val, out, true, level + 1);
            }
            out += "\n";
            out += json_indent(level);
            out += "}";
        }
    }

    inline void json_dump(const RuntimeObjectPtr& v, std::string& out,
                          bool pretty, int level) {
        if (auto b = rb::boolean_of(v)) {
            out += (*b ? "true" : "false");
            return;
        }
        if (auto n = rb::number_of(v)) {
            out += json_number_text(*n);
            return;
        }
        if (auto s = rb::string_of(v)) {
            out += json_quote(*s);
            return;
        }
        if (json_is_dict(v))  { json_dump_dict(v, out, pretty, level); return; }
        if (json_is_array(v)) { json_dump_array(v, out, pretty, level); return; }
        out += "null";   // std::Object (root zero-instance) => JSON null
    }

    // --------------------------------------------------------
    // Parser: recursive descent / 递归下降解析器
    // --------------------------------------------------------
    struct JsonParser {
        const std::string& s;
        std::size_t i = 0;
        std::string err;

        explicit JsonParser(const std::string& src) : s(src) {}

        [[noreturn]] void fail(const std::string& m) {
            err = m;
            throw std::runtime_error(m);
        }

        void ws() {
            while (i < s.size()) {
                char c = s[i];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i;
                else break;
            }
        }

        void expect(const char* lit) {
            std::size_t n = std::strlen(lit);
            if (s.compare(i, n, lit) != 0)
                fail(std::string("json: expected '") + lit + "'");
            i += n;
        }

        void utf8_append(std::string& out, unsigned cp) {
            if (cp <= 0x7F) {
                out += static_cast<char>(cp);
            } else if (cp <= 0x7FF) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp <= 0xFFFF) {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        unsigned parse_hex4() {
            if (i + 4 > s.size()) fail("json: bad \\u escape");
            unsigned v = 0;
            for (int k = 0; k < 4; ++k) {
                char c = s[i++];
                v <<= 4;
                if (c >= '0' && c <= '9')      v |= static_cast<unsigned>(c - '0');
                else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
                else fail("json: bad \\u escape digit");
            }
            return v;
        }

        std::string parse_string() {
            ++i;   // consume opening quote
            std::string out;
            while (i < s.size()) {
                char c = s[i++];
                if (c == '"') return out;
                if (c == '\\') {
                    if (i >= s.size()) fail("json: bad escape in string");
                    char e = s[i++];
                    switch (e) {
                        case '"': out += '"'; break;
                        case '\\': out += '\\'; break;
                        case '/': out += '/'; break;
                        case 'b': out += '\b'; break;
                        case 'f': out += '\f'; break;
                        case 'n': out += '\n'; break;
                        case 'r': out += '\r'; break;
                        case 't': out += '\t'; break;
                        case 'u': {
                            unsigned cp = parse_hex4();
                            if (cp >= 0xD800 && cp <= 0xDBFF) {
                                if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                                    i += 2;
                                    unsigned lo = parse_hex4();
                                    if (lo >= 0xDC00 && lo <= 0xDFFF)
                                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                    else
                                        cp = 0xFFFD;
                                } else {
                                    cp = 0xFFFD;
                                }
                            }
                            utf8_append(out, cp);
                            break;
                        }
                        default:
                            fail(std::string("json: invalid escape '\\") + e + "'");
                    }
                } else {
                    out += c;
                }
            }
            fail("json: unterminated string");
        }

        RuntimeObjectPtr parse_number() {
            std::size_t start = i;
            bool is_int = true;
            if (i < s.size() && s[i] == '-') ++i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
            if (i < s.size() && s[i] == '.') {
                is_int = false; ++i;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
            }
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                is_int = false; ++i;
                if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
            }
            if (i == start) fail("json: invalid number");
            std::string num = s.substr(start, i - start);
            try {
                double d = std::stod(num);
                return rb::make_number(d, is_int);
            } catch (...) {
                fail("json: invalid number");
            }
        }

        RuntimeObjectPtr parse_array() {
            ++i;   // consume '['
            std::vector<RuntimeObjectPtr> items;
            ws();
            if (i < s.size() && s[i] == ']') { ++i; return rb::make_array(items); }
            for (;;) {
                items.push_back(parse_value());
                ws();
                if (i >= s.size()) fail("json: unterminated array");
                char c = s[i++];
                if (c == ']') break;
                if (c != ',') fail("json: expected ',' or ']' in array");
                ws();
            }
            return rb::make_array(items);
        }

        RuntimeObjectPtr parse_object() {
            ++i;   // consume '{'
            auto d = rb::make_dict();
            auto* dcls = dynamic_cast<RuntimeClass*>(d.get());
            auto& a = dcls->get_attributes();
            std::size_t count = 0;
            auto put = [&](const std::string& k, RuntimeObjectPtr v) {
                auto keyObj = rb::make_string(k);
                std::string slot = rb::DICT_VALPRE + rb::encode_key(keyObj);
                a[rb::DICT_KEYPRE + slot.substr(rb::DICT_VALPRE.size())] = keyObj;
                a[slot] = v;
            };
            ws();
            if (i < s.size() && s[i] == '}') {
                ++i; a[rb::SIZE_KEY] = rb::cap_number(0.0, true); return d;
            }
            for (;;) {
                ws();
                if (i >= s.size() || s[i] != '"') fail("json: expected string key");
                std::string key = parse_string();
                ws();
                if (i >= s.size() || s[i] != ':') fail("json: expected ':'");
                ++i;
                RuntimeObjectPtr val = parse_value();
                put(key, val);
                ++count;
                ws();
                if (i >= s.size()) fail("json: unterminated object");
                char c = s[i++];
                if (c == '}') break;
                if (c != ',') fail("json: expected ',' or '}' in object");
            }
            a[rb::SIZE_KEY] = rb::cap_number(static_cast<double>(count), true);
            return d;
        }

        RuntimeObjectPtr parse_value() {
            ws();
            if (i >= s.size()) fail("json: unexpected end of input");
            char c = s[i];
            if (c == '{') return parse_object();
            if (c == '[') return parse_array();
            if (c == '"') return rb::make_string(parse_string());
            if (c == 't') { expect("true");  return rb::make_boolean(true); }
            if (c == 'f') { expect("false"); return rb::make_boolean(false); }
            if (c == 'n') { expect("null");  return rb::make_object(); }
            if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
            fail(std::string("json: unexpected character '") + c + "'");
        }
    };

    // --------------------------------------------------------
    // Native methods / 原生方法
    // --------------------------------------------------------

    inline rt_basic::Callable method_json_parse() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) {
                    return rb::list_of({rb::native_error(
                        "json.parse requires a std::String")});
                }
                JsonParser p(*text);
                try {
                    auto v = p.parse_value();
                    p.ws();
                    if (p.i != p.s.size()) {
                        return rb::list_of({rb::native_error(
                            "json.parse: trailing characters after JSON value")});
                    }
                    return rb::list_of({v});
                } catch (...) {
                    return rb::list_of({rb::native_error(
                        std::string("json.parse: ") +
                        (p.err.empty() ? "invalid JSON" : p.err))});
                }
            },
            rb::make_sign("parse",
                {{"text", "std::String"}}, {{"value", "std::Object"}})
        );
    }

    inline rt_basic::Callable method_json_stringify() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto v = rb::para_at(paras, 0);
                if (!v) {
                    return rb::list_of({rb::native_error(
                        "json.stringify requires a value")});
                }
                std::string out;
                json_dump(v, out, false, 0);
                return rb::list_of({rb::make_string(out)});
            },
            rb::make_sign("stringify",
                {{"value", "std::Object"}}, {{"text", "std::String"}})
        );
    }

    inline rt_basic::Callable method_json_pretty() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto v = rb::para_at(paras, 0);
                if (!v) {
                    return rb::list_of({rb::native_error(
                        "json.pretty requires a value")});
                }
                std::string out;
                json_dump(v, out, true, 0);
                return rb::list_of({rb::make_string(out)});
            },
            rb::make_sign("pretty",
                {{"value", "std::Object"}}, {{"text", "std::String"}})
        );
    }

    // ---- registration / 登记 ----
    inline void init_json_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(
            ::stdRT.getcls("Object")
        );
        proto->set_method("parse",     method_json_parse());
        proto->set_method("stringify", method_json_stringify());
        proto->set_method("pretty",    method_json_pretty());

        runtime::Prototypes p;
        // Centralized registration MUST carry the package index `json::` (the
        // "集中的必须加" rule): this is what makes `json` a known set and lets
        // `json::Json` resolve. The NAME itself is `Json`; the `::`-prefixed part
        // is only a package locator, not part of the name. The module's own
        // lib/json.synl declares the preset object with the BARE name `Json`.
        // 集中登记须带包索引 `json::`（「集中的必须加」）：这才能使 `json` 成为已知集、
        // `json::Json` 可解析。名字本身仍是 `Json`，`::` 前缀只是包定位符，而非名字的
        // 一部分。模块自身的 lib/json.synl 以裸名 `Json` 声明预置对象。
        p.regcls("Json", proto);
        ::stdRT.add_protos(p);
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("json", &init_json_stdlib), true);

} // namespace rt_lib_json
