// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/maths.hpp
//
// Standard library: maths (C++-backed backend).
// 标准库：maths（C++ 底层实现）。
//
// Provides scalar math on std::Number values. The C++ twin of
// lib/maths.synl (the Synth-OOP interface): maths.synl declares the class
// shape, this header supplies the real behavior. Self-registered under the
// name "maths" so the interpreter can bring it online on `&maths;`.
// 提供 std::Number 上的标量数学运算，是 lib/maths.synl（Synth-OOP 接口）
// 的 C++ 孪生体：maths.synl 声明类形态，本文件提供真实行为。以 "maths"
// 自注册，使解释器在 `&maths;` 时上线该类。
// ============================================================

#pragma once

#include <cmath>
#include <string>
#include <vector>
#include <memory>
#include <random>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_maths {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // ---- helpers / 辅助 ----

    inline rt_basic::InstanceListPtr math1(
        const rt_basic::InstanceListPtr& paras,
        double (*fn)(double),
        bool integral = false
    ) {
        auto x = rb::number_of(rb::para_at(paras, 0));
        if (!x) {
            return rb::list_of({rb::native_error(
                "maths: argument must be a std::Number")});
        }
        return rb::list_of({rb::make_number(fn(*x), integral)});
    }

    inline rt_basic::InstanceListPtr math2(
        const rt_basic::InstanceListPtr& paras,
        double (*fn)(double, double),
        bool integral = false
    ) {
        auto a = rb::number_of(rb::para_at(paras, 0));
        auto b = rb::number_of(rb::para_at(paras, 1));
        if (!a || !b) {
            return rb::list_of({rb::native_error(
                "maths: both arguments must be std::Number")});
        }
        return rb::list_of({rb::make_number(fn(*a, *b), integral)});
    }

    // ---- native methods / 原生方法 ----

    inline rt_basic::Callable method_maths_abs() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math1(p, [](double x){ return std::fabs(x); }, true);
            },
            rb::make_sign("abs", {{"x", "std::Number"}}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_sqrt() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math1(p, [](double x){ return std::sqrt(x); });
            },
            rb::make_sign("sqrt", {{"x", "std::Number"}}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_pow() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math2(p, [](double a, double b){ return std::pow(a, b); });
            },
            rb::make_sign("pow",
                {{"base", "std::Number"}, {"exp", "std::Number"}},
                {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_floor() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math1(p, [](double x){ return std::floor(x); }, true);
            },
            rb::make_sign("floor", {{"x", "std::Number"}}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_ceil() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math1(p, [](double x){ return std::ceil(x); }, true);
            },
            rb::make_sign("ceil", {{"x", "std::Number"}}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_round() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math1(p, [](double x){ return std::round(x); }, true);
            },
            rb::make_sign("round", {{"x", "std::Number"}}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_sin() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math1(p, [](double x){ return std::sin(x); });
            },
            rb::make_sign("sin", {{"x", "std::Number"}}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_cos() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math1(p, [](double x){ return std::cos(x); });
            },
            rb::make_sign("cos", {{"x", "std::Number"}}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_tan() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math1(p, [](double x){ return std::tan(x); });
            },
            rb::make_sign("tan", {{"x", "std::Number"}}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_log() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math1(p, [](double x){ return std::log(x); });
            },
            rb::make_sign("log", {{"x", "std::Number"}}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_log10() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math1(p, [](double x){ return std::log10(x); });
            },
            rb::make_sign("log10", {{"x", "std::Number"}}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_exp() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math1(p, [](double x){ return std::exp(x); });
            },
            rb::make_sign("exp", {{"x", "std::Number"}}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_mod() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math2(p, [](double a, double b){ return std::fmod(a, b); });
            },
            rb::make_sign("mod",
                {{"a", "std::Number"}, {"b", "std::Number"}},
                {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_min() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math2(p, [](double a, double b){
                    return a < b ? a : b;
                }, true);
            },
            rb::make_sign("min",
                {{"a", "std::Number"}, {"b", "std::Number"}},
                {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_max() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr p) {
                return math2(p, [](double a, double b){
                    return a > b ? a : b;
                }, true);
            },
            rb::make_sign("max",
                {{"a", "std::Number"}, {"b", "std::Number"}},
                {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_pi() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr) {
                return rb::list_of({rb::make_number(
                    std::atan(1.0) * 4.0, false)});
            },
            rb::make_sign("pi", {}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_e() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr) {
                return rb::list_of({rb::make_number(
                    std::exp(1.0), false)});
            },
            rb::make_sign("e", {}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_random() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr) {
                static std::mt19937_64 rng(
                    std::random_device{}());
                static std::uniform_real_distribution<double> dist(0.0, 1.0);
                return rb::list_of({rb::make_number(dist(rng), false)});
            },
            rb::make_sign("random", {}, {{"r", "std::Number"}})
        );
    }

    // ---- registration / 登记 ----
    inline void init_maths_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(
            ::stdRT.getcls("Object")
        );
        proto->set_method("abs",    method_maths_abs());
        proto->set_method("sqrt",   method_maths_sqrt());
        proto->set_method("pow",    method_maths_pow());
        proto->set_method("floor",  method_maths_floor());
        proto->set_method("ceil",   method_maths_ceil());
        proto->set_method("round",  method_maths_round());
        proto->set_method("sin",    method_maths_sin());
        proto->set_method("cos",    method_maths_cos());
        proto->set_method("tan",    method_maths_tan());
        proto->set_method("log",    method_maths_log());
        proto->set_method("log10",  method_maths_log10());
        proto->set_method("exp",    method_maths_exp());
        proto->set_method("mod",    method_maths_mod());
        proto->set_method("min",    method_maths_min());
        proto->set_method("max",    method_maths_max());
        proto->set_method("pi",     method_maths_pi());
        proto->set_method("e",      method_maths_e());
        proto->set_method("random", method_maths_random());

        runtime::Prototypes p;
        p.regcls("Maths", proto);
        ::stdRT.add_protos(p);
    }

    // Self-register so the interpreter can initialize this library.
    // 自注册，使解释器能够初始化本库。
    inline bool _registered =
        (rt_builtin::register_native_lib("maths", &init_maths_stdlib), true);

} // namespace rt_lib_maths
