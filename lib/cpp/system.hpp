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
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <ctime>
#include <filesystem>
#include <thread>
#include <chrono>
#include <algorithm>
#include <tuple>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <unistd.h>
#  include <sys/wait.h>
#  include <fcntl.h>
#  include <signal.h>
#  if defined(__APPLE__)
#    include <crt_externs.h>
#  endif
#endif

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

// On Linux, glibc exports the global `environ` (the process environment block).
// This declaration lives in the GLOBAL namespace on purpose: declaring it inside
// `namespace rt_lib_system` would create an unrelated undefined symbol
// `rt_lib_system::environ` at link time on POSIX (the bug that broke the macOS /
// Linux CI builds). macOS does not export `environ` directly and uses
// `_NSGetEnviron()` from <crt_externs.h> instead.
#if defined(__linux__)
extern char** environ;
#endif

namespace rt_lib_system {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // Result of a child process: exit status + captured stdout/stderr.
    // 子进程结果：退出状态 + 捕获的 stdout/stderr。
    struct ProcResult {
        int status = -1;          // -1 with empty out/err means launch failure
        std::string out;
        std::string err;
    };

    // The OS-specific argv prefix that invokes the user's shell. `run` (shell
    // form) and `exec` (safe, no shell) are unified on one executor: `run`
    // passes this prefix, `exec` passes the user's argv verbatim — so `exec`
    // can never be a command-injection sink.
    // 调用宿主 shell 的 OS 专属 argv 前缀。`run`（shell 形式）与
    // `exec`（安全、无 shell）统一在一种执行器上：run 传入此前缀，exec
    // 原样传入用户 argv——故 exec 绝不会成为命令注入点。
    inline std::vector<std::string> shell_argv(const std::string& cmd) {
#if defined(_WIN32)
        return {"cmd", "/c", cmd};
#else
        return {"sh", "-c", cmd};
#endif
    }
#ifdef _WIN32
    inline std::wstring utf8_to_wide(const std::string& s) {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }
#else
    inline std::wstring utf8_to_wide(const std::string& s) { return {}; }
#endif
    // Run argv WITHOUT a shell. Captures stdout/stderr, honours an optional
    // timeout (ms; 0 = wait forever), cwd and an explicit environment. This is
    // the safe default introduced by the industrialization audit (D7).
    // 不经过 shell 运行 argv。捕获 stdout/stderr，支持可选超时（ms；0 表示
    // 无限等待）、cwd 与显式环境变量。这是工业化审计 D7 引入的安全默认。
    inline ProcResult exec_no_shell(const std::vector<std::string>& argv,
                                    long long timeoutMs,
                                    const std::string& cwd,
                                    const std::vector<std::string>& env) {
        ProcResult res;
        if (argv.empty()) { res.err = "exec: empty argv"; return res; }
#if defined(_WIN32)
        // ---- Windows: CreateProcessW, no cmd.exe / sh in the middle. ----
        std::wstring cmdline;
        for (auto& a : argv) {
            std::wstring w = utf8_to_wide(a);
            cmdline += L'"' + w + L"\" ";
        }
        SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = nullptr;
        HANDLE hOutR, hOutW, hErrR, hErrW;
        if (!CreatePipe(&hOutR, &hOutW, &sa, 0) ||
            !CreatePipe(&hErrR, &hErrW, &sa, 0)) {
            res.err = "exec: cannot create pipes"; return res;
        }
        SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(hErrR, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hOutW; si.hStdError = hErrW;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

        std::wstring wcdir = cwd.empty() ? L"" : utf8_to_wide(cwd);
        std::wstring envblock;
        if (!env.empty()) {
            std::vector<std::wstring> wen;
            for (auto& e : env) wen.push_back(utf8_to_wide(e));
            std::sort(wen.begin(), wen.end());
            for (auto& e : wen) { envblock += e; envblock += L'\0'; }
            envblock += L'\0';
        }

        PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
        BOOL ok = CreateProcessW(
            nullptr, &cmdline[0], nullptr, nullptr, TRUE, 0,
            env.empty() ? nullptr : envblock.data(),
            wcdir.empty() ? nullptr : wcdir.c_str(), &si, &pi);
        CloseHandle(hOutW); CloseHandle(hErrW);
        if (!ok) {
            DWORD e = GetLastError();
            res.err = "exec: CreateProcess failed (code " + std::to_string(e) + ")";
            CloseHandle(hOutR); CloseHandle(hErrR);
            return res;
        }
        std::thread tout([&]() {
            char b[4096]; DWORD r = 0;
            while (ReadFile(hOutR, b, sizeof(b), &r, nullptr) && r > 0)
                res.out.append(b, r);
        });
        std::thread terr([&]() {
            char b[4096]; DWORD r = 0;
            while (ReadFile(hErrR, b, sizeof(b), &r, nullptr) && r > 0)
                res.err.append(b, r);
        });
        DWORD wr = WaitForSingleObject(pi.hProcess,
                        timeoutMs > 0 ? (DWORD)timeoutMs : INFINITE);
        if (wr == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 1);
            res.status = -1;
        } else {
            DWORD ec = 0; GetExitCodeProcess(pi.hProcess, &ec); res.status = (int)ec;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(hOutR); CloseHandle(hErrR);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        tout.join(); terr.join();
        return res;
#else
        // ---- POSIX: fork + execvp, pipes for stdout/stderr. ----
        int outPipe[2], errPipe[2];
        if (pipe(outPipe) < 0 || pipe(errPipe) < 0) {
            res.err = "exec: cannot create pipes"; return res;
        }
        pid_t pid = fork();
        if (pid < 0) { res.err = "exec: fork failed"; return res; }
        if (pid == 0) {
            if (!cwd.empty()) std::ignore = chdir(cwd.c_str());
            dup2(outPipe[1], STDOUT_FILENO);
            dup2(errPipe[1], STDERR_FILENO);
            close(outPipe[0]); close(outPipe[1]);
            close(errPipe[0]); close(errPipe[1]);
            std::vector<char*> cargs;
            for (auto& a : argv) cargs.push_back(const_cast<char*>(a.c_str()));
            cargs.push_back(nullptr);
            for (const auto& e : env) {
                size_t eq = e.find('=');
                if (eq != std::string::npos && eq > 0)
                    setenv(e.substr(0, eq).c_str(), e.substr(eq + 1).c_str(), 1);
            }
            execvp(argv[0].c_str(), cargs.data());
            _exit(127);
        }
        close(outPipe[1]); close(errPipe[1]);
        std::thread tout([&]() {
            char b[4096]; ssize_t r;
            while ((r = read(outPipe[0], b, sizeof(b))) > 0) res.out.append(b, (size_t)r);
        });
        std::thread terr([&]() {
            char b[4096]; ssize_t r;
            while ((r = read(errPipe[0], b, sizeof(b))) > 0) res.err.append(b, (size_t)r);
        });
        int status = 0;
        if (timeoutMs > 0) {
            long long waited = 0;
            while (waited < timeoutMs) {
                int w = waitpid(pid, &status, WNOHANG);
                if (w == pid) break;
                if (w < 0) break;
                usleep(5000); waited += 5;
            }
            if (waited >= timeoutMs) {
                kill(pid, SIGKILL); waitpid(pid, &status, 0); res.status = -1;
            } else {
                res.status = WEXITSTATUS(status);
            }
        } else {
            waitpid(pid, &status, 0);
            res.status = WEXITSTATUS(status);
        }
        close(outPipe[0]); close(errPipe[0]);
        tout.join(); terr.join();
        return res;
#endif
    }

    // Pull an argv Array out of a parameter list of SYNTH Array.
    // 从 SYNTH Array 参数中取出 argv 向量。
    inline std::vector<std::string> argv_from(const rt_basic::InstanceListPtr& paras, int idx) {
        std::vector<std::string> v;
        auto arr = rb::para_at(paras, idx);
        if (!arr) return v;
        auto* src = rb::attributes_of(arr);
        if (!src) return v;
        std::size_t n = rb::container_size(*src);
        for (std::size_t i = 0; i < n; ++i) {
            auto it = src->find(rb::elem_key(i));
            if (it != src->end()) {
                auto s = rb::string_of(it->second);
                if (s) v.push_back(*s);
            }
        }
        return v;
    }


    // system.run(command) ~> (status, stdout, stderr) —— shell form. Still
    // goes through a shell (so metacharacters are interpreted), therefore it is
    // UNSAFE for untrusted input. Prefer `exec` for untrusted data.
    // system.run(command) ~> (status, stdout, stderr) —— shell 形式。仍经
    // shell（故元字符会被解释），故对未信任输入不安全。未信任数据优先用 exec。
    inline rt_basic::Callable method_system_run() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto cmd = rb::string_of(rb::para_at(paras, 0));
                if (!cmd) {
                    return rb::list_of({rb::native_error(
                        "system.run requires a command string")});
                }
                auto t = rb::number_of(rb::para_at(paras, 1));
                long long timeoutMs = t ? static_cast<long long>(*t) : 0;
                ProcResult r = exec_no_shell(shell_argv(*cmd), timeoutMs, "", {});
                return rb::list_of({rb::make_tuple({
                    rb::make_number(static_cast<double>(r.status)),
                    rb::make_string(r.out),
                    rb::make_string(r.err)
                })});
            },
            rb::make_sign(
                "run",
                {{"command", "std::String"}, {"timeout_ms", "std::Number"}},
                {{"result", "std::Tuple"}}
            )
        );
    }

    // system.run_lines(command) ~> (lines) —— stdout of the shell form split
    // into an Array (one element per line). Shell form: unsafe for untrusted.
    // system.run_lines(command) ~> (lines) —— shell 形式的 stdout 按行拆成 Array。
    // shell 形式：对未信任输入不安全。
    inline rt_basic::Callable method_system_run_lines() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto cmd = rb::string_of(rb::para_at(paras, 0));
                if (!cmd) {
                    return rb::list_of({rb::native_error(
                        "system.run_lines requires a command string")});
                }
                ProcResult r = exec_no_shell(shell_argv(*cmd), 0, "", {});
                auto arr = ::stdRT.make("Array");
                auto* cls = dynamic_cast<runtime::RuntimeClass*>(arr.get());
                auto& aenv = cls->get_attributes();
                std::size_t i = 0;
                std::stringstream ss(r.out);
                std::string line;
                while (std::getline(ss, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
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

    // system.exec(argv, [timeout_ms], [cwd], [env]) ~> (status, stdout, stderr)
    // —— the SAFE default. `argv` is an Array of strings run WITHOUT a shell,
    // so no metacharacter in the arguments can be interpreted as a command.
    // `env` is an Array of "KEY=VALUE" strings (optional; inherit if omitted).
    // system.exec(argv, [timeout_ms], [cwd], [env]) ~> (status, stdout, stderr)
    // —— 安全默认。argv 为字符串数组，不经 shell 运行，故参数中的元字符
    // 绝不会被当作命令解释。env 为 "KEY=VALUE" 字符串数组（可选；省略则继承）。
    inline rt_basic::Callable method_system_exec() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto argv = argv_from(paras, 0);
                if (argv.empty()) {
                    return rb::list_of({rb::native_error(
                        "system.exec requires a non-empty argv Array")});
                }
                auto t = rb::number_of(rb::para_at(paras, 1));
                long long timeoutMs = t ? static_cast<long long>(*t) : 0;
                std::string cwd;
                auto cwdObj = rb::para_at(paras, 2);
                if (cwdObj) { auto s = rb::string_of(cwdObj); if (s) cwd = *s; }
                std::vector<std::string> env_pairs;   // not 'env': the lambda
                    // parameter is the object's attribute map, and shadowing it
                    // trips -Werror=shadow. / 避免与 lambda 参数 env（对象属性表）
                    // 重名触发 -Werror=shadow。
                auto envObj = rb::para_at(paras, 3);
                if (envObj) {
                    auto* src = rb::attributes_of(envObj);
                    if (src) {
                        std::size_t n = rb::container_size(*src);
                        for (std::size_t i = 0; i < n; ++i) {
                            auto it = src->find(rb::elem_key(i));
                            if (it != src->end()) {
                                auto s = rb::string_of(it->second);
                                if (s) env_pairs.push_back(*s);
                            }
                        }
                    }
                }
                ProcResult r = exec_no_shell(argv, timeoutMs, cwd, env_pairs);
                return rb::list_of({rb::make_tuple({
                    rb::make_number(static_cast<double>(r.status)),
                    rb::make_string(r.out),
                    rb::make_string(r.err)
                })});
            },
            rb::make_sign(
                "exec",
                {{"argv", "std::Array"}, {"timeout_ms", "std::Number"},
                 {"cwd", "std::String"}, {"env", "std::Array"}},
                {{"result", "std::Tuple"}}
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

    // system.monotonic() ~> (ms) —— milliseconds of a monotonic clock, immune to
    // wall-clock adjustments (so it is the right clock for benchmarking).
    // system.monotonic() ~> (ms) —— 单调时钟毫秒数，不受墙上时钟调整影响
    // （故是基准测试应使用的时钟）。
    inline rt_basic::Callable method_system_monotonic() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr /*paras*/) {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count();
                return rb::list_of({rb::make_number(static_cast<double>(ms))});
            },
            rb::make_sign("monotonic", {}, {{"ms", "std::Number"}})
        );
    }

    // system.os_name() ~> (name) —— "windows" | "linux" | "macos" | "unknown".
    // system.os_name() ~> (name) —— 编译期确定的操作系统名。
    inline rt_basic::Callable method_system_os_name() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr /*paras*/) {
#if defined(_WIN32)
                std::string n = "windows";
#elif defined(__APPLE__)
                std::string n = "macos";
#elif defined(__linux__)
                std::string n = "linux";
#else
                std::string n = "unknown";
#endif
                return rb::list_of({rb::make_string(n)});
            },
            rb::make_sign("os_name", {}, {{"name", "std::String"}})
        );
    }

    // system.arch() ~> (name) —— "x86_64" | "arm64" | "x86" | "arm" | "unknown",
    // resolved at compile time.
    // system.arch() ~> (name) —— 编译期确定的 CPU 架构名。
    inline rt_basic::Callable method_system_arch() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr /*paras*/) {
#if defined(_M_X64) || defined(__x86_64__)
                std::string a = "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
                std::string a = "arm64";
#elif defined(_M_IX86) || defined(__i386__)
                std::string a = "x86";
#elif defined(_M_ARM) || defined(__arm__)
                std::string a = "arm";
#else
                std::string a = "unknown";
#endif
                return rb::list_of({rb::make_string(a)});
            },
            rb::make_sign("arch", {}, {{"name", "std::String"}})
        );
    }

    // system.setenv(name, value) -> (void) —— set an environment variable for the
    // current process (and any child process spawned afterwards). `name` must be
    // non-empty.
    // system.setenv(name, value) -> (void) —— 为当前进程（及之后派生的子进程）
    // 设置环境变量。name 必须非空。
    inline rt_basic::Callable method_system_setenv() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto name = rb::string_of(rb::para_at(paras, 0));
                auto val  = rb::string_of(rb::para_at(paras, 1));
                if (!name || name->empty() || !val) {
                    return rb::list_of({rb::native_error(
                        "system.setenv requires a non-empty name and a value")});
                }
