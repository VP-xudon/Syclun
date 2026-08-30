// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/system.hpp
//
// Standard library: system (C++-backed backend).
// 标准库：system（C++ 底层实现）。
//
// The C++ twin of lib/system.synl. It shells out to the host OS to run
// commands and to query the environment. Like file.hpp it lives in the
// standard-library directory (not in builtin.hpp) and self-registers.
// lib/system.synl 的 C++ 孪生体，负责调用宿主 OS 执行命令与环境查询。
// 与 file.hpp 一样置于标准库目录（而非 builtin.hpp）并自注册。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <thread>
#include <chrono>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_system {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // Read everything a child process prints to stdout.
    // 读取子进程打印到 stdout 的全部内容。
    inline std::string capture_command(const std::string& cmd) {
        std::string out;
#if defined(_WIN32)
        // MinGW provides the POSIX-style _popen / _pclose.
        // MinGW 提供 POSIX 风格的 _popen / _pclose。
        FILE* pipe = _popen(cmd.c_str(), "r");
#else
        FILE* pipe = popen(cmd.c_str(), "r");
#endif
        if (!pipe) {
            return "";
        }
        char buf[1024];
        while (std::fgets(buf, sizeof(buf), pipe)) {
            out += buf;
        }
#if defined(_WIN32)
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return out;
    }

    // system.run(command) ~> (output) —— stdout of the command as a String.
    // system.run(command) ~> (output) —— 命令的 stdout 作为 String。
    inline rt_basic::Callable method_system_run() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto cmd = rb::string_of(rb::para_at(paras, 0));
                if (!cmd) {
                    return rb::list_of({rb::native_error(
                        "system.run requires a command string")});
                }
                return rb::list_of({rb::make_string(capture_command(*cmd))});
            },
            rb::make_sign(
                "run", {{"command", "std::String"}}, {{"output", "std::String"}}
            )
        );
    }

    // system.run_lines(command) ~> (lines) —— stdout split into an Array.
    // system.run_lines(command) ~> (lines) —— stdout 按行拆成 Array。
    inline rt_basic::Callable method_system_run_lines() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto cmd = rb::string_of(rb::para_at(paras, 0));
                if (!cmd) {
                    return rb::list_of({rb::native_error(
                        "system.run_lines requires a command string")});
                }
                std::string out = capture_command(*cmd);
                auto arr = ::stdRT.make("Array");
                auto* cls = dynamic_cast<runtime::RuntimeClass*>(arr.get());
                auto& aenv = cls->get_attributes();
                std::size_t i = 0;
                std::stringstream ss(out);
                std::string line;
                while (std::getline(ss, line)) {
                    aenv[rb::elem_key(i)] = rb::make_string(line);
                    ++i;
                }
                rb::set_container_size(aenv, i);
                return rb::list_of({arr});
            },
            rb::make_sign(
                "run_lines",
                {{"command", "std::String"}},
                {{"lines", "std::Array"}}
            )
        );
    }

    // system.cwd() ~> (path) —— current working directory.
    // system.cwd() ~> (path) —— 当前工作目录。
    inline rt_basic::Callable method_system_cwd() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr /*paras*/) {
                // std::filesystem::current_path works on every target.
                // std::filesystem::current_path 在各平台均可。
                std::string dir = std::filesystem::current_path().string();
                return rb::list_of({rb::make_string(dir)});
            },
            rb::make_sign("cwd", {}, {{"path", "std::String"}})
        );
    }

    // system.getenv(name) ~> (value) —— value of an environment variable
    // (empty string when unset).
    // system.getenv(name) ~> (value) —— 环境变量的值（未设置时为空串）。
    inline rt_basic::Callable method_system_getenv() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto name = rb::string_of(rb::para_at(paras, 0));
                if (!name) {
                    return rb::list_of({rb::native_error(
                        "system.getenv requires a name string")});
                }
                // MinGW / POSIX both provide std::getenv.
                // MinGW / POSIX 均提供 std::getenv。
                const char* v = std::getenv(name->c_str());
                return rb::list_of({rb::make_string(v ? v : "")});
            },
            rb::make_sign(
                "getenv", {{"name", "std::String"}}, {{"value", "std::String"}}
            )
        );
    }

    // system.wait(ms) -> (void) —— sleep for `ms` integer milliseconds.
    // system.wait(ms) -> (void) —— 休眠 ms 个整数毫秒。
    inline rt_basic::Callable method_system_wait() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto ms = rb::number_of(rb::para_at(paras, 0));
                if (!ms) {
                    return rb::list_of({rb::native_error(
                        "system.wait requires an integer millisecond count")});
                }
                // Truncate to a whole-number millisecond delay.
                // 截断为整数毫秒的休眠时长。
                long long msLL = static_cast<long long>(*ms);
                if (msLL < 0) msLL = 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(msLL));
                return rb::empty_result();
            },
            rb::make_sign(
                "wait", {{"ms", "std::Number"}}, {}
            )
        );
    }

    // Format a std::tm into a fixed-width string via std::strftime.
    // 用 std::strftime 把 std::tm 格式化为定宽字符串。
    inline std::string fmt_time(const std::tm& tm, const char* fmt) {
        char buf[64];
        std::strftime(buf, sizeof(buf), fmt, &tm);
        return std::string(buf);
    }

    // system.now() ~> (ms) —— milliseconds since the Unix epoch.
    // system.now() ~> (ms) —— 自 Unix 纪元起的毫秒数。
    inline rt_basic::Callable method_system_now() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr /*paras*/) {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                return rb::list_of({rb::make_number(static_cast<double>(ms))});
            },
            rb::make_sign("now", {}, {{"ms", "std::Number"}})
        );
    }

    // system.time() ~> (hhmmss) —— local time as "HH:MM:SS".
    // system.time() ~> (hhmmss) —— 本地时间，格式 "HH:MM:SS"。
    inline rt_basic::Callable method_system_time() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr /*paras*/) {
                std::time_t t = std::time(nullptr);
                std::tm tm;
#if defined(_WIN32)
                localtime_s(&tm, &t);
#else
                localtime_r(&t, &tm);
#endif
                return rb::list_of({rb::make_string(fmt_time(tm, "%H:%M:%S"))});
            },
            rb::make_sign("time", {}, {{"hhmmss", "std::String"}})
        );
    }

    // system.date() ~> (yyyymmdd) —— local date as "YYYY-MM-DD".
    // system.date() ~> (yyyymmdd) —— 本地日期，格式 "YYYY-MM-DD"。
    inline rt_basic::Callable method_system_date() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr /*paras*/) {
                std::time_t t = std::time(nullptr);
                std::tm tm;
#if defined(_WIN32)
                localtime_s(&tm, &t);
#else
                localtime_r(&t, &tm);
#endif
                return rb::list_of({rb::make_string(fmt_time(tm, "%Y-%m-%d"))});
            },
            rb::make_sign("date", {}, {{"yyyymmdd", "std::String"}})
        );
    }

    // system.datetime() ~> (stamp) —— local date-time "YYYY-MM-DD HH:MM:SS".
    // system.datetime() ~> (stamp) —— 本地日期时间 "YYYY-MM-DD HH:MM:SS"。
    inline rt_basic::Callable method_system_datetime() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr /*paras*/) {
                std::time_t t = std::time(nullptr);
                std::tm tm;
#if defined(_WIN32)
                localtime_s(&tm, &t);
#else
                localtime_r(&t, &tm);
#endif
                return rb::list_of({rb::make_string(fmt_time(tm, "%Y-%m-%d %H:%M:%S"))});
            },
            rb::make_sign("datetime", {}, {{"stamp", "std::String"}})
        );
    }

    // ---- registration / 登记 ----
    inline void init_system_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(
            ::stdRT.getcls("Object")
        );
        proto->set_method("run",       method_system_run());
        proto->set_method("run_lines", method_system_run_lines());
        proto->set_method("cwd",       method_system_cwd());
        proto->set_method("getenv",    method_system_getenv());
        proto->set_method("wait",      method_system_wait());
        proto->set_method("now",       method_system_now());
        proto->set_method("time",      method_system_time());
        proto->set_method("date",      method_system_date());
        proto->set_method("datetime",  method_system_datetime());

        runtime::Prototypes p;
        p.regcls("System", proto);
        ::stdRT.add_protos(p);
    }

    // Self-register so the interpreter can initialize this library.
    // 自注册，使解释器能够初始化本库。
    inline bool _registered =
        (rt_builtin::register_native_lib("system", &init_system_stdlib), true);

} // namespace rt_lib_system
