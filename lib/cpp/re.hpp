// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/re.hpp
//
// Standard library: re — regular-expression matching (C++-backed).
// 标准库：re —— 正则表达式匹配（C++ 底层实现）。
//
// Provides a `Pattern` class (compiled regex, ECMAScript grammar) and a
// `Match` result object, plus static convenience methods on the `re` class
// that compile on the fly. Self-registered under "re".
// 提供 `Pattern` 类（编译后的正则，ECMAScript 文法）与 `Match` 结果对象，
// 以及 `re` 类上的静态便捷方法（即时编译）。以 "re" 自注册。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <regex>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_re {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // Forward declaration so make_match can call build_groups before its definition.
    // 前向声明：make_match 在 build_groups 定义之前即可调用它。
    inline RuntimeObjectPtr build_groups(const std::vector<RuntimeObjectPtr>& items);

    // Build a `Match` object from a std::smatch (or empty when no match).
    // 由 std::smatch 构造 `Match` 对象（无匹配时为空）。
    inline RuntimeObjectPtr make_match(const std::string& haystack,
                                       const std::smatch& m) {
        auto match = ::stdRT.make("Match");
        auto* cls = dynamic_cast<RuntimeClass*>(match.get());
        if (!cls) return match;
        auto& am = cls->get_attributes();
        if (m.empty()) {
            am["matched"] = rb::make_boolean(false);
            am["text"]    = rb::make_string("");
            am["groups"]  = build_groups({});
            return match;
        }
        std::vector<RuntimeObjectPtr> groups;
        for (auto& sub : m) {
            groups.push_back(rb::make_string(sub.str()));
        }
        am["matched"] = rb::make_boolean(true);
        am["text"]    = rb::make_string(m.str());
        am["groups"]  = build_groups(groups);
        am["start"]   = rb::make_number(static_cast<double>(m.position()), true);
        am["end"]     = rb::make_number(static_cast<double>(m.position() + m.length()), true);
        return match;
    }

    // Helper: build an Array runtime object (mirrors structs.hpp build_array).
    // 构造 Array 运行时对象（与 structs.hpp 的 build_array 同构）。
    inline RuntimeObjectPtr build_groups(const std::vector<RuntimeObjectPtr>& items) {
        auto arr = ::stdRT.make("Array");
        auto* cls = dynamic_cast<RuntimeClass*>(arr.get());
        auto& am = cls->get_attributes();
        for (std::size_t i = 0; i < items.size(); ++i) {
            am[rb::elem_key(i)] = items[i];
        }
        rb::set_container_size(am, items.size());
        return arr;
    }

    // Compile a pattern, failing to poison on a bad regex.
    // 编译正则表达式，非法时降级为毒水。
    inline std::shared_ptr<std::regex> compile(const std::string& pat, std::string& err) {
        try {
            return std::make_shared<std::regex>(pat, std::regex::ECMAScript);
        } catch (const std::regex_error& e) {
            err = std::string("re: invalid pattern: ") + e.what();
            return nullptr;
        }
    }

    // ---- $Pattern methods / 方法 ----
    // A Pattern keeps its compiled regex in a process-wide cache keyed by id.
    // Pattern 把编译后的正则存于进程级缓存（按 id 索引）。
    static std::recursive_mutex g_re_mux;
    static long long  g_re_id = 0;
    static std::unordered_map<long long, std::shared_ptr<std::regex>> g_patterns;

    inline long long pattern_id(rt_basic::InstanceMap& env) {
        long long id = 0;
        auto it = env.find("id");
        if (it != env.end()) {
            auto v = rb::number_of(it->second);
            if (v) id = static_cast<long long>(*v);
        }
        // Fall back to #value: the capsule that survives publish/receive, so a
        // `-(re::Pattern p) << r.compile(...)` keeps its compiled regex.
        // 回退到 #value：经公布/接收存活的胶囊，使
        // `-(re::Pattern p) << r.compile(...)` 保留已编译正则。
        if (id <= 0) {
            auto vit = env.find("#value");
            if (vit != env.end()) {
                auto v = rb::number_of(vit->second);
                if (v) id = static_cast<long long>(*v);
            }
        }
        if (id <= 0) {
            std::lock_guard<std::recursive_mutex> lk(g_re_mux);
            id = ++g_re_id;
            env["id"] = rb::make_number(static_cast<double>(id));
        }
        return id;
    }

    inline rt_basic::Callable method_pattern_match() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error("pattern.match requires text")});
                std::shared_ptr<std::regex> re;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                    re = g_patterns[pattern_id(env)];
                }
                if (!re) return rb::list_of({rb::native_error("pattern not compiled")});
                std::smatch m;
                bool ok = std::regex_match(*text, m, *re);
                return rb::list_of({make_match(*text, ok ? m : std::smatch())});
            },
            rb::make_sign("match", {{"text", "std::String"}}, {{"result", "re::Match"}})
        );
    }
    inline rt_basic::Callable method_pattern_search() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error("pattern.search requires text")});
                std::shared_ptr<std::regex> re;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                    re = g_patterns[pattern_id(env)];
                }
                if (!re) return rb::list_of({rb::native_error("pattern not compiled")});
                std::smatch m;
                bool ok = std::regex_search(*text, m, *re);
                return rb::list_of({make_match(*text, ok ? m : std::smatch())});
            },
            rb::make_sign("search", {{"text", "std::String"}}, {{"result", "re::Match"}})
        );
    }
    inline rt_basic::Callable method_pattern_findall() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error("pattern.findall requires text")});
                std::shared_ptr<std::regex> re;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                    re = g_patterns[pattern_id(env)];
                }
                if (!re) return rb::list_of({rb::native_error("pattern not compiled")});
                std::vector<RuntimeObjectPtr> out;
                auto begin = text->cbegin();
                auto end = text->cend();
                std::smatch m;
                while (std::regex_search(begin, end, m, *re)) {
                    out.push_back(rb::make_string(m.str()));
                    begin = m.suffix().first;
                }
                return rb::list_of({build_groups(out)});
            },
            rb::make_sign("findall", {{"text", "std::String"}}, {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_pattern_replace() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                auto repl = rb::string_of(rb::para_at(paras, 1));
                if (!text || !repl) {
                    return rb::list_of({rb::native_error("pattern.replace requires text and replacement")});
                }
                std::shared_ptr<std::regex> re;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                    re = g_patterns[pattern_id(env)];
                }
                if (!re) return rb::list_of({rb::native_error("pattern not compiled")});
                return rb::list_of({rb::make_string(
                    std::regex_replace(*text, *re, *repl))});
            },
            rb::make_sign(
                "replace", {{"text", "std::String"}, {"repl", "std::String"}}, {{"out", "std::String"}})
        );
    }
    inline rt_basic::Callable method_pattern_split() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error("pattern.split requires text")});
                std::shared_ptr<std::regex> re;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                    re = g_patterns[pattern_id(env)];
                }
                if (!re) return rb::list_of({rb::native_error("pattern not compiled")});
                std::vector<RuntimeObjectPtr> out;
                auto begin = text->cbegin();
                auto end = text->cend();
                std::smatch m;
                std::size_t last = 0;
                while (std::regex_search(begin, end, m, *re)) {
                    out.push_back(rb::make_string(text->substr(last, m.position())));
                    last += m.position() + m.length();
                    begin = m.suffix().first;
                }
                out.push_back(rb::make_string(text->substr(last)));
                return rb::list_of({build_groups(out)});
            },
            rb::make_sign("split", {{"text", "std::String"}}, {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_pattern_test() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error("pattern.test requires text")});
                std::shared_ptr<std::regex> re;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                    re = g_patterns[pattern_id(env)];
                }
                if (!re) return rb::list_of({rb::native_error("pattern not compiled")});
                return rb::list_of({rb::make_boolean(
                    std::regex_search(*text, *re))});
            },
            rb::make_sign("test", {{"text", "std::String"}}, {{"ok", "std::Boolean"}})
        );
    }

    // ---- $re static convenience methods / 静态便捷方法 ----
    inline rt_basic::Callable method_re_compile() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                if (!pat) return rb::list_of({rb::native_error("re.compile requires a pattern")});
                std::string err;
                auto re = compile(*pat, err);
                if (!re) return rb::list_of({rb::native_error(err)});
                // Cache and hand back a Pattern object.
                // 缓存并返回 Pattern 对象。
                auto pobj = ::stdRT.make("Pattern");
                long long id = 0;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                    id = ++g_re_id;
                    g_patterns[id] = re;
                }
                auto& pam = dynamic_cast<RuntimeClass*>(pobj.get())->get_attributes();
                pam["id"]     = rb::make_number(static_cast<double>(id));
                // Stash id in #value so it survives publish/receive.
                // 把 id 存进 #value，使之能经公布/接收存活。
                pam["#value"] = rb::make_number(static_cast<double>(id));
                return rb::list_of({pobj});
            },
            rb::make_sign("compile", {{"pattern", "std::String"}}, {{"pat", "re::Pattern"}})
        );
    }
    // Run a fresh regex on the fly and return the appropriate shape.
    // 即时编译并返回对应形态。
    inline rt_basic::Callable method_re_match() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                if (!pat || !text) return rb::list_of({rb::native_error("re.match requires pattern and text")});
                std::string err; auto re = compile(*pat, err);
                if (!re) return rb::list_of({rb::native_error(err)});
                std::smatch m; bool ok = std::regex_match(*text, m, *re);
                return rb::list_of({make_match(*text, ok ? m : std::smatch())});
            },
            rb::make_sign(
                "match", {{"pattern", "std::String"}, {"text", "std::String"}}, {{"result", "re::Match"}})
        );
    }
    inline rt_basic::Callable method_re_search() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                if (!pat || !text) return rb::list_of({rb::native_error("re.search requires pattern and text")});
                std::string err; auto re = compile(*pat, err);
                if (!re) return rb::list_of({rb::native_error(err)});
                std::smatch m; bool ok = std::regex_search(*text, m, *re);
                return rb::list_of({make_match(*text, ok ? m : std::smatch())});
            },
            rb::make_sign(
                "search", {{"pattern", "std::String"}, {"text", "std::String"}}, {{"result", "re::Match"}})
        );
    }
    inline rt_basic::Callable method_re_findall() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                if (!pat || !text) return rb::list_of({rb::native_error("re.findall requires pattern and text")});
                std::string err; auto re = compile(*pat, err);
                if (!re) return rb::list_of({rb::native_error(err)});
                std::vector<RuntimeObjectPtr> out;
                auto begin = text->cbegin(), end = text->cend();
                std::smatch m;
                while (std::regex_search(begin, end, m, *re)) {
                    out.push_back(rb::make_string(m.str()));
                    begin = m.suffix().first;
                }
                return rb::list_of({build_groups(out)});
            },
            rb::make_sign(
                "findall", {{"pattern", "std::String"}, {"text", "std::String"}}, {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_re_replace() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                auto repl = rb::string_of(rb::para_at(paras, 2));
                if (!pat || !text || !repl) {
                    return rb::list_of({rb::native_error("re.replace requires pattern, text, replacement")});
                }
                std::string err; auto re = compile(*pat, err);
                if (!re) return rb::list_of({rb::native_error(err)});
                return rb::list_of({rb::make_string(std::regex_replace(*text, *re, *repl))});
            },
            rb::make_sign(
                "replace",
                {{"pattern", "std::String"}, {"text", "std::String"}, {"repl", "std::String"}},
                {{"out", "std::String"}})
        );
    }
    inline rt_basic::Callable method_re_split() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                if (!pat || !text) return rb::list_of({rb::native_error("re.split requires pattern and text")});
                std::string err; auto re = compile(*pat, err);
                if (!re) return rb::list_of({rb::native_error(err)});
                std::vector<RuntimeObjectPtr> out;
                auto begin = text->cbegin(), end = text->cend();
                std::smatch m; std::size_t last = 0;
                while (std::regex_search(begin, end, m, *re)) {
                    out.push_back(rb::make_string(text->substr(last, m.position())));
                    last += m.position() + m.length();
                    begin = m.suffix().first;
                }
                out.push_back(rb::make_string(text->substr(last)));
                return rb::list_of({build_groups(out)});
            },
            rb::make_sign(
                "split", {{"pattern", "std::String"}, {"text", "std::String"}}, {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_re_test() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                if (!pat || !text) return rb::list_of({rb::native_error("re.test requires pattern and text")});
                std::string err; auto re = compile(*pat, err);
                if (!re) return rb::list_of({rb::native_error(err)});
                return rb::list_of({rb::make_boolean(std::regex_search(*text, *re))});
            },
            rb::make_sign(
                "test", {{"pattern", "std::String"}, {"text", "std::String"}}, {{"ok", "std::Boolean"}})
        );
    }

    // ---- registration / 登记 ----
    inline void init_re_stdlib() {
        // Pattern / 编译后的正则
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            proto->set_method("match",    method_pattern_match());
            proto->set_method("search",   method_pattern_search());
            proto->set_method("findall",  method_pattern_findall());
            proto->set_method("replace",  method_pattern_replace());
            proto->set_method("split",    method_pattern_split());
            proto->set_method("test",     method_pattern_test());
            runtime::Prototypes p; p.regcls("Pattern", proto); ::stdRT.add_protos(p);
        }
        // Match (result object) / 匹配结果
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            // Match exposes matched/text/groups/start/end as attributes;
            // simple read-back methods are supplied for explicit access.
            proto->set_attribute("matched", rb::make_boolean(false));
            proto->set_attribute("text",    rb::make_string(""));
            proto->set_attribute("groups",  build_groups({}));
            proto->set_attribute("start",   rb::make_number(0, true));
            proto->set_attribute("end",     rb::make_number(0, true));
            runtime::Prototypes p; p.regcls("Match", proto); ::stdRT.add_protos(p);
        }
        // re (static convenience) / 静态便捷入口
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            proto->set_method("compile",  method_re_compile());
            proto->set_method("match",    method_re_match());
            proto->set_method("search",   method_re_search());
            proto->set_method("findall",  method_re_findall());
            proto->set_method("replace",  method_re_replace());
            proto->set_method("split",    method_re_split());
            proto->set_method("test",     method_re_test());
            runtime::Prototypes p; p.regcls("Re", proto); ::stdRT.add_protos(p);
        }
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("re", &init_re_stdlib), true);

} // namespace rt_lib_re