#if defined(_WIN32)
                std::wstring wn = utf8_to_wide(*name);
                std::wstring wv = utf8_to_wide(*val);
                _wputenv_s(wn.c_str(), wv.c_str());
#else
                setenv(name->c_str(), val->c_str(), 1);
#endif
                return rb::empty_result();
            },
            rb::make_sign(
                "setenv", {{"name", "std::String"}, {"value", "std::String"}}, {}
            )
        );
    }

    // system.unsetenv(name) -> (void) —— remove an environment variable.
    // system.unsetenv(name) -> (void) —— 移除一个环境变量。
    inline rt_basic::Callable method_system_unsetenv() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto name = rb::string_of(rb::para_at(paras, 0));
                if (!name || name->empty()) {
                    return rb::list_of({rb::native_error(
                        "system.unsetenv requires a non-empty name")});
                }
#if defined(_WIN32)
                std::wstring wn = utf8_to_wide(*name);
                _wputenv_s(wn.c_str(), L"");
#else
                unsetenv(name->c_str());
#endif
                return rb::empty_result();
            },
            rb::make_sign("unsetenv", {{"name", "std::String"}}, {})
        );
    }

    // system.environ() ~> (vars) —— Array of "KEY=VALUE" strings for the current
    // process environment (the leading-'=' service entries are skipped on
    // Windows).
    // system.environ() ~> (vars) —— 当前进程环境的 "KEY=VALUE" 字符串数组。
    inline rt_basic::Callable method_system_environ() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr /*paras*/) {
                auto arr = ::stdRT.make("Array");
                auto* cls = dynamic_cast<runtime::RuntimeClass*>(arr.get());
                auto& aenv = cls->get_attributes();
                std::size_t i = 0;
#if defined(_WIN32)
                wchar_t* env = GetEnvironmentStringsW();
                if (env) {
                    for (wchar_t* p = env; *p; ) {
                        std::wstring w(p);
                        // Skip the leading-'=' service entries (e.g. "=::=...").
                        // 跳过以 '=' 开头的服务条目（如 "=::=..."）。
                        if (!w.empty() && w[0] != L'=') {
                            int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                (int)w.size(), nullptr, 0, nullptr, nullptr);
                            std::string s(n, '\0');
                            WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                (int)w.size(), s.data(), n, nullptr, nullptr);
                            aenv[rb::elem_key(i++)] = rb::make_string(s);
                        }
                        p += std::wcslen(p) + 1;
                    }
                    FreeEnvironmentStringsW(env);
                }
