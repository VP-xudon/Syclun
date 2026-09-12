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
#include <filesystem>
#include <chrono>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace fs = std::filesystem;

namespace rt_lib_file {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // Build an Array runtime object from a vector.
    // 由 vector 构造 Array 运行时对象。
    inline RuntimeObjectPtr build_array(const std::vector<RuntimeObjectPtr>& items) {
        auto arr = ::stdRT.make("Array");
        auto* cls = dynamic_cast<RuntimeClass*>(arr.get());
        auto& am = cls->get_attributes();
        for (std::size_t i = 0; i < items.size(); ++i) {
            am[rb::elem_key(i)] = items[i];
        }
        rb::set_container_size(am, items.size());
        return arr;
    }

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

    // file.exists() ~> (ok) —— true when the recorded path exists on disk.
    // Distinct from "readable": a file can exist yet be unreadable, or a
    // directory can exist. Uses fs::exists (D4-adjacent fix).
    // file.exists() ~> (ok) —— 记录路径在磁盘上存在时为 true。
    // 与"可读"不同：文件可能存在却不可读，目录也可能存在。使用 fs::exists。
    inline rt_basic::Callable method_file_exists() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto path = rb::string_of(env["path"]);
                bool ok = path && !path->empty() && fs::exists(*path);
                return rb::list_of({rb::make_boolean(ok)});
            },
            rb::make_sign("exists", {}, {{"ok", "std::Boolean"}})
        );
    }

    // ========================================================
    // Extra file methods (industrialization audit §4.4).
    // 追加文件方法（工业化审计 §4.4）。
    // ========================================================

    // ---- pure path helpers / 纯路径助手（取 path 字符串，返回结果字符串）----
    inline rt_basic::Callable method_file_join() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto a = rb::string_of(rb::para_at(paras, 0));
                auto b = rb::string_of(rb::para_at(paras, 1));
                if (!a || !b) {
                    return rb::list_of({rb::native_error("join requires two path strings")});
                }
                return rb::list_of({rb::make_string(
                    (fs::path(*a) / *b).generic_string())});
            },
            rb::make_sign("join",
                {{"base", "std::String"}, {"part", "std::String"}},
                {{"p", "std::String"}})
        );
    }
    inline rt_basic::Callable method_file_basename() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(rb::para_at(paras, 0));
                if (!p) return rb::list_of({rb::native_error("basename requires a path")});
                return rb::list_of({rb::make_string(
                    fs::path(*p).filename().generic_string())});
            },
            rb::make_sign("basename", {{"path", "std::String"}}, {{"name", "std::String"}})
        );
    }
    inline rt_basic::Callable method_file_dirname() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(rb::para_at(paras, 0));
                if (!p) return rb::list_of({rb::native_error("dirname requires a path")});
                return rb::list_of({rb::make_string(
                    fs::path(*p).parent_path().generic_string())});
            },
            rb::make_sign("dirname", {{"path", "std::String"}}, {{"dir", "std::String"}})
        );
    }
    inline rt_basic::Callable method_file_extname() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(rb::para_at(paras, 0));
                if (!p) return rb::list_of({rb::native_error("extname requires a path")});
                return rb::list_of({rb::make_string(
                    fs::path(*p).extension().generic_string())});
            },
            rb::make_sign("extname", {{"path", "std::String"}}, {{"ext", "std::String"}})
        );
    }
    inline rt_basic::Callable method_file_absolute() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(rb::para_at(paras, 0));
                if (!p) return rb::list_of({rb::native_error("absolute requires a path")});
                std::error_code ec;
                auto ap = fs::absolute(*p, ec);
                return rb::list_of({rb::make_string(ap.generic_string())});
            },
            rb::make_sign("absolute", {{"path", "std::String"}}, {{"p", "std::String"}})
        );
    }
    inline rt_basic::Callable method_file_normalize() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(rb::para_at(paras, 0));
                if (!p) return rb::list_of({rb::native_error("normalize requires a path")});
                std::error_code ec;
                auto np = fs::weakly_canonical(*p, ec);
                if (ec) np = fs::path(*p).lexically_normal();
                return rb::list_of({rb::make_string(np.generic_string())});
            },
            rb::make_sign("normalize", {{"path", "std::String"}}, {{"p", "std::String"}})
        );
    }

    // ---- existence / type predicates / 存在性与类型谓词 ----
    inline rt_basic::Callable method_file_is_file() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(rb::para_at(paras, 0));
                if (!p) return rb::list_of({rb::native_error("is_file requires a path")});
                std::error_code ec;
                bool ok = fs::is_regular_file(*p, ec);
                return rb::list_of({rb::make_boolean(ok)});
            },
            rb::make_sign("is_file", {{"path", "std::String"}}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_file_is_dir() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(rb::para_at(paras, 0));
                if (!p) return rb::list_of({rb::native_error("is_dir requires a path")});
                std::error_code ec;
                bool ok = fs::is_directory(*p, ec);
                return rb::list_of({rb::make_boolean(ok)});
            },
            rb::make_sign("is_dir", {{"path", "std::String"}}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_file_is_readable() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(rb::para_at(paras, 0));
                if (!p) return rb::list_of({rb::native_error("is_readable requires a path")});
                std::ifstream fin(*p);
                return rb::list_of({rb::make_boolean(static_cast<bool>(fin))});
            },
            rb::make_sign("is_readable", {{"path", "std::String"}}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_file_is_writable() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(rb::para_at(paras, 0));
                if (!p) return rb::list_of({rb::native_error("is_writable requires a path")});
                std::error_code ec;
                auto s = fs::status(*p, ec);
                if (ec || !fs::exists(s)) {
                    // Non-existent: test the parent directory's writability.
                    // 不存在：测父目录是否可写。
                    auto ps = fs::status(fs::path(*p).parent_path(), ec);
                    bool ok = !ec && ((ps.permissions() & fs::perms::owner_write)
                                      != fs::perms::none);
                    return rb::list_of({rb::make_boolean(ok)});
                }
                bool ok = (s.permissions() & fs::perms::owner_write) != fs::perms::none;
                return rb::list_of({rb::make_boolean(ok)});
            },
            rb::make_sign("is_writable", {{"path", "std::String"}}, {{"ok", "std::Boolean"}})
        );
    }

    // ---- directory operations / 目录操作 ----
    inline rt_basic::Callable method_file_mkdir() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(rb::para_at(paras, 0));
                if (!p) return rb::list_of({rb::native_error("mkdir requires a path")});
                std::error_code ec;
                bool ok = fs::create_directories(*p, ec);
                return rb::list_of({rb::make_boolean(!ec && ok)});
            },
            rb::make_sign("mkdir", {{"path", "std::String"}}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_file_rmdir() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(rb::para_at(paras, 0));
                if (!p) return rb::list_of({rb::native_error("rmdir requires a path")});
                std::error_code ec;
                bool ok = fs::remove(*p, ec);   // single entry; fails on non-empty
                return rb::list_of({rb::make_boolean(!ec && ok)});
            },
            rb::make_sign("rmdir", {{"path", "std::String"}}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_file_list_dir() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(rb::para_at(paras, 0));
                if (!p) return rb::list_of({rb::native_error("list_dir requires a path")});
                std::vector<RuntimeObjectPtr> out;
                std::error_code ec;
                for (auto& e : fs::directory_iterator(*p, ec)) {
                    out.push_back(rb::make_string(e.path().filename().generic_string()));
                }
                if (ec) return rb::list_of({rb::native_error(
                    "list_dir: " + ec.message())});
                return rb::list_of({build_array(out)});
            },
            rb::make_sign("list_dir", {{"path", "std::String"}}, {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_file_walk() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(rb::para_at(paras, 0));
                if (!p) return rb::list_of({rb::native_error("walk requires a path")});
                std::vector<RuntimeObjectPtr> out;
                std::error_code ec;
                for (auto& e : fs::recursive_directory_iterator(*p, ec)) {
                    out.push_back(rb::make_string(e.path().generic_string()));
                }
                if (ec) return rb::list_of({rb::native_error(
                    "walk: " + ec.message())});
                return rb::list_of({build_array(out)});
            },
            rb::make_sign("walk", {{"path", "std::String"}}, {{"arr", "std::Array"}})
        );
    }

    // ---- file operations / 文件操作 ----
    inline rt_basic::Callable method_file_copy_to() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto dst = rb::string_of(rb::para_at(paras, 0));
                auto src = rb::string_of(env["path"]);
                if (!dst) return rb::list_of({rb::native_error("copy_to requires a destination")});
                if (!src || src->empty()) {
                    return rb::list_of({rb::native_error("copy_to: no source path set")});
                }
                std::error_code ec;
                fs::copy_file(*src, *dst, fs::copy_options::overwrite_existing, ec);
                return rb::list_of({rb::make_boolean(!ec)});
            },
            rb::make_sign("copy_to", {{"dst", "std::String"}}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_file_move_to() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto dst = rb::string_of(rb::para_at(paras, 0));
                auto src = rb::string_of(env["path"]);
                if (!dst) return rb::list_of({rb::native_error("move_to requires a destination")});
                if (!src || src->empty()) {
                    return rb::list_of({rb::native_error("move_to: no source path set")});
                }
                std::error_code ec;
                fs::rename(*src, *dst, ec);
                return rb::list_of({rb::make_boolean(!ec)});
            },
            rb::make_sign("move_to", {{"dst", "std::String"}}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_file_rename() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto dst = rb::string_of(rb::para_at(paras, 0));
                auto src = rb::string_of(env["path"]);
                if (!dst) return rb::list_of({rb::native_error("rename requires a destination")});
                if (!src || src->empty()) {
                    return rb::list_of({rb::native_error("rename: no source path set")});
                }
                std::error_code ec;
                fs::rename(*src, *dst, ec);
                return rb::list_of({rb::make_boolean(!ec)});
            },
            rb::make_sign("rename", {{"dst", "std::String"}}, {{"ok", "std::Boolean"}})
        );
    }

    // file.stat() ~> (Dict) —— size / mtime / mode / is_dir.
    // file.stat() ~> (Dict) —— 大小 / 修改时间 / 权限模式 / 是否目录。
    inline rt_basic::Callable method_file_stat() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto p = rb::string_of(env["path"]);
                if (!p || p->empty()) {
                    return rb::list_of({rb::native_error("file.stat: no path set")});
                }
                std::error_code ec;
                auto s = fs::status(*p, ec);
                if (ec) return rb::list_of({rb::native_error(
                    "file.stat: " + ec.message())});
                auto d = rb::make_dict();
                auto* cls = dynamic_cast<runtime::RuntimeClass*>(d.get());
                auto& a = cls->get_attributes();
                auto put = [&](const std::string& k, RuntimeObjectPtr v) {
                    auto keyObj = rb::make_string(k);
                    std::string slot = rb::DICT_VALPRE + rb::encode_key(keyObj);
                    a[rb::DICT_KEYPRE + slot.substr(3)] = keyObj;
                    a[slot] = v;
                };
                std::int64_t sz = 0;
                if (fs::is_regular_file(s)) {
                    sz = static_cast<std::int64_t>(fs::file_size(*p, ec));
                    if (ec) sz = 0;
                }
                auto ftime = fs::last_write_time(*p, ec);
                double mtime = 0.0;
                if (!ec) {
                    auto dur = ftime.time_since_epoch();
                    mtime = static_cast<double>(
                        std::chrono::duration_cast<std::chrono::seconds>(dur).count());
                }
                std::uintmax_t mode = static_cast<std::uintmax_t>(s.permissions());
                put("size",   rb::make_int(sz));
                put("mtime",  rb::make_number(mtime));
                put("mode",   rb::make_int(static_cast<std::int64_t>(mode)));
                put("is_dir", rb::make_boolean(fs::is_directory(s)));
                return rb::list_of({d});
            },
            rb::make_sign("stat", {}, {{"info", "std::Dict"}})
        );
    }

    // file.write_atomic(text) -> (ok) —— write to a temp file then rename,
    // so a crash mid-write leaves the original intact (industrial pattern).
    // file.write_atomic(text) -> (ok) —— 写临时文件再 rename，崩溃时不留损坏文件。
    inline rt_basic::Callable method_file_write_atomic() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto p = rb::string_of(env["path"]);
                if (!p || p->empty()) {
                    return rb::list_of({rb::native_error("write_atomic: no path set")});
                }
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) {
                    return rb::list_of({rb::native_error(
                        "write_atomic requires a string argument")});
                }
                fs::path final = *p;
                fs::path tmp = final;
                tmp += ".tmp-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count());
                {
                    std::ofstream fout(tmp, std::ios::trunc | std::ios::binary);
                    if (!fout) {
                        return rb::list_of({rb::native_error(
                            "write_atomic: cannot open temp file")});
                    }
                    fout << *text;
                }
                std::error_code ec;
                fs::rename(tmp, final, ec);
                if (ec) {
                    std::error_code ignore;
                    fs::remove(tmp, ignore);
                    return rb::list_of({rb::native_error(
                        "write_atomic: rename failed: " + ec.message())});
                }
                return rb::list_of({rb::make_boolean(true)});
            },
            rb::make_sign("write_atomic", {{"text", "std::String"}}, {{"ok", "std::Boolean"}})
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
        // path helpers / 路径助手
        proto->set_method("join",      method_file_join());
        proto->set_method("basename",  method_file_basename());
        proto->set_method("dirname",   method_file_dirname());
        proto->set_method("extname",   method_file_extname());
        proto->set_method("absolute",  method_file_absolute());
        proto->set_method("normalize", method_file_normalize());
        // predicates / 谓词
        proto->set_method("is_file",      method_file_is_file());
        proto->set_method("is_dir",       method_file_is_dir());
        proto->set_method("is_readable",  method_file_is_readable());
        proto->set_method("is_writable",  method_file_is_writable());
        // directory ops / 目录操作
        proto->set_method("mkdir",     method_file_mkdir());
        proto->set_method("rmdir",     method_file_rmdir());
        proto->set_method("list_dir",  method_file_list_dir());
        proto->set_method("walk",      method_file_walk());
        // file ops / 文件操作
        proto->set_method("copy_to",      method_file_copy_to());
        proto->set_method("move_to",      method_file_move_to());
        proto->set_method("rename",       method_file_rename());
        // metadata + safe write / 元数据与原子写
        proto->set_method("stat",         method_file_stat());
        proto->set_method("write_atomic", method_file_write_atomic());

        runtime::Prototypes p;
        p.regcls("File", proto);
        ::stdRT.add_protos(p);
    }

    // Self-register so the interpreter can initialize this library.
    // 自注册，使解释器能够初始化本库。
    inline bool _registered =
        (rt_builtin::register_native_lib("file", &init_file_stdlib), true);

} // namespace rt_lib_file
