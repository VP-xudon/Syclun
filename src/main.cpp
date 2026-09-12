// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// main.cpp
//
// Synth OOP interpreter entry point.
// Synth OOP 解释器入口。
//
// Usage / 用法：
//   ./synth                  start the interactive shell / REPL（无参数启动交互式 shell）
//   ./synth program.syn      run a source file / 运行源文件
//   ./synth < program.syn    run from standard input / 从标准输入运行
//   ./synth --version        print version / 输出版本号
//   ./synth --info           print overall information / 输出整体信息
//   ./synth --config KEY     set a runtime option (NOCOLOR, COLOR, ...) / 设置运行期选项
//   ./synth --help           this help / 本帮助
//
// Runtime options (--config) / 运行期选项（--config）：
//   NOCOLOR    disable ANSI color in diagnostics / 关闭诊断中的 ANSI 颜色
//   COLOR      force ANSI color / 强制颜色
//   (further keys are reserved for future use / 其它键为预留)
// ============================================================

#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <vector>

#include "interpreter.hpp"

// Product / release version. This is the *interpreter* version, distinct from
// the language document version line (which is intentionally unversioned).
// 产品 / 发布版本号。这是*解释器*版本，与语言文档的版本线（刻意不写版本号）
// 不同。
#ifndef SYNTH_VERSION
#define SYNTH_VERSION "1.27.0"
#endif

static const char* SYNTH_NAME = "Syclun";
static const char* SYNTH_LICENSE = "GPL-3.0-or-later";

// Runtime configuration set via --config. Stored as a canonical lower-cased
// key -> value map. Reserved keys are kept so future releases can act on them.
// 经 --config 设置的运行期配置，存为「小写键 -> 值」映射；预留键一并保留，
// 便于后续版本读取。
static std::map<std::string, std::string> g_config;

// Path to this executable, used by the REPL to re-invoke a child interpreter.
// 本可执行文件的路径，供 REPL 复用于派生子解释器。
static std::string g_repl_exe;

// Resolve the standard-library directory relative to the executable so that
// `&module;` works no matter the current working directory.
// 依可执行文件位置解析标准库目录，使 `&module;` 不受当前工作目录影响。
static std::string resolve_lib_dir(int argc, char** argv) {
    std::string lib_dir = "lib";
    if (argc > 0 && argv[0] && *argv[0]) {
        if (const char* ov = std::getenv("SYNTH_LIB_DIR")) {
            if (*ov && std::filesystem::exists(ov)) lib_dir = ov;
        }
        if (lib_dir == "lib") {
            std::filesystem::path exe(argv[0]);
            std::filesystem::path cands[4] = {
                exe.parent_path() / "lib",
                exe.parent_path().parent_path() / "lib",
                exe.parent_path() / "libs",
                exe.parent_path().parent_path() / "libs",
            };
            for (const auto& cand : cands) {
                if (std::filesystem::exists(cand)) {
                    lib_dir = cand.string();
                    break;
                }
            }
        }
    }
    return lib_dir;
}

// Apply accumulated --config options to the process environment / state.
// Currently NOCOLOR/COLOR toggle the existing NO_COLOR env var consumed by the
// diagnostic color logic; other keys are stored for forward compatibility.
// 将累积的 --config 选项落到进程环境 / 状态。目前 NOCOLOR/COLOR 切换诊断配色
// 已读取的 NO_COLOR 环境变量；其它键保留以备将来使用。
static void apply_config() {
    auto it = g_config.find("nocolor");
    if (it != g_config.end()) {
        std::string v = it->second;
        if (v == "1" || v == "on" || v == "true" || v == "yes") {
#ifdef _WIN32
            _putenv("NO_COLOR=1");
#else
            setenv("NO_COLOR", "1", 1);
#endif
        }
    }
    auto itc = g_config.find("color");
    if (itc != g_config.end()) {
        std::string v = itc->second;
        if (v == "1" || v == "on" || v == "true" || v == "yes") {
#ifdef _WIN32
            _putenv("NO_COLOR=");
#else
            unsetenv("NO_COLOR");
#endif
        }
    }
}

