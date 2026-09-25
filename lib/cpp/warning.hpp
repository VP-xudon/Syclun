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

    // warning::Raise(code, message) —— emit a non-fatal warning to stderr and
    // populate the instance with `code` / `message` so callers can query it
    // later (e.g. warn.code, warn.message). The runtime discards the
    // constructor's return value, so the state is read off the instance itself.
    // warning::Raise(code, message) —— 向 stderr 输出非致命警告，并把 `code` /
    // `message` 写入实例，供调用方随后查询（如 warn.code、warn.message）。
    // 运行时会丢弃构造函数的返回值，故状态须从实例本身读取。
    inline rt_basic::Callable method_raise_ctor() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                // Only an explicit (argument-bearing) construction emits a line.
                // 仅显式（带参）构造才输出一行。
                if (!rb::para_at(paras, 0)) return rb::empty_result();
                auto code = rb::string_of(rb::para_at(paras, 0));
                auto msg  = rb::string_of(rb::para_at(paras, 1));
                const std::string c = code ? *code : std::string("WARNING");
                const std::string m = msg ? *msg : std::string();
                env["code"]    = rb::make_string(c);
                env["message"] = rb::make_string(m);
                std::fprintf(stderr, "[WARNING] %s: %s\n", c.c_str(), m.c_str());
                return rb::empty_result();
            },
            rb::make_sign("::",
                {{"code", "std::String"}, {"message", "std::String"}}, {})
        );
    }

    // `=:` so that `w := warning::Raise(...)` copies every attribute off the
    // source warning instance (a plain assignment would only copy known fields).
    // `=:` 使 `w := warning::Raise(...)` 把源警告实例的全部属性拷出
    // （普通赋值只会复制已知字段）。
    inline rt_basic::Callable method_raise_receive() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto incoming = rb::para_at(paras, 0);
                if (auto* src = dynamic_cast<RuntimeClass*>(incoming.get())) {
                    if (auto* sattrs = rb::attributes_of(incoming)) {
                        for (const auto& kv : *sattrs) env[kv.first] = kv.second;
                    }
                }
                return rb::empty_result();
            },
            rb::make_sign("=:", {{"value", "std::Object"}}, {})
        );
    }

    inline void init_warning_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
        proto->set_method("::", method_raise_ctor());
        proto->set_method("=:", method_raise_receive());
        proto->set_attribute("code",    rb::make_string(""));
        proto->set_attribute("message", rb::make_string(""));
        runtime::Prototypes p;
        // Centralized registration MUST carry the package index `warning::` (the
        // "集中的必须加" rule): this is what makes `warning` a known set and lets
        // `warning::Raise` resolve as a value. The authoritative type name is
        // `warning::Raise` (the `::`-prefixed part is the package locator). The
        // warning class is constructed directly via `-(warning::Raise(...) w)`,
        // so no preset object is needed. Mirrors io/json/re/encoding.
        // 集中登记须带包索引 `warning::`（「集中的必须加」）：这才能使 `warning` 成为已知集、
        // `warning::Raise` 可作值解析。权威类型名即 `warning::Raise`（`::` 前缀是包定位符）。
        // warning 类经 `-(warning::Raise(...) w)` 直接构造，故无需预置对象。与 io/json/re/encoding 一致。
        p.regcls("warning::Raise", proto);
        ::stdRT.add_protos(p);
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("warning", &init_warning_stdlib), true);

} // namespace rt_lib_warning
