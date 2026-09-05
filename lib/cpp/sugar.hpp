// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/sugar.hpp
//
// Standard library: sugar (C++-backed backend).
// 标准库：sugar（C++ 底层实现）。
//
// A small bag of syntactic "sugar" that the core interpreter does not
// provide natively. The first (and currently only) class is `Infix`:
// 一个核心解释器本身不原生提供的小型「语法糖」包。目前（也是唯一）的
// 类是 `Infix`：
//
//   -(sugar::Infix("1+(2-3)*(3+5)") e);   // construct with an expression
//   e.env(myDict);                         // bind variables from a std::Dict
//   out << (e.parse());                    // evaluate -> std::Number (-7)
//
// `Infix.parse()` evaluates an arithmetic expression (+ - * / %) with
// parentheses and unary minus. Identifiers are resolved against the
// environment (`@env`) dict. A missing environment or an unresolved
// variable raises a normal, duck-typed runtime error attributed to the
// `parse` method (not a silent poison value, not a process crash).
// `Infix.parse()` 计算含括号与一元负号的算术表达式（+ - * / %）。标识符
// 从环境（`@env`）字典中解析。环境缺失或变量未解析时，会抛出一个归属
// `parse` 方法的、鸭子式的普通运行时错误（而非静默毒水或进程崩溃）。
//
// State for each instance lives in a process-wide registry keyed by a stable
// instance id (assigned lazily on first use), so the C++ methods never fight
// the interpreter's attribute model. Self-registered under "sugar".
// 每个实例的状态存于进程级注册表（按惰性分配的实例 id 索引），故 C++ 方法
// 不与解释器的属性模型冲突。以 "sugar" 自注册。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cctype>
#include <cmath>
#include <stdexcept>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API
#include "../../src/exception_throw.hpp"  // diag:: for call-stack frames

namespace rt_lib_sugar {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // --------------------------------------------------------
    // Shared state registry / 共享状态注册表
    // --------------------------------------------------------
    struct InfixState {
        std::string expr;            // the expression text
        RuntimeObjectPtr env;        // std::Dict environment, or null
    };

    static std::recursive_mutex g_mux;
    static long long             g_id = 0;
    static std::unordered_map<long long, InfixState> g_infix;

    // Lazily obtain a stable id for this instance (stored back on `env`).
    // 惰性取得本实例的稳定 id（回写至 env）。
    inline long long instance_id(rt_basic::InstanceMap& env) {
        long long id = 0;
        auto it = env.find("id");
        if (it != env.end()) {
            auto v = rb::number_of(it->second);
            if (v) id = static_cast<long long>(*v);
        }
        if (id <= 0) {
            std::lock_guard<std::recursive_mutex> lk(g_mux);
            id = ++g_id;
            env["id"] = rb::make_number(static_cast<double>(id));
        }
        return id;
    }

    inline InfixState& state_of(rt_basic::InstanceMap& env) {
        long long id = instance_id(env);
        std::lock_guard<std::recursive_mutex> lk(g_mux);
        return g_infix[id];
    }

    // --------------------------------------------------------
    // Expression evaluator / 表达式求值器
    // --------------------------------------------------------

    // RAII guard: push a call-stack frame so any error raised while evaluating
    // is attributed to the `parse` method at its call site.
    // RAII 守卫：压入执行栈帧，使求值期间抛出的任何错误都归属到调用点的
    // `parse` 方法。
    struct ParseFrameGuard {
        ParseFrameGuard() {
            diag::call_stack().push_back({
                "in method 'parse'",
                diag::source_file(),
                diag::cur_locus().line,
                diag::cur_locus().col
            });
        }
        ~ParseFrameGuard() {
            if (!diag::call_stack().empty()) diag::call_stack().pop_back();
        }
    };

    struct InfixParser {
        const std::string& src;
        std::size_t pos = 0;
        RuntimeObjectPtr env;   // std::Dict environment, or null