#else
                char** envp =
                #if defined(__APPLE__)
                    *_NSGetEnviron()
                #else
                    environ
                #endif
                    ;
                for (char** e = envp; e && *e; ++e) {
                    aenv[rb::elem_key(i++)] = rb::make_string(std::string(*e));
                }
#endif
                rb::set_container_size(aenv, i);
                return rb::list_of({arr});
            },
            rb::make_sign("environ", {}, {{"vars", "std::Array"}})
        );
    }

    // Process command line, captured once. On POSIX we read /proc/self/cmdline;
    // on Windows we use GetCommandLineW + CommandLineToArgvW.
    // 进程命令行，仅捕获一次。POSIX 读取 /proc/self/cmdline；Windows 用
    // GetCommandLineW + CommandLineToArgvW。
    inline const std::vector<std::string>& proc_argv() {
        static std::vector<std::string> cached;
        static bool done = false;
        if (done) return cached;
        done = true;
#if defined(_WIN32)
        int argc = 0;
        wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (wargv) {
            for (int i = 0; i < argc; ++i) {
                int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                    nullptr, 0, nullptr, nullptr);
                std::string s(n, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), n,
                    nullptr, nullptr);
                if (!s.empty() && s.back() == '\0') s.pop_back();
                cached.push_back(s);
            }
            LocalFree(wargv);
        }
