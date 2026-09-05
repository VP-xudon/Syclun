// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/file.hpp
//
// Standard library: file (C++-backed backend).
// 标准库：file（C++ 底层实现）。
//
// This header implements the native methods of the `file` class. It is the
// C++ twin of `lib/file.synl` (the Synth-OOP interface): file.synl declares
// the class shape, this header supplies the real behavior. The file lives
// beside the .synl in the standard-library directory — NOT inside
// builtin.hpp — so the native standard libraries are arranged exactly like
// the interpreted ones.
// 本头文件实现 `file` 类的原生方法，是 `lib/file.synl`（Synth-OOP 接口）的
// C++ 孪生体：file.synl 声明类的形态，本文件提供真实行为。它与 .synl
// 一同置于标准库目录中——而非塞进 builtin.hpp——使原生标准库与解释型
// 标准库的排列方式完全一致。
//
// Self-registration: at static initialization the registry entry "file" is
// bound to init_file_stdlib, so the interpreter can bring the class online
// without knowing about this file.
// 自注册：静态初始化时把注册表项 "file" 绑定到 init_file_stdlib，
// 解释器据此上线该类而无须感知本文件。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <cstdio>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_file {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // ---- native methods / 原生方法 ----

    // file.open(path, mode?) -> (void)
    // Record the target path (and optional mode) on the instance. The actual
    // OS handle is opened per-operation, keeping the object serializable and
    // side-effect free except when read/write is called.
    // 在实例上记录目标路径（与可选模式）。真实的 OS 句柄按每次操作打开，
    // 使对象可序列化，且除 read/write 调用外无副作用。
    inline rt_basic::Callable method_file_open() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto path = rb::string_of(rb::para_at(paras, 0));
                if (!path) {
                    return rb::list_of({rb::native_error(
                        "file.open requires a path string")});
                }
                env["path"] = rb::make_string(*path);
                auto mode = rb::string_of(rb::para_at(paras, 1));
                env["mode"] = rb::make_string(mode ? *mode : "r");
                return rb::empty_result();
            },
            rb::make_sign(
                "open",
                {{"path", "std::String"}, {"mode", "std::String"}},
                {}
            )
        );
    }

    // file.read() ~> (content)
    // Open the recorded path for reading and publish the whole content as a
    // String capsule. Errors degrade to poison water.
    // 按记录路径以读方式打开，把全部内容作为 String 胶囊公布。出错优雅降级为毒水。
    inline rt_basic::Callable method_file_read() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto path = rb::string_of(env["path"]);
                if (!path || path->empty()) {
                    return rb::list_of({rb::native_error(
                        "file.read: no path set (call open first)")});
                }
                std::ifstream fin(*path, std::ios::binary);
                if (!fin) {
                    return rb::list_of({rb::native_error(
                        "file.read: cannot open '" + *path + "'")});
                }
                std::stringstream ss;
                ss << fin.rdbuf();
                return rb::list_of({rb::make_string(ss.str())});
            },
            rb::make_sign("read", {}, {{"content", "std::String"}})
        );
    }

    // file.readlines() ~> (lines) —— content split into an Array of Strings.
    // file.readlines() ~> (lines) —— 内容按行拆成 String 数组。
    inline rt_basic::Callable method_file_readlines() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto path = rb::string_of(env["path"]);
                if (!path || path->empty()) {
                    return rb::list_of({rb::native_error(
                        "file.readlines: no path set")});
                }
                std::ifstream fin(*path);
                if (!fin) {
                    return rb::list_of({rb::native_error(
                        "file.readlines: cannot open '" + *path + "'")});
                }
                auto arr = ::stdRT.make("Array");
                auto* cls = dynamic_cast<runtime::RuntimeClass*>(arr.get());
                auto& aenv = cls->get_attributes();
                std::size_t i = 0;
                std::string line;
                while (std::getline(fin, line)) {
                    // Strip a trailing CR left by files written with CRLF line
                    // endings, so readlines is consistent across platforms.
                    // 去掉 CRLF 换行留下的尾随 CR，使 readlines 跨平台一致。
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    aenv[rb::elem_key(i)] = rb::make_string(line);
                    ++i;
                }
                rb::set_container_size(aenv, i);
                return rb::list_of({arr});
            },
            rb::make_sign("readlines", {}, {{"lines", "std::Array"}})
        );
    }

    // file.write(text) -> (void) —— truncate then write.
    // file.write(text) -> (void) —— 清空后写入。
    inline rt_basic::Callable method_file_write() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto path = rb::string_of(env["path"]);
                if (!path || path->empty()) {
                    return rb::list_of({rb::native_error(
                        "file.write: no path set")});
                }
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) {
                    return rb::list_of({rb::native_error(
                        "file.write requires a string argument")});
                }
                // Binary mode: never translate newlines, so a write on Windows
                // produces byte-identical content to a write on Linux (D4 fix).
                // 二进制模式：绝不翻译换行，使 Windows 与 Linux 写入逐字节一致（D4 修复）。
                std::ofstream fout(*path, std::ios::trunc | std::ios::binary);
                if (!fout) {
                    return rb::list_of({rb::native_error(
                        "file.write: cannot open '" + *path + "'")});
                }
                fout << *text;
                return rb::empty_result();
            },
            rb::make_sign("write", {{"text", "std::String"}}, {})
        );
    }

    // file.append(text) -> (void) —— append without truncation.
    // file.append(text) -> (void) —— 不清空地追加。
    inline rt_basic::Callable method_file_append() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto path = rb::string_of(env["path"]);
                if (!path || path->empty()) {
                    return rb::list_of({rb::native_error(
                        "file.append: no path set")});
                }
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) {
                    return rb::list_of({rb::native_error(
                        "file.append requires a string argument")});
                }
                std::ofstream fout(*path, std::ios::app | std::ios::binary);
                if (!fout) {
                    return rb::list_of({rb::native_error(
                        "file.append: cannot open '" + *path + "'")});
                }
                fout << *text;
                return rb::empty_result();
            },
            rb::make_sign("append", {{"text", "std::String"}}, {})
        );
    }

    // file.write_lines(lines) -> (void) — join an Array of Strings with "\n"
    // and write atomically (binary mode). Convenience counterpart to readlines.
    // file.write_lines(lines) -> (void) —— 用 "\n" 连接 String 数组并写入
    // （二进制模式）。readlines 的便捷对应物。
    inline rt_basic::Callable method_file_write_lines() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto path = rb::string_of(env["path"]);
                if (!path || path->empty()) {
                    return rb::list_of({rb::native_error(
                        "file.write_lines: no path set")});
                }
                auto arrObj = rb::para_at(paras, 0);
                auto* src = rb::attributes_of(arrObj);
                if (!src) {
                    return rb::list_of({rb::native_error(
                        "file.write_lines requires an Array of Strings")});
                }
                std::string content;
                std::size_t n = rb::container_size(*src);
                for (std::size_t i = 0; i < n; ++i) {
                    auto it = src->find(rb::elem_key(i));
                    if (it == src->end()) continue;
                    auto s = rb::string_of(it->second);
                    if (s) {
                        if (i > 0) content += "\n";
                        content += *s;
                    }
                }
                std::ofstream fout(*path, std::ios::trunc | std::ios::binary);
                if (!fout) {
                    return rb::list_of({rb::native_error(
                        "file.write_lines: cannot open '" + *path + "'")});
                }
                fout << content;
                return rb::empty_result();
            },
            rb::make_sign("write_lines", {{"lines", "std::Array"}}, {})
        );
    }

    // file.exists() ~> (ok) —— true when the recorded path is readable.
    // file.exists() ~> (ok) —— 记录路径可读时为 true。
    inline rt_basic::Callable method_file_exists() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto path = rb::string_of(env["path"]);
                bool ok = path && !path->empty()
                       && static_cast<bool>(std::ifstream(*path));
                return rb::list_of({rb::make_boolean(ok)});
            },
            rb::make_sign("exists", {}, {{"ok", "std::Boolean"}})
        );
    }

    // file.remove() ~> (ok) —— delete the recorded file.
    // file.remove() ~> (ok) —— 删除记录的文件。
    inline rt_basic::Callable method_file_remove() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto path = rb::string_of(env["path"]);
                if (!path || path->empty()) {
                    return rb::list_of({rb::native_error(
                        "file.remove: no path set")});
                }
                bool ok = (std::remove(path->c_str()) == 0);
                return rb::list_of({rb::make_boolean(ok)});
            },
            rb::make_sign("remove", {}, {{"ok", "std::Boolean"}})
        );
    }

    // file.size() ~> (sz) —— byte length of the recorded file.
    // file.size() ~> (sz) —— 记录文件的字节长度。
    inline rt_basic::Callable method_file_size() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto path = rb::string_of(env["path"]);
                if (!path || path->empty()) {
                    return rb::list_of({rb::native_error(
                        "file.size: no path set")});
                }
                std::ifstream fin(*path, std::ios::binary | std::ios::ate);
                if (!fin) {
                    return rb::list_of({rb::native_error(
                        "file.size: cannot open '" + *path + "'")});
                }
                auto bytes = static_cast<std::int64_t>(fin.tellg());
                return rb::list_of({rb::make_int(bytes < 0 ? 0 : bytes)});
            },
            rb::make_sign("size", {}, {{"sz", "std::Number"}})
        );
    }

    // ---- registration / 登记 ----
    inline void init_file_stdlib() {
        // Inherit Object's reserved methods, then add file-specific members
        // and native methods.
        // 继承 Object 的保留方法，再加 file 专属成员与原生方法。
        auto proto = std::make_shared<rt_basic::ClsProto>(
            ::stdRT.getcls("Object")
        );
        proto->set_attribute("path", rb::make_string(""));
        proto->set_attribute("mode", rb::make_string("r"));
        proto->set_method("open",     method_file_open());
        proto->set_method("read",     method_file_read());
        proto->set_method("readlines", method_file_readlines());
        proto->set_method("write",    method_file_write());
        proto->set_method("append",   method_file_append());
        proto->set_method("write_lines", method_file_write_lines());
        proto->set_method("exists",   method_file_exists());
        proto->set_method("remove",   method_file_remove());
        proto->set_method("size",     method_file_size());

        runtime::Prototypes p;
        p.regcls("File", proto);
        ::stdRT.add_protos(p);
    }

    // Self-register so the interpreter can initialize this library.
    // 自注册，使解释器能够初始化本库。
    inline bool _registered =
        (rt_builtin::register_native_lib("file", &init_file_stdlib), true);

} // namespace rt_lib_file