static void print_version() {
    std::cout << SYNTH_NAME << " " << SYNTH_VERSION << "\n";
}

static void print_config() {
    std::cout << "config:\n";
    if (g_config.empty()) {
        std::cout << "  (none)\n";
    } else {
        for (auto& kv : g_config) {
            std::cout << "  " << kv.first << " = " << kv.second << "\n";
        }
    }
}

static std::string detect_platform() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

static std::string detect_compiler() {
#if defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
    return "Clang " + std::to_string(__clang_major__) + "." +
           std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    return "GCC " + std::to_string(__GNUC__) + "." +
           std::to_string(__GNUC_MINOR__);
#else
    return "unknown";
#endif
}

static long count_modules(const std::string& lib_dir) {
    long n = 0;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(lib_dir, ec)) {
        if (e.path().extension() == ".synl") ++n;
    }
    return n;
}

static void print_info(const std::string& lib_dir) {
    std::cout << SYNTH_NAME << " (Synth-OOP interpreter)\n";
    std::cout << "  version : " << SYNTH_VERSION << "\n";
    std::cout << "  license : " << SYNTH_LICENSE << "\n";
    std::cout << "  platform: " << detect_platform() << "\n";
    std::cout << "  compiler: " << detect_compiler() << "\n";
    std::cout << "  standard: C++" << __cplusplus / 100 << "\n";
    std::cout << "  built   : " << __DATE__ << " " << __TIME__ << "\n";
    std::cout << "  modules : " << count_modules(lib_dir)
              << " standard libraries in " << lib_dir << "\n";
    std::cout << "  config  :";
    if (g_config.empty()) std::cout << " (default)";
    for (auto& kv : g_config) std::cout << " " << kv.first << "=" << kv.second;
    std::cout << "\n";
}

static int run_source(const std::string& source,
                      const std::string& lib_dir,
                      const std::string& src_name) {
    interp::run_program(source, lib_dir, src_name);
    return 0;
}

// Read the whole standard input and run it. Kept for `./synth < file.syn`.
// 读取整个标准输入并运行，供 `./synth < file.syn` 使用。
static int run_stdin(const std::string& lib_dir) {
    std::stringstream buffer;
    buffer << std::cin.rdbuf();
    return run_source(buffer.str(), lib_dir, std::string());
}

static int run_file(const std::string& path, const std::string& lib_dir) {
    std::ifstream fin(path);
    if (!fin) {
        std::cerr << "error: cannot open source file '" << path << "'\n";
        return 1;
    }
    std::stringstream buffer;
    buffer << fin.rdbuf();
    return run_source(buffer.str(), lib_dir, path);
}

// ---------------------------------------------------------------------------
// Interactive shell / REPL
//
// Each entry is evaluated in a *child* Synth-OOP process (re-invoking this same
// executable on the buffered source). This isolates fatal runtime errors and
// GUI mainloops: a crash or an open window in one entry cannot corrupt the
// shell. The child inherits the environment, so --config settings (e.g.
// NOCOLOR) propagate automatically.
// 交互式 shell / REPL。每条输入在*子* Synth-OOP 进程中求值（复用同一可执行文件
// 运行缓冲的源码），从而把致命运行时错误与 GUI 主循环隔离：某条输入的崩溃或
// 打开的窗口不会影响 shell。子进程继承环境，故 --config（如 NOCOLOR）自动生效。
// ---------------------------------------------------------------------------

static std::string exe_path(int argc, char** argv) {
    if (argc > 0 && argv[0] && *argv[0]) {
        std::error_code ec;
        auto p = std::filesystem::canonical(argv[0], ec);
        if (!ec) return p.string();
        return argv[0];
    }
    return "synth";
}