#else
        std::ifstream f("/proc/self/cmdline");
        if (f) {
            std::string buf((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
            std::string cur;
            for (char c : buf) {
                if (c == '\0') {
                    if (!cur.empty()) cached.push_back(cur);
                    cur.clear();
                } else cur.push_back(c);
            }
            if (!cur.empty()) cached.push_back(cur);
        }
#endif
        return cached;
    }

    // system.argv() ~> (args) —— Array of the interpreter's command-line args.
    // system.argv() ~> (args) —— 解释器命令行参数数组。
    inline rt_basic::Callable method_system_argv() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr /*paras*/) {
                const auto& a = proc_argv();
                auto arr = ::stdRT.make("Array");
                auto* cls = dynamic_cast<runtime::RuntimeClass*>(arr.get());
                auto& aenv = cls->get_attributes();
                for (std::size_t i = 0; i < a.size(); ++i)
                    aenv[rb::elem_key(i)] = rb::make_string(a[i]);
                rb::set_container_size(aenv, a.size());
                return rb::list_of({arr});
            },
            rb::make_sign("argv", {}, {{"args", "std::Array"}})
        );
    }

    // system.argc() ~> (count) —— number of command-line args.
    // system.argc() ~> (count) —— 命令行参数个数。
    inline rt_basic::Callable method_system_argc() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr /*paras*/) {
                return rb::list_of({rb::make_number(
                    static_cast<double>(proc_argv().size()))});
            },
            rb::make_sign("argc", {}, {{"count", "std::Number"}})
        );
    }

    // system.exit(code) -> (noreturn) —— terminate the interpreter process with
    // the given status. Intended for scripts that must stop early with a specific
    // code; it ends the whole process (including any pending output flush).
    // system.exit(code) -> (noreturn) —— 以给定状态码终止解释器进程。用于需要
    // 提前以特定状态码退出的脚本；会结束整个进程（含待刷新的输出）。
    inline rt_basic::Callable method_system_exit() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto code = rb::number_of(rb::para_at(paras, 0));
                int c = code ? static_cast<int>(*code) : 0;
                std::exit(c);
                return rb::empty_result();   // unreachable / 不可达
            },
            rb::make_sign("exit", {{"code", "std::Number"}}, {})
        );
    }

    // ---- registration / 登记 ----
    inline void init_system_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(
            ::stdRT.getcls("Object")
        );
        proto->set_method("run",       method_system_run());
        proto->set_method("run_lines", method_system_run_lines());
        proto->set_method("exec",      method_system_exec());
        proto->set_method("cwd",       method_system_cwd());
        proto->set_method("getenv",    method_system_getenv());
        proto->set_method("wait",      method_system_wait());
        proto->set_method("now",       method_system_now());
        proto->set_method("time",      method_system_time());
        proto->set_method("date",      method_system_date());
        proto->set_method("datetime",  method_system_datetime());
        proto->set_method("monotonic", method_system_monotonic());
        proto->set_method("os_name",   method_system_os_name());
        proto->set_method("arch",      method_system_arch());
        proto->set_method("setenv",    method_system_setenv());
        proto->set_method("unsetenv",  method_system_unsetenv());
        proto->set_method("environ",   method_system_environ());
        proto->set_method("argv",      method_system_argv());
        proto->set_method("argc",      method_system_argc());
        proto->set_method("exit",      method_system_exit());

        runtime::Prototypes p;
        p.regcls("System", proto);
        ::stdRT.add_protos(p);
    }

    // Self-register so the interpreter can initialize this library.
    // 自注册，使解释器能够初始化本库。
    inline bool _registered =
        (rt_builtin::register_native_lib("system", &init_system_stdlib), true);

} // namespace rt_lib_system
