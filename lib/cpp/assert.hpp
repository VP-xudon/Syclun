// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/assert.hpp
//
// Standard library: assert (C++-backed backend).
// 标准库：assert（C++ 底层实现）。
//
// Provides a Checker object that validates the legality of operations at
// runtime — the modern replacement for the retired poison-water exception
// catcher. Two queries are offered:
//   has_method(target, name) -> Boolean   true if `target` has a method
//                                         named `name` (native or user)
//   has_changed(target)      -> Boolean   true if any method variable of
//                                         `target` was rebound at runtime
//                                         (the instance's `methods_dirty`
//                                         flag, set by `obj.method.=(beh)`
//                                         or `obj.method << beh`)
// The C++ twin of lib/assert.synl. Self-registered under "assert".
// 提供 Checker 对象，在运行期验证操作的合法性——这是已退役的毒水异常捕获器
// 的现代替代品。提供两类查询：
//   has_method(target, name) -> Boolean   目标对象拥有名为 name 的方法则为真
//   has_changed(target)      -> Boolean   目标的任一方法变量在运行期被重绑
//                                         （即实例的 methods_dirty 标志，由
//                                         `obj.method.=(beh)` 或
//                                         `obj.method << beh` 置位）则为真
// 本文件是 lib/assert.synl 的 C++ 孪生体，以 "assert" 自注册。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_assert {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // has_method(target[std::Object], name[std::String]) -> Boolean
    // 检查目标对象是否拥有名为 name 的方法（原生或用户定义皆可）。
    inline rt_basic::Callable method_checker_has_method() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto target = rb::para_at(paras, 0);
                auto nameObj = rb::para_at(paras, 1);
                auto name = rb::string_of(nameObj);
                if (!name) {
                    return rb::list_of({rb::make_boolean(false)});
                }
                auto cls = std::dynamic_pointer_cast<RuntimeClass>(target);
                bool has = false;
                if (cls) {
                    has = cls->get_methods().count(*name) > 0;
                }
                return rb::list_of({rb::make_boolean(has)});
            },
            rb::make_sign("has_method",
                {{"target", "std::Object"}, {"name", "std::String"}},
                {{"result", "std::Boolean"}})
        );
    }

    // has_changed(target[std::Object]) -> Boolean
    // 检查目标对象的任一方法变量是否在运行期被动态重绑。
    // 读取 RuntimeClass::methods_dirty，由方法重绑路径置位。
    inline rt_basic::Callable method_checker_has_changed() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto target = rb::para_at(paras, 0);
                auto cls = std::dynamic_pointer_cast<RuntimeClass>(target);
                bool changed = false;
                if (cls) {
                    changed = cls->methods_dirty;
                }
                return rb::list_of({rb::make_boolean(changed)});
            },
            rb::make_sign("has_changed",
                {{"target", "std::Object"}},
                {{"result", "std::Boolean"}})
        );
    }

    // ---- registration / 登记 ----
    inline void init_assert_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(
            ::stdRT.getcls("Object")
        );
        proto->set_method("has_method",  method_checker_has_method());
        proto->set_method("has_changed", method_checker_has_changed());

        runtime::Prototypes p;
        p.regcls("Checker", proto);
        ::stdRT.add_protos(p);
    }

    // Self-register so the interpreter can initialize this library.
    // 自注册，使解释器能够初始化本库。
    inline bool _registered =
        (rt_builtin::register_native_lib("assert", &init_assert_stdlib), true);

} // namespace rt_lib_assert
