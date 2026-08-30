// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/io.hpp
//
// Standard library: io (C++-backed backend).
// 标准库：io（C++ 底层实现）。
//
// The C++ twin of lib/io.synl. It provides the two stream types
// io::OStream (output) and io::IStream (input). It lives in the
// standard-library directory (not in builtin.hpp) and self-registers,
// exactly like file.hpp / system.hpp.
// lib/io.synl 的 C++ 孪生体，提供 io::OStream（输出）与 io::IStream
// （输入）两类。与 file.hpp / system.hpp 一样置于标准库目录（而非
// builtin.hpp）并自注册。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <iostream>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_io {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // io::OStream.:=(value) ~> (void) —— receive function (const contract).
    // Outputs the published value to standard output; does not modify out's
    // own state, so out may be declared const (Spec 3.4.1). The receiver is
    // a universal sink: any runtime object may be published to it (the
    // "value" parameter is typed std::Object, i.e. not type-checked at the
    // boundary — see enforce_sign's universal-receiver exemption).
    // This is the flow half of output: `out << x` publishes x into out,
    // which dispatches to this `:=` behavior.
    // io::OStream.:=(value) ~> (void) —— 接收函数（常数契约）。
    // 把公布值输出到标准输出；不修改 out 自身状态，因此 out 可声明为常数
    // （文档 3.4.1）。接收者是通用汇聚点：任意运行时对象皆可公布给它
    // （"value" 参数类型为 std::Object，边界不核查——见 enforce_sign 的
    // 通用接收豁免）。这是输出的"流"半边：`out << x` 把 x 公布给 out，
    // 进而分派到此 `:=` 行为。
    inline rt_basic::Callable method_OStream_receive() {
        return rb::native_method(
            [](
                rt_basic::InstanceMap& /*env*/,
                rt_basic::InstanceListPtr paras
            ) {
                if (paras) {
                    for (const auto& item : *paras) {
                        std::cout << rb::display(item);
                    }
                }
                return rb::empty_result();
            },
            rb::make_sign(":=", {{"value", "std::Object"}}, {})
        );
    }

    // io::OStream.push(value) ~> (void) —— method-form output (no newline).
    // Equivalent effect to `out << value`, but invoked as a method call
    // rather than a flow. Convenient for callers who prefer the explicit
    // `out.push("Hello")` spelling. Like `:=`, out is a universal sink.
    // io::OStream.push(value) ~> (void) —— 方法式输出（不换行）。
    // 效果等同 `out << value`，但以方法调用形式写出，便于偏好
    // `out.push("Hello")` 写法的调用方。与 `:=` 一样，out 是通用汇聚点。
    inline rt_basic::Callable method_OStream_push() {
        return rb::native_method(
            [](
                rt_basic::InstanceMap& /*env*/,
                rt_basic::InstanceListPtr paras
            ) {
                if (paras) {
                    for (const auto& item : *paras) {
                        std::cout << rb::display(item);
                    }
                }
                return rb::empty_result();
            },
            rb::make_sign("push", {{"value", "std::Object"}}, {})
        );
    }

    // io::OStream.push_line(value) ~> (void) —— method-form output + newline.
    // Writes the value followed by a line terminator, so each call lands on
    // its own line. The "self-printing" complement to push().
    // io::OStream.push_line(value) ~> (void) —— 方法式输出并换行。
    // 写出值并追加换行符，使每次调用各占一行。是 push() 的"自换行"补完。
    inline rt_basic::Callable method_OStream_push_line() {
        return rb::native_method(
            [](
                rt_basic::InstanceMap& /*env*/,
                rt_basic::InstanceListPtr paras
            ) {
                if (paras) {
                    for (const auto& item : *paras) {
                        std::cout << rb::display(item);
                    }
                }
                std::cout << "\n";
                return rb::empty_result();
            },
            rb::make_sign("push_line", {{"value", "std::Object"}}, {})
        );
    }

    // io::IStream.=:() ~> (result) —— publish function (const semantics).
    // Reads one whitespace-delimited word from standard input and publishes
    // it (typed std::String); does not modify in's own state. On read
    // failure (EOF) rb::native_error() raises an immediate RuntimeException
    // ("input stream ended") at the source — poison values no longer
    // propagate, so there is no graceful empty-string fallback. This is the
    // flow half of input: `x << in` (i.e. `x =: in`) pulls a word from in.
    // io::IStream.=:() ~> (result) —— 公布函数（常数语义）。从标准输入读取
    // 一个以空白分隔的词并公布（类型 std::String）；不修改 in 自身状态。
    // 读取失败（EOF）时 rb::native_error() 在源头即时抛出 RuntimeException
    //（"input stream ended"）—— 毒水值不再传播，故没有优雅的空串兜底。
    // 这是输入的"流"半边：`x << in`（即 `x =: in`）从 in 拉取一个词。
    inline rt_basic::Callable method_IStream_publish() {
        return rb::native_method(
            [](
                rt_basic::InstanceMap& /*env*/,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                std::string token;
                if (std::cin >> token) {
                    return rb::list_of({rb::make_string(token)});
                }
                return rb::list_of({rb::native_error(
                    rb::make_string(""), "input stream ended"
                )});
            },
            rb::make_sign("=:", {}, {{"result", "std::String"}})
        );
    }

    // io::IStream.get() ~> (result) —— method-form input (one word).
    // Equivalent to `=:` but invoked as a method: `in.get()` reads the next
    // whitespace-delimited word. On EOF raises an immediate RuntimeException
    // ("input stream ended").
    // io::IStream.get() ~> (result) —— 方法式输入（一个词）。
    // 等同 `=:` 但以方法调用：`in.get()` 读取下一个以空白分隔的词。
    // 遇 EOF 即时抛出 RuntimeException（"input stream ended"）。
    inline rt_basic::Callable method_IStream_get() {
        return rb::native_method(
            [](
                rt_basic::InstanceMap& /*env*/,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                std::string token;
                if (std::cin >> token) {
                    return rb::list_of({rb::make_string(token)});
                }
                return rb::list_of({rb::native_error(
                    rb::make_string(""), "input stream ended"
                )});
            },
            rb::make_sign("get", {}, {{"result", "std::String"}})
        );
    }

    // io::IStream.get_line() ~> (result) —— method-form input (one line).
    // Reads a whole line via std::getline (newline stripped). If a preceding
    // `get()`/`=:` left a dangling newline in the buffer, it is consumed
    // first so the call does not immediately return an empty line. On EOF
    // raises an immediate RuntimeException ("input stream ended").
    // io::IStream.get_line() ~> (result) —— 方法式输入（一整行）。
    // 经 std::getline 读取整行（去掉换行符）。若先前的 get()/`=:` 在缓冲里
    // 留下悬空换行，先把它吃掉，避免本调用立刻返回空行。遇 EOF 即时抛出
    // RuntimeException（"input stream ended"）。
    inline rt_basic::Callable method_IStream_get_line() {
        return rb::native_method(
            [](
                rt_basic::InstanceMap& /*env*/,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                // Consume a possible leftover newline left by a preceding `>>`
                // read, otherwise getline would return an empty line at once.
                // 吃掉可能由先前 `>>` 读取遗留的换行，否则 getline 会立刻
                // 返回空行。
                if (std::cin.peek() == '\n') {
                    std::cin.get();
                }
                std::string line;
                if (std::getline(std::cin, line)) {
                    return rb::list_of({rb::make_string(line)});
                }
                return rb::list_of({rb::native_error(
                    rb::make_string(""), "input stream ended"
                )});
            },
            rb::make_sign("get_line", {}, {{"result", "std::String"}})
        );
    }

    inline void init_io_stdlib() {
        auto proto_o = std::make_shared<rt_basic::ClsProto>(
            ::stdRT.getcls("Object")
        );
        proto_o->set_method(":=", method_OStream_receive());
        proto_o->set_method("push", method_OStream_push());
        proto_o->set_method("push_line", method_OStream_push_line());

        auto proto_i = std::make_shared<rt_basic::ClsProto>(
            ::stdRT.getcls("Object")
        );
        proto_i->set_method("=:", method_IStream_publish());
        proto_i->set_method("get", method_IStream_get());
        proto_i->set_method("get_line", method_IStream_get_line());

        runtime::Prototypes p;
        // Register under the unqualified name; Prototypes::regcls stamps the
        // authoritative type name "OStream" / "IStream" onto each prototype.
        // Source written as io::OStream / io::IStream still resolves via the
        // getcls "::" fallback.
        // 以非限定名登记；Prototypes::regcls 把权威类型名 "OStream" /
        // "IStream" 烙印到原型上。源码写 io::OStream / io::IStream 仍经
        // getcls 的 "::" 回退解析。
        p.regcls("OStream", proto_o);
        p.regcls("IStream", proto_i);
        ::stdRT.add_protos(p);
    }

    // Self-register so the interpreter (and the runtime acceptance suite)
    // can initialize this library.
    // 自注册，使解释器（及运行时验收套件）能够初始化本库。
    inline bool _registered =
        (rt_builtin::register_native_lib("io", &init_io_stdlib), true);

} // namespace rt_lib_io
