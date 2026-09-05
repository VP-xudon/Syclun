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
#include <algorithm>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/wait.h>
#  include <fcntl.h>
#  include <signal.h>
#endif

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

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

    inline std::wstring utf8_to_wide(const std::string& s) {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }

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
            if (!cwd.empty()) (void)chdir(cwd.c_str());
            dup2(outPipe[1], STDOUT_FILENO);
            dup2(errPipe[1], STDERR_FILENO);
            close(outPipe[0]); close(outPipe[1]);
            close(errPipe[0]); close(errPipe[1]);
            std::vector<char*> cargs;
            for (auto& a : argv) cargs.push_back(const_cast<char*>(a.c_str()));
            cargs.push_back(nullptr);
            if (!env.empty()) {
                std::vector<char*> cenv;
                for (auto& e : env) cenv.push_back(const_cast<char*>(e.c_str()));
                cenv.push_back(nullptr);
                execvpe(argv[0].c_str(), cargs.data(), cenv.data());
            } else {
                execvp(argv[0].c_str(), cargs.data());
            }
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

        runtime::Prototypes p;
        p.regcls("System", proto);
        ::stdRT.add_protos(p);
    }

    // Self-register so the interpreter can initialize this library.
    // 自注册，使解释器能够初始化本库。
    inline bool _registered =
        (rt_builtin::register_native_lib("system", &init_system_stdlib), true);

} // namespace rt_lib_system
