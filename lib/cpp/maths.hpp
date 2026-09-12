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
#include <mutex>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_maths {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // ---- thread-safe RNG (fixes the industrialization audit gap: random was
    //      non-reproducible and not thread-safe) / 线程安全 RNG（修复审计缺口：
    //      原 random 不可复现且非线程安全）----
    inline std::mt19937_64& maths_rng() {
        static std::mt19937_64 rng(std::random_device{}());
        return rng;
    }
    inline std::mutex& maths_rng_mux() {
        static std::mutex m;
        return m;
    }

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
                std::lock_guard<std::mutex> lk(maths_rng_mux());
                static std::uniform_real_distribution<double> dist(0.0, 1.0);
                return rb::list_of({rb::make_number(dist(maths_rng()), false)});
            },
            rb::make_sign("random", {}, {{"r", "std::Number"}})
        );
    }

    // maths.seed(n) -> (void) —— make the RNG reproducible. Without a seed the
    // sequence differs every run, which makes any random-dependent program
    // impossible to test deterministically.
    // maths.seed(n) -> (void) —— 使 RNG 可复现。未播种时每次运行序列不同，
    // 导致任何依赖随机的程序无法写确定性测试。
    inline rt_basic::Callable method_maths_seed() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto n = rb::number_of(rb::para_at(paras, 0));
                if (!n) {
                    return rb::list_of({rb::native_error(
                        "maths.seed requires a std::Number")});
                }
                std::lock_guard<std::mutex> lk(maths_rng_mux());
                maths_rng().seed(static_cast<std::uint64_t>(*n));
                return rb::empty_result();
            },
            rb::make_sign("seed", {{"n", "std::Number"}}, {})
        );
    }

    // maths.random_int(a, b) -> (int in [a, b]) —— inclusive integer range.
    // maths.random_int(a, b) -> (int in [a, b]) —— 含端点的整数区间。
    inline rt_basic::Callable method_maths_random_int() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto a = rb::number_of(rb::para_at(paras, 0));
                auto b = rb::number_of(rb::para_at(paras, 1));
                if (!a || !b) {
                    return rb::list_of({rb::native_error(
                        "maths.random_int requires two std::Number")});
                }
                long long lo = static_cast<long long>(std::floor(*a));
                long long hi = static_cast<long long>(std::floor(*b));
                if (lo > hi) std::swap(lo, hi);
                std::lock_guard<std::mutex> lk(maths_rng_mux());
                std::uniform_int_distribution<long long> dist(lo, hi);
                return rb::list_of({rb::make_int(dist(maths_rng()))});
            },
            rb::make_sign("random_int",
                {{"a", "std::Number"}, {"b", "std::Number"}},
                {{"r", "std::Number"}})
        );
    }

    // maths.choice(arr) -> (element) —— pick a uniformly random element.
    // maths.choice(arr) -> (element) —— 等概率随机选取一个元素。
    inline rt_basic::Callable method_maths_choice() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto arr = rb::para_at(paras, 0);
                auto* src = rb::attributes_of(arr);
                if (!src) {
                    return rb::list_of({rb::native_error(
                        "maths.choice requires an Array")});
                }
                std::size_t n = rb::container_size(*src);
                if (n == 0) {
                    return rb::list_of({rb::native_error(
                        "maths.choice: array is empty")});
                }
                std::lock_guard<std::mutex> lk(maths_rng_mux());
                std::uniform_int_distribution<std::size_t> dist(0, n - 1);
                auto it = src->find(rb::elem_key(dist(maths_rng())));
                return rb::list_of({it != src->end()
                    ? it->second : rb::make_int(0)});
            },
            rb::make_sign("choice", {{"arr", "std::Array"}},
                {{"item", "std::Object"}})
        );
    }

    // maths.shuffle(arr) -> (Array) —— Fisher-Yates, returns a NEW shuffled
    // array (the input is not mutated). / 返回新打乱的数组（不改原数组）。
    inline rt_basic::Callable method_maths_shuffle() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto arr = rb::para_at(paras, 0);
                auto* src = rb::attributes_of(arr);
                if (!src) {
                    return rb::list_of({rb::native_error(
                        "maths.shuffle requires an Array")});
                }
                std::size_t n = rb::container_size(*src);
                std::vector<runtime::RuntimeObjectPtr> items;
                items.reserve(n);
                for (std::size_t i = 0; i < n; ++i) {
                    auto it = src->find(rb::elem_key(i));
                    if (it != src->end()) items.push_back(it->second);
                }
                std::lock_guard<std::mutex> lk(maths_rng_mux());
                for (std::size_t i = items.size(); i > 1; --i) {
                    std::uniform_int_distribution<std::size_t> dist(0, i - 1);
                    std::swap(items[i - 1], items[dist(maths_rng())]);
                }
                return rb::list_of({rb::make_array(items)});
            },
            rb::make_sign("shuffle", {{"arr", "std::Array"}},
                {{"arr", "std::Array"}})
        );
    }

    // maths.gauss(mu, sigma) -> (Number) —— normal sample (Box-Muller).
    // maths.gauss(mu, sigma) -> (Number) —— 正态采样（Box-Muller）。
    inline rt_basic::Callable method_maths_gauss() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto mu = rb::number_of(rb::para_at(paras, 0));
                auto sigma = rb::number_of(rb::para_at(paras, 1));
                if (!mu || !sigma) {
                    return rb::list_of({rb::native_error(
                        "maths.gauss requires mu and sigma")});
                }
                std::lock_guard<std::mutex> lk(maths_rng_mux());
                static std::normal_distribution<double> dist(0.0, 1.0);
                double z = dist(maths_rng());
                return rb::list_of({rb::make_number(*mu + z * (*sigma), false)});
            },
            rb::make_sign("gauss",
                {{"mu", "std::Number"}, {"sigma", "std::Number"}},
                {{"r", "std::Number"}})
        );
    }

    // ---- scalar utilities / 标量工具 ----

    inline rt_basic::Callable method_maths_clamp() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto v = rb::number_of(rb::para_at(paras, 0));
                auto lo = rb::number_of(rb::para_at(paras, 1));
                auto hi = rb::number_of(rb::para_at(paras, 2));
                if (!v || !lo || !hi) {
                    return rb::list_of({rb::native_error(
                        "maths.clamp requires three std::Number")});
                }
                double r = *v;
                if (r < *lo) r = *lo;
                if (r > *hi) r = *hi;
                return rb::list_of({rb::make_number(r, false)});
            },
            rb::make_sign("clamp",
                {{"v", "std::Number"}, {"lo", "std::Number"},
                 {"hi", "std::Number"}}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_lerp() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto a = rb::number_of(rb::para_at(paras, 0));
                auto b = rb::number_of(rb::para_at(paras, 1));
                auto t = rb::number_of(rb::para_at(paras, 2));
                if (!a || !b || !t) {
                    return rb::list_of({rb::native_error(
                        "maths.lerp requires three std::Number")});
                }
                return rb::list_of({rb::make_number(
                    *a + (*t) * (*b - *a), false)});
            },
            rb::make_sign("lerp",
                {{"a", "std::Number"}, {"b", "std::Number"},
                 {"t", "std::Number"}}, {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_sign() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto x = rb::number_of(rb::para_at(paras, 0));
                if (!x) {
                    return rb::list_of({rb::native_error(
                        "maths.sign requires a std::Number")});
                }
                double r = (*x > 0) ? 1.0 : ((*x < 0) ? -1.0 : 0.0);
                return rb::list_of({rb::make_number(r, true)});
            },
            rb::make_sign("sign", {{"x", "std::Number"}},
                {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_trunc() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                return math1(paras, [](double x){ return std::trunc(x); }, true);
            },
            rb::make_sign("trunc", {{"x", "std::Number"}},
                {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_hypot() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto a = rb::number_of(rb::para_at(paras, 0));
                auto b = rb::number_of(rb::para_at(paras, 1));
                if (!a || !b) {
                    return rb::list_of({rb::native_error(
                        "maths.hypot requires two std::Number")});
                }
                return rb::list_of({rb::make_number(
                    std::hypot(*a, *b), false)});
            },
            rb::make_sign("hypot",
                {{"a", "std::Number"}, {"b", "std::Number"}},
                {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_is_nan() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto x = rb::number_of(rb::para_at(paras, 0));
                if (!x) {
                    return rb::list_of({rb::native_error(
                        "maths.is_nan requires a std::Number")});
                }
                return rb::list_of({rb::make_boolean(
                    std::isnan(*x) != 0)});
            },
            rb::make_sign("is_nan", {{"x", "std::Number"}},
                {{"b", "std::Boolean"}})
        );
    }

    inline rt_basic::Callable method_maths_is_infinite() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto x = rb::number_of(rb::para_at(paras, 0));
                if (!x) {
                    return rb::list_of({rb::native_error(
                        "maths.is_infinite requires a std::Number")});
                }
                return rb::list_of({rb::make_boolean(
                    std::isinf(*x) != 0)});
            },
            rb::make_sign("is_infinite", {{"x", "std::Number"}},
                {{"b", "std::Boolean"}})
        );
    }

    inline rt_basic::Callable method_maths_is_finite() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto x = rb::number_of(rb::para_at(paras, 0));
                if (!x) {
                    return rb::list_of({rb::native_error(
                        "maths.is_finite requires a std::Number")});
                }
                return rb::list_of({rb::make_boolean(
                    std::isfinite(*x) != 0)});
            },
            rb::make_sign("is_finite", {{"x", "std::Number"}},
                {{"b", "std::Boolean"}})
        );
    }

    inline rt_basic::Callable method_maths_degrees() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto r = rb::number_of(rb::para_at(paras, 0));
                if (!r) {
                    return rb::list_of({rb::native_error(
                        "maths.degrees requires a std::Number (radians)")});
                }
                return rb::list_of({rb::make_number(
                    *r * 180.0 / std::acos(-1.0), false)});
            },
            rb::make_sign("degrees", {{"r", "std::Number"}},
                {{"d", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_radians() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto d = rb::number_of(rb::para_at(paras, 0));
                if (!d) {
                    return rb::list_of({rb::native_error(
                        "maths.radians requires a std::Number (degrees)")});
                }
                return rb::list_of({rb::make_number(
                    *d * std::acos(-1.0) / 180.0, false)});
            },
            rb::make_sign("radians", {{"d", "std::Number"}},
                {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_gcd() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto a = rb::number_of(rb::para_at(paras, 0));
                auto b = rb::number_of(rb::para_at(paras, 1));
                if (!a || !b) {
                    return rb::list_of({rb::native_error(
                        "maths.gcd requires two std::Number")});
                }
                long long x = std::llabs(static_cast<long long>(*a));
                long long y = std::llabs(static_cast<long long>(*b));
                while (y) { long long t = y; y = x % y; x = t; }
                return rb::list_of({rb::make_int(x)});
            },
            rb::make_sign("gcd",
                {{"a", "std::Number"}, {"b", "std::Number"}},
                {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_lcm() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto a = rb::number_of(rb::para_at(paras, 0));
                auto b = rb::number_of(rb::para_at(paras, 1));
                if (!a || !b) {
                    return rb::list_of({rb::native_error(
                        "maths.lcm requires two std::Number")});
                }
                long long x = std::llabs(static_cast<long long>(*a));
                long long y = std::llabs(static_cast<long long>(*b));
                if (x == 0 || y == 0) return rb::list_of({rb::make_int(0)});
                long long g = x;
                long long t = y;
                while (t) { long long tmp = t; t = g % t; g = tmp; }
                return rb::list_of({rb::make_int(x / g * y)});
            },
            rb::make_sign("lcm",
                {{"a", "std::Number"}, {"b", "std::Number"}},
                {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_factorial() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto n = rb::number_of(rb::para_at(paras, 0));
                if (!n) {
                    return rb::list_of({rb::native_error(
                        "maths.factorial requires a std::Number")});
                }
                long long v = static_cast<long long>(*n);
                if (v < 0) {
                    return rb::list_of({rb::native_error(
                        "maths.factorial: negative input")});
                }
                long long r = 1;
                for (long long i = 2; i <= v; ++i) r *= i;
                return rb::list_of({rb::make_int(r)});
            },
            rb::make_sign("factorial", {{"n", "std::Number"}},
                {{"r", "std::Number"}})
        );
    }

    inline rt_basic::Callable method_maths_comb() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto n = rb::number_of(rb::para_at(paras, 0));
                auto k = rb::number_of(rb::para_at(paras, 1));
                if (!n || !k) {
                    return rb::list_of({rb::native_error(
                        "maths.comb requires two std::Number")});
                }
                long long N = static_cast<long long>(*n);
                long long K = static_cast<long long>(*k);
                if (K < 0 || K > N) return rb::list_of({rb::make_int(0)});
                K = std::min(K, N - K);
                long long r = 1;
                for (long long i = 1; i <= K; ++i) {
                    r = r * (N - K + i) / i;
                }
                return rb::list_of({rb::make_int(r)});
            },
            rb::make_sign("comb",
                {{"n", "std::Number"}, {"k", "std::Number"}},
                {{"r", "std::Number"}})
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
        // reproducible RNG + random primitives
        // 可复现 RNG + 随机原语
        proto->set_method("seed",        method_maths_seed());
        proto->set_method("random_int",  method_maths_random_int());
        proto->set_method("choice",      method_maths_choice());
        proto->set_method("shuffle",     method_maths_shuffle());
        proto->set_method("gauss",       method_maths_gauss());
        // scalar utilities / 标量工具
        proto->set_method("clamp",      method_maths_clamp());
        proto->set_method("lerp",       method_maths_lerp());
        proto->set_method("sign",       method_maths_sign());
        proto->set_method("trunc",      method_maths_trunc());
        proto->set_method("hypot",      method_maths_hypot());
        proto->set_method("is_nan",     method_maths_is_nan());
        proto->set_method("is_infinite", method_maths_is_infinite());
        proto->set_method("is_finite",  method_maths_is_finite());
        proto->set_method("degrees",    method_maths_degrees());
        proto->set_method("radians",    method_maths_radians());
        proto->set_method("gcd",        method_maths_gcd());
        proto->set_method("lcm",        method_maths_lcm());
        proto->set_method("factorial",  method_maths_factorial());
        proto->set_method("comb",       method_maths_comb());

        runtime::Prototypes p;
        p.regcls("Maths", proto);
        ::stdRT.add_protos(p);
    }

    // Self-register so the interpreter can initialize this library.
    // 自注册，使解释器能够初始化本库。
    inline bool _registered =
        (rt_builtin::register_native_lib("maths", &init_maths_stdlib), true);

} // namespace rt_lib_maths