        explicit InfixParser(const std::string& s, RuntimeObjectPtr e)
            : src(s), env(std::move(e)) {}

        [[noreturn]] void err(const std::string& m) {
            throw rb::native_error("parse: " + m);
        }

        void skip_ws() {
            while (pos < src.size() && std::isspace(
                    static_cast<unsigned char>(src[pos]))) {
                ++pos;
            }
        }

        double resolve_var(const std::string& name) {
            if (!env) {
                err("undefined variable '" + name
                    + "' (no environment set; call @env(dict) first)");
            }
            auto* cls = dynamic_cast<RuntimeClass*>(env.get());
            if (!cls) err("environment is not a std::Dict");
            auto& am = cls->get_attributes();
            auto slot = rb::DICT_VALPRE + rb::encode_key(rb::make_string(name));
            auto it = am.find(slot);
            if (it == am.end()) {
                err("undefined variable '" + name
                    + "' (not present in the environment dict)");
            }
            auto num = rb::number_of(it->second);
            if (!num) {
                err("variable '" + name + "' is not a std::Number");
            }
            return *num;
        }

        double parse_expr() {
            double v = parse_term();
            for (;;) {
                skip_ws();
                if (pos >= src.size()) break;
                char c = src[pos];
                if (c == '+') { ++pos; v += parse_term(); }
                else if (c == '-') { ++pos; v -= parse_term(); }
                else break;
            }
            return v;
        }

        double parse_term() {
            double v = parse_factor();
            for (;;) {
                skip_ws();
                if (pos >= src.size()) break;
                char c = src[pos];
                if (c == '*') { ++pos; v *= parse_factor(); }
                else if (c == '/') {
                    ++pos;
                    double d = parse_factor();
                    if (d == 0.0) err("division by zero");
                    v /= d;
                }
                else if (c == '%') {
                    ++pos;
                    double d = parse_factor();
                    if (d == 0.0) err("modulo by zero");
                    v = std::fmod(v, d);
                }
                else break;
            }
            return v;
        }

        double parse_factor() {
            skip_ws();
            if (pos >= src.size()) err("unexpected end of expression");
            if (src[pos] == '-') { ++pos; return -parse_factor(); }
            if (src[pos] == '+') { ++pos; return parse_factor(); }
            return parse_primary();
        }

        double parse_primary() {
            skip_ws();
            if (pos >= src.size()) err("unexpected end of expression");
            // Parenthesised sub-expression.
            // 括号子表达式。
            if (src[pos] == '(') {
                ++pos;
                double v = parse_expr();
                skip_ws();
                if (pos >= src.size() || src[pos] != ')') {
                    err("missing closing ')'");
                }
                ++pos;
                return v;
            }
            // Number literal (integer or decimal).
            // 数字字面量（整数或小数）。
            if (std::isdigit(static_cast<unsigned char>(src[pos]))
                    || src[pos] == '.') {
                std::size_t start = pos;
                while (pos < src.size()
                        && (std::isdigit(
                                static_cast<unsigned char>(src[pos]))
                            || src[pos] == '.')) {
                    ++pos;
                }
                try {
                    return std::stod(src.substr(start, pos - start));
                } catch (...) {
                    err("invalid number literal");
                }
            }
            // Identifier -> environment lookup.
            // 标识符 -> 查环境。
            if (std::isalpha(static_cast<unsigned char>(src[pos]))
                    || src[pos] == '_') {
                std::size_t start = pos;
                while (pos < src.size()
                        && (std::isalnum(
                                static_cast<unsigned char>(src[pos]))
                            || src[pos] == '_')) {
                    ++pos;
                }
                return resolve_var(src.substr(start, pos - start));
            }
            err("unexpected character '" + std::string(1, src[pos])
                + "' in expression");
        }

