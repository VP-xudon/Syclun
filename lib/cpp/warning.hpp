// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/warning.hpp
//
// Standard library: warning (C++-backed backend).
// 标准库：warning（C++ 底层实现）。
//
// The sibling of `error`: `warning::Raise(code, message)` emits a warning to
// the standard error stream (prefixed `[WARNING]`) and returns an (empty)
// Raise object. Unlike `error::Raise`, it does NOT terminate the program —
// the warning is treated as a specific, non-fatal output, and execution
// continues. Useful for deprecations, recoverable conditions, and lint-style
// notices emitted from Synth-OOP programs.
// error 的姊妹库：warning::Raise(code, message) 向标准错误流输出一条警告
//（带 `[WARNING]` 前缀）并返回（空的）Raise 对象。与 error::Raise 不同，
// 它**不**终止程序——警告被视为一次特定的、非致命的输出，执行继续。适用于
// 弃用提示、可恢复状况、以及由 Synth-OOP 程序发出的类 lint 提示。
//
// The C++ twin of lib/warning.synl. Self-registered under "warning".
// lib/warning.synl 的 C++ 孪生体，以 "warning" 自注册。
// ============================================================

#pragma once

#include <string>
#include <memory>
#include <cstdio>
#include "../../src/builtin.hpp"

namespace rt_lib_warning {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // warning::Raise(code, message) —— emit a non-fatal warning to stderr.
    // The constructor's return value is discarded by the runtime (like every
    // constructor), so the side effect (the stderr line) is all that matters.
    // warning::Raise(code, message) —— 向 stderr 输出非致命警告。运行时会丢弃
    // 构造函数的返回值（与其它构造函数一致），故副作用（那行 stderr 输出）
    // 才是关键。
    inline rt_basic::Callable method_raise_ctor() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                // Only an explicit (argument-bearing) construction emits a line.
                // 仅显式（带参）构造才输出一行。
                if (!rb::para_at(paras, 0)) return rb::empty_result();
                auto code = rb::string_of(rb::para_at(paras, 0));
                auto msg  = rb::string_of(rb::para_at(paras, 1));
                std::fprintf(stderr, "[WARNING] %s: %s\n",
                             code ? code->c_str() : "WARNING",
                             msg ? msg->c_str() : "");
                return rb::empty_result();
            },
            rb::make_sign("::",
                {{"code", "std::String"}, {"message", "std::String"}}, {})
        );
    }

    inline void init_warning_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
        proto->set_method("::", method_raise_ctor());
        runtime::Prototypes p;
        p.regcls("Raise", proto);
        ::stdRT.add_protos(p);
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("warning", &init_warning_stdlib), true);

} // namespace rt_lib_warning
