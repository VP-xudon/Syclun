// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/assert.hpp
//
// Standard library: assert (C++-backed backend).
// 标准库：assert（C++ 底层实现）。
//
// Two layers:
//  1. `Checker` — a runtime legality validator (the modern replacement for the
//     retired poison-water exception catcher): has_method / has_changed.
//  2. `Assert`  — a small test framework: pure assertion predicates
//     (equal / is_true / is_false / near / throws), plus `Suite` aggregation
//     (suite / test / run / tap / json) that produces TAP / TAP-JSON output.
// The C++ twin of lib/assert.synl. Self-registered under "assert".
// 两层：
//  1. Checker——运行期合法性校验器（已退役毒水异常捕获器的现代替代品）：
//     has_method / has_changed。
//  2. Assert——轻量测试框架：纯断言谓词（equal / is_true / is_false / near /
//     throws），以及 Suite 聚合（suite / test / run / tap / json），产出 TAP /
//     TAP-JSON 输出。本文件是 lib/assert.synl 的 C++ 孪生体，以 "assert" 自注册。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <stdexcept>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_assert {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // --------------------------------------------------------
    // Helpers / 辅助
    // --------------------------------------------------------

    // Deep structural equality for any RuntimeObject (numbers, strings,
    // booleans, and recursively for containers). / 任意 RuntimeObject 的深层
    // 结构相等（数字、字符串、布尔，以及容器的递归比较）。
    inline bool deep_equal(const RuntimeObjectPtr& a, const RuntimeObjectPtr& b,
                           int depth = 0) {
        if (!a && !b) return true;
        if (!a || !b) return false;
        if (auto na = rb::number_of(a)) {
            auto nb = rb::number_of(b);
            return nb && *na == *nb;
        }
        if (auto sa = rb::string_of(a)) {
            auto sb = rb::string_of(b);
            return sb && *sa == *sb;
        }
        if (auto ba = rb::boolean_of(a)) {
            auto bb = rb::boolean_of(b);
            return bb && *ba == *bb;
        }
        auto* ca = rb::attributes_of(a);
        auto* cb = rb::attributes_of(b);
        if (ca && cb) {
            std::size_t na = rb::container_size(*ca);
            std::size_t nb = rb::container_size(*cb);
            if (na != nb) return false;
            for (std::size_t i = 0; i < na; ++i) {
                auto ia = ca->find(rb::elem_key(i));
                auto ib = cb->find(rb::elem_key(i));
                if (ia == ca->end() || ib == cb->end()) return false;
                if (!deep_equal(ia->second, ib->second, depth + 1)) return false;
            }
            return true;
        }
        return false;
    }

    // Run a closure, returning true on success and filling `msg` on failure.
    // NOTE: do NOT take a GILScope here. Synth-OOP evaluation is GIL-serialized
    // per the top-level run() (interpreter.hpp holds the GIL for the whole user
    // evaluation), so the calling thread already owns the GIL when this runs from
    // user code; re-acquiring a non-recursive mutex would deadlock. The nested
    // call_behavior therefore executes correctly without another lock.
    // 注意：此处不要再加 GILScope。Synth-OOP 求值本就 GIL 串行化（run() 在顶层
    // 持有 GIL 覆盖整段用户求值），故从用户代码调用时本线程已持有 GIL，再对
    // 非递归 mutex 加锁会死锁。嵌套的 call_behavior 因而无需再次加锁即可正确执行。
    inline bool run_capture(const RuntimeObjectPtr& fn, std::string& msg) {
        try {
            rt_basic::InstanceMap localEnv;
            auto out = rb::call_behavior(fn, localEnv, rb::empty_result());
            (void)out;
            return true;
        } catch (const rt_builtin::NativeError& e) {
            msg = e.what_msg;
            return false;
        } catch (const std::exception& e) {
            msg = std::string("threw: ") + e.what();
            return false;
        } catch (...) {
            msg = "threw an unknown exception";
            return false;
        }
    }

    // ========================================================
    // Checker —— runtime legality validator / 运行期合法性校验器
    // ========================================================
    inline rt_basic::Callable method_checker_has_method() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto target = rb::para_at(paras, 0);
                auto nameObj = rb::para_at(paras, 1);
                auto name = rb::string_of(nameObj);
                if (!name) return rb::list_of({rb::make_boolean(false)});
                auto cls = std::dynamic_pointer_cast<RuntimeClass>(target);
                bool has = false;
                if (cls) has = cls->get_methods().count(*name) > 0;
                return rb::list_of({rb::make_boolean(has)});
            },
            rb::make_sign("has_method",
                {{"target", "std::Object"}, {"name", "std::String"}},
                {{"result", "std::Boolean"}})
        );
    }

    inline rt_basic::Callable method_checker_has_changed() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto target = rb::para_at(paras, 0);
                auto cls = std::dynamic_pointer_cast<RuntimeClass>(target);
                bool changed = false;
                if (cls) changed = cls->methods_dirty;
                return rb::list_of({rb::make_boolean(changed)});
            },
            rb::make_sign("has_changed",
                {{"target", "std::Object"}},
                {{"result", "std::Boolean"}})
        );
    }

    // ========================================================
    // Assert —— pure assertion predicates / 纯断言谓词
    // ========================================================
    inline rt_basic::Callable method_assert_equal() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto a = rb::para_at(paras, 0);
                auto b = rb::para_at(paras, 1);
                return rb::list_of({rb::make_boolean(deep_equal(a, b))});
            },
            rb::make_sign("equal",
                {{"actual", "std::Object"}, {"expected", "std::Object"}},
                {{"result", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_assert_is_true() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto v = rb::para_at(paras, 0);
                auto b = rb::boolean_of(v);
                return rb::list_of({rb::make_boolean(b ? *b : false)});
            },
            rb::make_sign("is_true",
                {{"cond", "std::Object"}}, {{"result", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_assert_is_false() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto v = rb::para_at(paras, 0);
                auto b = rb::boolean_of(v);
                return rb::list_of({rb::make_boolean(!(b ? *b : false))});
            },
            rb::make_sign("is_false",
                {{"cond", "std::Object"}}, {{"result", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_assert_near() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto a = rb::number_of(rb::para_at(paras, 0));
                auto b = rb::number_of(rb::para_at(paras, 1));
                auto t = rb::number_of(rb::para_at(paras, 2));
                bool ok = a && b && t &&
                          std::fabs(*a - *b) <= std::fabs(*t);
                return rb::list_of({rb::make_boolean(ok)});
            },
            rb::make_sign("near",
                {{"actual", "std::Number"}, {"expected", "std::Number"},
                 {"tol", "std::Number"}},
                {{"result", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_assert_throws() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto fn = rb::para_at(paras, 0);
                if (!fn) return rb::list_of({rb::make_boolean(false)});
                std::string msg;
                bool caught = !run_capture(fn, msg);  // true if it threw
                return rb::list_of({rb::make_boolean(caught)});
            },
            rb::make_sign("throws",
                {{"fn", "@"}}, {{"result", "std::Boolean"}})
        );
    }

    // ========================================================
    // Suite —— test aggregation / 测试聚合
    // ========================================================
    inline rt_basic::Callable method_assert_suite() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto name = rb::string_of(rb::para_at(paras, 0));
                auto suite = ::stdRT.make("Suite");
                auto* cls = dynamic_cast<RuntimeClass*>(suite.get());
                auto& am = cls->get_attributes();
                am["name"]  = rb::make_string(name ? *name : "");
                am["cases"] = ::stdRT.make("Array");
                am["last"]  = ::stdRT.make("Array");
                return rb::list_of({suite});
            },
            rb::make_sign("suite",
                {{"name", "std::String"}}, {{"suite", "assert::Suite"}})
        );
    }
    // assert.test(suite, name, fn) -> () —— append a case (fn is a behavior).
    inline rt_basic::Callable method_assert_test() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto suite = rb::para_at(paras, 0);
                auto name  = rb::string_of(rb::para_at(paras, 1));
                auto fn    = rb::para_at(paras, 2);
                auto* cls = dynamic_cast<RuntimeClass*>(suite.get());
                if (!cls) return rb::empty_result();
                auto& am = cls->get_attributes();
                auto casesObj =
                    std::dynamic_pointer_cast<RuntimeClass>(am["cases"]);
                if (!casesObj) return rb::empty_result();
                auto& cases = casesObj->get_attributes();
                std::size_t i = rb::container_size(cases);
                auto rec = ::stdRT.make("Object");
                auto* rc = dynamic_cast<RuntimeClass*>(rec.get());
                auto& ram = rc->get_attributes();
                ram["name"] = rb::make_string(name ? *name : "");
                ram["fn"]   = fn ? fn : rb::make_boolean(false);
                cases[rb::elem_key(i)] = rec;
                rb::set_container_size(cases, i + 1);
                return rb::empty_result();
            },
            rb::make_sign("test",
                {{"suite", "assert::Suite"}, {"name", "std::String"},
                 {"fn", "@"}}, {})
        );
    }
    // assert.run(suite) -> (results) —— run every case; store under "last".
    // Each result is {name, ok, message}.
    inline rt_basic::Callable method_assert_run() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto suite = rb::para_at(paras, 0);
                auto* cls = dynamic_cast<RuntimeClass*>(suite.get());
                auto out = ::stdRT.make("Array");
                auto* outArr = dynamic_cast<RuntimeClass*>(out.get());
                std::size_t oi = 0;
                if (cls) {
                    auto& am = cls->get_attributes();
                    auto* cases = rb::attributes_of(am["cases"]);
                    if (cases) {
                        std::size_t n = rb::container_size(*cases);
                        for (std::size_t i = 0; i < n; ++i) {
                            auto it = cases->find(rb::elem_key(i));
                            if (it == cases->end()) continue;
                            auto* rc = dynamic_cast<RuntimeClass*>(it->second.get());
                            if (!rc) continue;
                            auto& ram = rc->get_attributes();
                            auto cname = rb::string_of(ram["name"]);
                            std::string msg;
                            bool ok = run_capture(ram["fn"], msg);
                            auto res = ::stdRT.make("Object");
                            auto* rr = dynamic_cast<RuntimeClass*>(res.get());
                            auto& ram2 = rr->get_attributes();
                            ram2["name"]    = rb::make_string(cname ? *cname : "");
                            ram2["ok"]      = rb::make_boolean(ok);
                            ram2["message"] = rb::make_string(msg);
                            outArr->get_attributes()[rb::elem_key(oi++)] = res;
                        }
                    }
                    rb::set_container_size(outArr->get_attributes(), oi);
                    am["last"] = out;   // cache for tap/json / 缓存供 tap/json
                }
                return rb::list_of({out});
            },
            rb::make_sign("run",
                {{"suite", "assert::Suite"}}, {{"results", "std::Array"}})
        );
    }
    // assert.tap(suite) -> (text) —— TAP (Test Anything Protocol) text.
    inline rt_basic::Callable method_assert_tap() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto suite = rb::para_at(paras, 0);
                std::string tap;
                auto* cls = dynamic_cast<RuntimeClass*>(suite.get());
                if (cls) {
                    auto& am = cls->get_attributes();
                    auto* last = rb::attributes_of(am["last"]);
                    std::size_t n = last ? rb::container_size(*last) : 0;
                    tap = "1.." + std::to_string(n) + "\n";
                    for (std::size_t i = 0; i < n; ++i) {
                        auto it = last->find(rb::elem_key(i));
                        if (it == last->end()) continue;
                        auto* rc = dynamic_cast<RuntimeClass*>(it->second.get());
                        if (!rc) continue;
                        auto& ram = rc->get_attributes();
                        auto cname = rb::string_of(ram["name"]);
                        auto okb = rb::boolean_of(ram["ok"]);
                        bool ok = okb ? *okb : false;
                        auto m = rb::string_of(ram["message"]);
                        tap += (ok ? "ok " : "not ok ") + std::to_string(i + 1) + " ";
                        tap += (cname ? *cname : "");
                        if (!ok && m && !m->empty()) tap += " # " + *m;
                        tap += "\n";
                    }
                }
                return rb::list_of({rb::make_string(tap)});
            },
            rb::make_sign("tap",
                {{"suite", "assert::Suite"}}, {{"text", "std::String"}})
        );
    }
    // Clone a Suite from an attribute map (publish-side `=:` so that
    // `-(assert::Suite s) << a.suite("demo")` carries the cases across the
    // flow boundary). 依据属性表克隆 Suite（供公布侧 `=:` 使用，使
    // `-(assert::Suite s) << a.suite("demo")` 能跨越流边界带走 cases）。
    inline RuntimeObjectPtr clone_suite_from_env(rt_basic::InstanceMap& env) {
        auto suite = ::stdRT.make("Suite");
        auto* cls = dynamic_cast<RuntimeClass*>(suite.get());
        if (!cls) return suite;
        auto& am = cls->get_attributes();
        am["name"]  = env.count("name")  ? env["name"]  : rb::make_string("");
        am["cases"] = env.count("cases") ? env["cases"] : ::stdRT.make("Array");
        am["last"]  = env.count("last")  ? env["last"]  : ::stdRT.make("Array");
        return suite;
    }
    // `Suite` publish: a freshly built Suite has no scalar `#value`, so the base
    // `Object.=:` would publish nothing and the binding would lose its cases.
    // `Suite` 公布：新建的 Suite 没有标量 `#value`，基类 `Object.=:` 会公布空，
    // 绑定将丢失 cases。
    inline rt_basic::Callable method_suite_publish() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                return rb::list_of({clone_suite_from_env(env)});
            },
            rb::make_sign("=:", {}, {{"result", "assert::Suite"}})
        );
    }
    // `Suite` receive: copy the incoming suite's fields into this variable.
    // `Suite` 接收：把传入 Suite 的字段拷入本变量。
    inline rt_basic::Callable method_suite_receive() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto incoming = rb::para_at(paras, 0);
                if (!incoming || !paras || paras->size() != 1)
                    return rb::empty_result();
                auto* cls = dynamic_cast<RuntimeClass*>(incoming.get());
                if (!cls) return rb::empty_result();
                auto& senv = cls->get_attributes();
                const char* fields[] = {"name", "cases", "last"};
                for (const char* f : fields) {
                    auto it = senv.find(f);
                    if (it != senv.end()) env[f] = it->second;
                }
                return rb::empty_result();
            },
            rb::make_sign(":=", {{"value", "std::Object"}}, {})
        );
    }

    // assert.json(suite) -> (text) —— TAP-JSON summary.
    inline rt_basic::Callable method_assert_json() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto suite = rb::para_at(paras, 0);
                auto* cls = dynamic_cast<RuntimeClass*>(suite.get());
                std::string json = "{";
                if (cls) {
                    auto& am = cls->get_attributes();
                    auto sname = rb::string_of(am["name"]);
                    json += "\"suite\":\"" + (sname ? *sname : "") + "\",";
                    json += "\"cases\":[";
                    auto* last = rb::attributes_of(am["last"]);
                    std::size_t n = last ? rb::container_size(*last) : 0;
                    int passed = 0;
                    for (std::size_t i = 0; i < n; ++i) {
                        auto it = last->find(rb::elem_key(i));
                        if (it == last->end()) continue;
                        auto* rc = dynamic_cast<RuntimeClass*>(it->second.get());
                        if (!rc) continue;
                        auto& ram = rc->get_attributes();
                        auto cname = rb::string_of(ram["name"]);
                        auto okb = rb::boolean_of(ram["ok"]);
                        bool ok = okb ? *okb : false;
                        if (ok) ++passed;
                        auto m = rb::string_of(ram["message"]);
                        if (i) json += ",";
                        json += "{\"name\":\"" + (cname ? *cname : "") + "\"";
                        json += ",\"ok\":" + std::string(ok ? "true" : "false");
                        json += ",\"message\":\"" + (m ? *m : "") + "\"}";
                    }
                    json += "],\"passed\":" + std::to_string(passed);
                    json += ",\"total\":" + std::to_string(n);
                }
                json += "}";
                return rb::list_of({rb::make_string(json)});
            },
            rb::make_sign("json",
                {{"suite", "assert::Suite"}}, {{"text", "std::String"}})
        );
    }

    // ---- registration / 登记 ----
    inline void init_assert_stdlib() {
        // Checker (legacy validator) / 旧式校验器
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(
                ::stdRT.getcls("Object"));
            proto->set_method("has_method",  method_checker_has_method());
            proto->set_method("has_changed", method_checker_has_changed());
            runtime::Prototypes p;
            p.regcls("Checker", proto);
            ::stdRT.add_protos(p);
        }
        // Assert (pure predicates + suite facade) / 纯断言谓词 + Suite 门面
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(
                ::stdRT.getcls("Object"));
            proto->set_method("equal",    method_assert_equal());
            proto->set_method("is_true",  method_assert_is_true());
            proto->set_method("is_false", method_assert_is_false());
            proto->set_method("near",     method_assert_near());
            proto->set_method("throws",   method_assert_throws());
            // Suite facade: the documented API is Assert.suite / .test /
            // .run / .tap / .json (the suite object is the data container).
            // Suite 门面：公开 API 为 Assert.suite / .test / .run / .tap /
            // .json（suite 对象只是数据容器）。
            proto->set_method("suite", method_assert_suite());
            proto->set_method("test",  method_assert_test());
            proto->set_method("run",   method_assert_run());
            proto->set_method("tap",   method_assert_tap());
            proto->set_method("json",  method_assert_json());
            runtime::Prototypes p;
            p.regcls("Assert", proto);
            ::stdRT.add_protos(p);
        }
        // Suite (aggregation + TAP / TAP-JSON output)
        // Suite（聚合 + TAP / TAP-JSON 输出）
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(
                ::stdRT.getcls("Object"));
            // Default fields so a bare `-(assert::Suite s)` is immediately
            // usable; the publish/receive pair lets `s << a.suite("demo")`
            // carry cases across the flow boundary.
            // 默认字段，使裸 `-(assert::Suite s)` 即可直接用；公布/接收对使
            // `s << a.suite("demo")` 能把 cases 跨越流边界带走。
            // NOTE: do NOT set default `cases`/`last` here — the prototype's
            // default attributes are COPIED by shared_ptr value, so a mutable
            // Array set as a default would be shared by every Suite instance.
            // `a.suite(...)` builds a fresh Suite and the := receive copies its
            // fields, so no default is needed.
            // 注意：此处不要设默认 cases/last——原型默认属性按 shared_ptr 值拷贝，
            // 设为可变的 Array 默认会被每个 Suite 实例共享。a.suite(...) 构建全新
            // Suite，:= 接收再拷贝其字段，故无需默认值。
            proto->set_attribute("name",  rb::make_string(""));
            proto->set_method("=:",       method_suite_publish());
            proto->set_method(":=",       method_suite_receive());
            proto->set_method("suite", method_assert_suite());
            proto->set_method("test",  method_assert_test());
            proto->set_method("run",   method_assert_run());
            proto->set_method("tap",   method_assert_tap());
            proto->set_method("json",  method_assert_json());
            runtime::Prototypes p;
            p.regcls("Suite", proto);
            ::stdRT.add_protos(p);
        }
    }

    // Self-register so the interpreter can initialize this library.
    // 自注册，使解释器能够初始化本库。
    inline bool _registered =
        (rt_builtin::register_native_lib("assert", &init_assert_stdlib), true);

} // namespace rt_lib_assert