        RuntimeObjectPtr evaluate() {
            if (src.empty()) {
                err("no expression set; construct with a string or call @set");
            }
            double result = parse_expr();
            skip_ws();
            if (pos != src.size()) err("unexpected token after expression");
            bool integral = std::isfinite(result)
                            && (std::floor(result) == result);
            return rb::make_number(result, integral);
        }
    };

    // --------------------------------------------------------
    // Methods / 方法
    // --------------------------------------------------------

    // Constructor: Infix("expr") stores the expression text.
    // 构造函数：Infix("expr") 保存表达式文本。
    inline rt_basic::Callable method_infix_construct() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto& st = state_of(env);
                auto arg = rb::para_at(paras, 0);
                auto s = arg ? rb::string_of(arg) : std::nullopt;
                if (arg && !s) {
                    rb::native_error(
                        "Infix constructor expects a std::String expression");
                }
                st.expr = s ? *s : "";
                st.env = nullptr;
                return rb::empty_result();
            },
            rb::make_sign("::", {{"expr", "std::String"}}, {})
        );
    }

    // @set(expr) —— replace the expression text.
    // @set(expr) —— 替换表达式文本。
    inline rt_basic::Callable method_infix_set() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto& st = state_of(env);
                auto arg = rb::para_at(paras, 0);
                auto s = arg ? rb::string_of(arg) : std::nullopt;
                if (!s) {
                    rb::native_error("@set expects a std::String expression");
                }
                st.expr = *s;
                return rb::empty_result();
            },
            rb::make_sign("set", {{"expr", "std::String"}}, {})
        );
    }

    // @env(dict) —— bind variables from a std::Dict.
    // @env(dict) —— 从 std::Dict 绑定变量。
    inline rt_basic::Callable method_infix_env() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto& st = state_of(env);
                auto arg = rb::para_at(paras, 0);
                if (!arg) {
                    rb::native_error("@env expects a std::Dict environment");
                }
                auto* cls = dynamic_cast<RuntimeClass*>(arg.get());
                auto proto = cls ? cls->get_prototype() : nullptr;
                if (!proto || proto->name != "Dict") {
                    rb::native_error("@env expects a std::Dict environment");
                }
                st.env = arg;
                return rb::empty_result();
            },
            rb::make_sign("env", {{"env", "std::Dict"}}, {})
        );
    }

    // @parse() —— evaluate and return a std::Number.
    // @parse() —— 求值并返回 std::Number。
    inline rt_basic::Callable method_infix_parse() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                ParseFrameGuard fg;   // attribute any error to `parse`
                auto& st = state_of(env);
                InfixParser parser(st.expr, st.env);
                return rb::list_of({parser.evaluate()});
            },
            rb::make_sign("parse", {}, {{"value", "std::Number"}})
        );
    }
    inline rt_basic::Callable method_infix_dispose() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                long long id = instance_id(env);
                std::lock_guard<std::recursive_mutex> lk(g_mux);
                g_infix.erase(id);
                return rb::empty_result();
            },
            rb::make_sign("dispose", {}, {})
        );
    }

    // ---- registration / 登记 ----
    inline void init_sugar_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(
            ::stdRT.getcls("Object"));
        proto->set_method("::",   method_infix_construct());
        proto->set_method("set",  method_infix_set());
        proto->set_method("env",  method_infix_env());
        proto->set_method("parse", method_infix_parse());
        proto->set_method("dispose", method_infix_dispose());
        // D5: reclaim the g_infix registry entry when the Infix object is
        // collected (avoids the unbounded state-table leak from the audit).
        // D5：Infix 对象被回收时取回 g_infix 注册表条目（堵塞表泄漏）。
        proto->on_release = [](rt_basic::InstanceMap& env) {
            long long id = instance_id(env);
            std::lock_guard<std::recursive_mutex> lk(g_mux);
            g_infix.erase(id);
        };
        runtime::Prototypes p; p.regcls("Infix", proto); ::stdRT.add_protos(p);
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("sugar", &init_sugar_stdlib), true);

} // namespace rt_lib_sugar