static void repl_eval(const std::string& buffer, const std::string& exe) {
    if (buffer.empty()) return;

    // Write the buffered program to a temp file the child can open by path
    // (passing via stdin would re-trigger the REPL in the child).
    // 把缓冲的程序写入临时文件，供子进程按路径打开（经 stdin 传入会令子进程
    // 再次进入 REPL）。
    std::error_code ec;
    auto tmp = std::filesystem::temp_directory_path(ec) / "synth_repl.syn";
    if (ec) tmp = "synth_repl.syn";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) {
            std::cerr << "repl: cannot write temp file\n";
            return;
        }
        f << buffer;
    }

    std::string cmd = "\"" + exe + "\" \"" + tmp.string() + "\" 2>&1";
    std::cerr << "\n";
    std::unique_ptr<FILE, int(*)(FILE*)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        std::cerr << "repl: failed to start child interpreter\n";
        return;
    }
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe.get())) {
        std::cout << buf;
    }
    std::cout << std::flush;
}

static int repl(const std::string& lib_dir) {
    std::cout << SYNTH_NAME << " " << SYNTH_VERSION
              << " interactive shell. Type :help for help, :quit to exit.\n";
    std::cout << "Each entry is a complete $Program; press Enter on an empty\n";
    std::cout << "line or type :run to evaluate. Multi-line input accumulates.\n";

    std::string buffer;
    std::string line;
    while (true) {
        std::cout << (buffer.empty() ? "synth> " : "...>   ");
        std::cout.flush();
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break; // EOF (Ctrl-D / Ctrl-Z)
        }
        // Strip a trailing CR (Windows console).
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line == ":quit" || line == ":q" || line == "quit" ||
            line == "exit") {
            break;
        }
        if (line == ":help" || line == ":h") {
            std::cout
                << "  :help / :h     show this help\n"
                << "  :run  / :r     evaluate the accumulated program\n"
                << "  :clear/:reset  discard the accumulated program\n"
                << "  :quit / :q     exit the shell\n"
                << "  (empty line)   evaluate the accumulated program\n"
                << "Example:\n"
                << "  synth> -(io::OStream out); out << \"hi\";\n"
                << "  synth> :run\n";
            continue;
        }
        if (line == ":clear" || line == ":reset") {
            buffer.clear();
            continue;
        }
        if (line == ":run" || line == ":r") {
            repl_eval(buffer, g_repl_exe);
            buffer.clear();
            continue;
        }
        if (line.empty()) {
            repl_eval(buffer, g_repl_exe);
            buffer.clear();
            continue;
        }
        buffer += line;
        buffer += "\n";
    }
    return 0;
}

// exe path for the REPL child, set from main's argv.
int main(int argc, char** argv) {
    std::string lib_dir = resolve_lib_dir(argc, argv);
    g_repl_exe = exe_path(argc, argv);

    std::vector<std::string> positional;
    bool config_only = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--version" || arg == "-v") {
            print_version();
            return 0;
        }
        if (arg == "--info") {
            print_info(lib_dir);
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            print_version();
            std::cout
                << "\nUsage / 用法:\n"
                << "  synth                 interactive shell / REPL\n"
                << "  synth FILE.syn        run a source file\n"
                << "  synth < FILE.syn      run from standard input\n"
                << "  synth --version       print version\n"
                << "  synth --info          print overall information\n"
                << "  synth --config KEY    set a runtime option (NOCOLOR, COLOR, ...)\n"
                << "  synth --help          this help\n";
            return 0;
        }
        if (arg == "--config") {
            if (i + 1 < argc) {
                std::string kv = argv[++i];
                std::string k = kv, v = "1";
                auto eq = kv.find('=');
                if (eq != std::string::npos) {
                    k = kv.substr(0, eq);
                    v = kv.substr(eq + 1);
                }
                // canonicalise the key to lower case
                for (auto& c : k) c = (char)::tolower((unsigned char)c);
                g_config[k] = v;
            } else {
                config_only = true;
            }
            continue;
        }
        if (arg.rfind("--", 0) == 0) {
            std::cerr << "error: unknown option '" << arg << "'\n";
            return 2;
        }
        positional.push_back(arg);
    }

    apply_config();

    if (config_only) {
        print_config();
        return 0;
    }

    if (!positional.empty()) {
        return run_file(positional[0], lib_dir);
    }

    // No source file and no terminating flag -> interactive shell.
    // 既无源文件也无终止性标志 -> 启动交互式 shell。
    return repl(lib_dir);
}
