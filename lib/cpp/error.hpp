// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/error.hpp
//
// Standard library: error (C++-backed backend).
// 标准库：error（C++ 底层实现）。
//
// A minimal, dependency-free error-raising library. `error::Raise` is a
// class whose CONSTRUCTOR immediately raises a fatal interpreter error with
// the supplied code and message, terminating the process (via Thrower, which
// emits a g++-style diagnostic and exits with the fatal status). Because the
// constructor never returns, `error::Raise("E", "msg")` is in practice a
// one-line `raise` statement.
// 一个极简、零依赖的错误抛出库。error::Raise 是一个类，其**构造函数**会
// 立刻以给定的 code 与 message 抛出致命解释器错误，终止进程（经 Thrower
// 上报类 g++ 诊断并以致命状态码退出）。由于构造函数绝不返回，
// `error::Raise("E", "msg")` 实际上就是一行 raise 语句。
//
// The constructor is guarded so that a bare member declaration
// `-(error::Raise e);` (instantiated by the host class with no arguments)
// does NOT fire — only an explicit `error::Raise(code, message)` call does.
// 构造函数带保护：裸成员声明 `-(error::Raise e);`（宿主类以无参方式实例化）
// 不会触发——只有显式的 `error::Raise(code, message)` 调用才会。
//
// The C++ twin of lib/error.synl. Self-registered under "error".
// lib/error.synl 的 C++ 孪生体，以 "error" 自注册。
// ============================================================

#pragma once

#include <string>
#include <memory>
#include "../../src/builtin.hpp"

namespace rt_lib_error {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // error::Raise(code, message) —— the constructor raises a fatal error.
    // Because Thrower.throwE terminates the process, this never returns.
    // error::Raise(code, message) —— 构造函数抛出致命错误。由于 Thrower 会直接
    // 终止进程，此函数永不返回。
    inline rt_basic::Callable method_raise_ctor() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                // Guarded: only an explicit (argument-bearing) construction fires.
                // 带保护：仅显式（带参）构造才触发。
                if (!rb::para_at(paras, 0)) return rb::empty_result();
                auto code = rb::string_of(rb::para_at(paras, 0));
                auto msg  = rb::string_of(rb::para_at(paras, 1));
                Thrower.throwE(code ? *code : "ERROR", msg ? *msg : "");
                return rb::empty_result(); // unreachable / 不可达
            },
            rb::make_sign("::",
                {{"code", "std::String"}, {"message", "std::String"}}, {})
        );
    }

    inline void init_error_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
        proto->set_method("::", method_raise_ctor());
        runtime::Prototypes p;
        p.regcls("Raise", proto);
        ::stdRT.add_protos(p);
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("error", &init_error_stdlib), true);

} // namespace rt_lib_error
