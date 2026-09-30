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
#include <cctype>
#include <map>
#include <vector>

#include "interpreter.hpp"
#include "exception_throw.hpp"   // reuse diag::color_enabled() / diag::C()

// Windows headers must come AFTER the Synth-OOP headers: <windows.h> defines
// macros CONST / STRICT that collide with enum identifiers in runtime.hpp
// (e.g. BehavStateOBJ::CONST), which would otherwise fail to compile. We also
// undef them defensively once the Win32 API we use is in scope.
// Windows 头必须排在 Synth-OOP 头文件之后：<windows.h> 把 CONST / STRICT 定义为
// 宏，会与 runtime.hpp 的枚举标识符（如 BehavStateOBJ::CONST）冲突。用到的
// Win32 API 入域后再防御性 undef。
#ifdef _WIN32
#  include <windows.h>
#  include <conio.h>
#  undef CONST
#  undef STRICT
#else
#  include <termios.h>
#  include <unistd.h>
#endif

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

// Resolve the standard-library directory so that `&module;` works no matter
// where the executable, the program file, or the current working directory are.
// 解析标准库目录，使 `&module;` 不受可执行文件、程序文件或当前工作目录位置影响。
//
// Candidates (each tried for both `lib` and `libs`, so either packaging
// convention works): 候选目录（每个都同时尝试 `lib` 与 `libs` 两种命名，
// 兼容两种打包约定）：
//   - $SYNTH_LIB_DIR (env override)        环境变量覆盖
//   - executable dir / parent / grandparent 可执行文件所在目录及其上两层
//   - program-file dir / parent            被运行的 .syn 文件所在目录及其上层
//   - current working directory / parent   当前工作目录及其上层
static std::string resolve_lib_dir(int argc, char** argv) {
    // 0) Environment override. / 环境变量覆盖。
    if (argc > 0 && argv[0] && *argv[0]) {
        if (const char* ov = std::getenv("SYNTH_LIB_DIR")) {
            if (*ov && std::filesystem::exists(ov)) return ov;
        }
    }

    std::vector<std::filesystem::path> bases;
    std::error_code ec;

    // 1) Executable location (dir, parent, grandparent).
    //    可执行文件位置（本目录、父目录、祖父目录）。
    if (argc > 0 && argv[0] && *argv[0]) {
        std::filesystem::path exe(argv[0]);
        auto p = exe.parent_path();
        if (!p.empty()) {
            bases.push_back(p);
            bases.push_back(p.parent_path());
            bases.push_back(p.parent_path().parent_path());
        }
    }

    // 2) Program-file location (argv[1]) — lets users keep `libs/` next to the
    //    .syn they run. 程序文件位置（argv[1]）——允许用户把 `libs/` 放在所运行
    //    的 .syn 旁边。
    if (argc > 1 && argv[1] && *argv[1]) {
        std::filesystem::path src(argv[1]);
        auto sp = src.parent_path();
        if (!sp.empty()) {
            bases.push_back(sp);
            bases.push_back(sp.parent_path());
        }
    }

    // 3) Current working directory (and its parent).
    //    当前工作目录（及其父目录）。
    auto cwd = std::filesystem::current_path(ec);
    if (!ec) {
        bases.push_back(cwd);
        bases.push_back(cwd.parent_path());
    }

    const char* names[] = {"lib", "libs"};
    for (const auto& base : bases) {
        if (base.empty()) continue;
        for (const char* n : names) {
            std::filesystem::path cand = base / n;
            std::error_code e2;
            if (std::filesystem::is_directory(cand, e2)) {
                return cand.string();
            }
        }
    }

    // Fallback: a plain relative `lib` (resolved against the CWD at open time).
    // 回退：普通相对路径 `lib`（在打开时相对当前目录解析）。
    return "lib";
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

// ---------------------------------------------------------------------------
// REPL helpers: ANSI output, raw (per-keystroke) input, live highlighting
// REPL 辅助：ANSI 输出、原始（逐键）输入、即时高亮
// ---------------------------------------------------------------------------

// Enable ANSI virtual-terminal processing on the console output so that the
// REPL's own highlighting and the child's diagnostic colors both render.
// 在控制台输出上启用 ANSI 虚拟终端处理，使 REPL 自身高亮与子进程诊断配色都能
// 正常显示。
static void enable_vt_output() {
#ifdef _WIN32
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hout == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (GetConsoleMode(hout, &mode)) {
        SetConsoleMode(hout, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

// ---- raw console mode (per-keystroke editing) ----
static bool g_raw_enabled = false;
#ifdef _WIN32
static DWORD g_old_in_mode = 0;
static HANDLE g_hin = INVALID_HANDLE_VALUE;
#else
static struct termios g_old_term;
#endif

static void enable_raw_mode() {
#ifdef _WIN32
    g_hin = GetStdHandle(STD_INPUT_HANDLE);
    if (g_hin == INVALID_HANDLE_VALUE) return;
    if (!GetConsoleMode(g_hin, &g_old_in_mode)) return;
    // Keep processed-input (so Ctrl-C still arrives as a byte) but drop line
    // editing and echo so we can redraw the line ourselves.
    // 保留 processed-input（Ctrl-C 仍以字节到达），仅去掉行编辑与回显，
    // 以便自行重绘当前行。
    SetConsoleMode(g_hin, g_old_in_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
    g_raw_enabled = true;
#else
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &g_old_term) != 0) return;
    struct termios raw = g_old_term;
    raw.c_lflag &= ~(ICANON | ECHO);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) g_raw_enabled = true;
#endif
}

static void disable_raw_mode() {
    if (!g_raw_enabled) return;
#ifdef _WIN32
    SetConsoleMode(g_hin, g_old_in_mode);
#else
    tcsetattr(STDIN_FILENO, TCSANOW, &g_old_term);
#endif
    g_raw_enabled = false;
}

static int repl_getch() {
#ifdef _WIN32
    return _getch();
#else
    return getchar();
#endif
}

static long repl_pid() {
#ifdef _WIN32
    return (long)GetCurrentProcessId();
#else
    return (long)getpid();
#endif
}

// Token-based syntax coloring for a single Synth-OOP line. The language has no
// reserved keywords (true/false/void are identifiers), so we color by lexical
// category: strings, line comments, numbers, the distinctive sigils
// (& $ @ :: =: := -> ! and friends), and brackets. ANSI codes are stripped when
// color is disabled (NO_COLOR), so the returned string is safe to print.
// 按词法类别为单行 Synth-OOP 着色。本语言无保留关键字（true/false/void 皆为
// 标识符），故按词法类着色：字符串、行注释、数字、特征符号（& $ @ :: =: := -> !
// 等）与括号。关闭颜色（NO_COLOR）时去掉 ANSI 码，返回值可直接打印。
static std::string highlight_synth(const std::string& s) {
    if (!diag::color_enabled()) return s;
    const std::string Cstr = diag::C("32");   // string  -> green
    const std::string Ccom = diag::C("90");   // comment -> bright black / dim
    const std::string Cnum = diag::C("33");   // number  -> yellow
    const std::string Csig = diag::C("35");   // sigil/op -> magenta
    const std::string Cbrk = diag::C("36");   // bracket -> cyan
    const std::string R    = diag::R();
    std::string out;
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        char c = s[i];
        // line comment  // ...
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            size_t j = i;
            while (j < n && s[j] != '\n') ++j;
            out += Ccom + s.substr(i, j - i) + R;
            i = j;
            continue;
        }
        // string literal " ... "
        if (c == '"') {
            size_t j = i + 1;
            while (j < n) {
                if (s[j] == '\\' && j + 1 < n) { j += 2; continue; }
                if (s[j] == '"') { ++j; break; }
                ++j;
            }
            out += Cstr + s.substr(i, j - i) + R;
            i = j;
            continue;
        }
        // number (digit run; allow a leading dot before a digit)
        if (std::isdigit((unsigned char)c) ||
            (c == '.' && i + 1 < n && std::isdigit((unsigned char)s[i + 1]))) {
            size_t j = i;
            while (j < n && (std::isalnum((unsigned char)s[j]) || s[j] == '.' ||
                             s[j] == '_'))
                ++j;
            out += Cnum + s.substr(i, j - i) + R;
            i = j;
            continue;
        }
        // two-char sigils
        if (i + 1 < n) {
            std::string two = s.substr(i, 2);
            if (two == "::" || two == "=:" || two == ":=" || two == "->" ||
                two == "&&" || two == "||" || two == "==" || two == "!=" ||
                two == "<=" || two == ">=") {
                out += Csig + two + R;
                i += 2;
                continue;
            }
        }
        // single sigils / operators
        if (c == '&' || c == '$' || c == '@' || c == '!' || c == '=' ||
            c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
            c == '<' || c == '>' || c == '?' || c == ':') {
            out += Csig + std::string(1, c) + R;
            ++i;
            continue;
        }
        // brackets / separators
        if (c == '(' || c == ')' || c == '{' || c == '}' || c == '[' ||
            c == ']' || c == ';' || c == ',' || c == '.') {
            out += Cbrk + std::string(1, c) + R;
            ++i;
            continue;
        }
        // anything else (identifiers, whitespace)
        out += c;
        ++i;
    }
    return out;
}

// Read one physical line with live, PowerShell-style highlighting. In raw mode
// the current line is recolored and redrawn after every keystroke; otherwise
// (input not a TTY, e.g. piped) we fall back to a plain getline so scripts and
// tests still work. Returns false on EOF.
// 读取一行并做 PowerShell 式即时高亮。原始模式下每按一键即重绘当前行；否则
// （非 TTY，如管道输入）退回普通 getline，保证脚本/测试仍可用。EOF 时返回 false。
static bool repl_read_line(const std::string& prompt, std::string& out_line,
                           std::vector<std::string>& history, long& hist_idx) {
    out_line.clear();
    if (!g_raw_enabled) {
        std::cout << prompt << std::flush;
        std::string l;
        if (!std::getline(std::cin, l)) return false;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out_line = l;
        return true;
    }

    std::string line;
    int pos = 0;
    const int prompt_len = (int)prompt.size();   // prompt is ASCII
    auto render = [&]() {
        std::cout << "\r\x1b[K" << prompt << highlight_synth(line);
        int col = prompt_len + pos + 1;           // 1-based cursor column
        std::cout << "\x1b[" << col << "G";
        std::cout.flush();
    };
    render();

    while (true) {
        int ch = repl_getch();
        if (ch == 3) {                 // Ctrl-C: cancel the current line
            std::cout << "\r\x1b[K" << prompt << diag::C("36") << "^C"
                      << diag::R() << "\n";
            out_line.clear();
            return true;
        }
        if (ch == 4) {                 // Ctrl-D
            if (line.empty()) { std::cout << "\n"; return false; }
            continue;                  // ignore mid-line Ctrl-D
        }
        if (ch == 13 || ch == 10) {    // Enter
            std::cout << "\r\x1b[K" << prompt << highlight_synth(line) << "\n";
            if (!line.empty() && (history.empty() || history.back() != line)) {
                history.push_back(line);
            }
            hist_idx = (long)history.size();
            out_line = line;
            return true;
        }
        if (ch == 8 || ch == 127) {    // Backspace / DEL
            if (pos > 0) { line.erase(pos - 1, 1); --pos; render(); }
            continue;
        }
        // arrow / editing keys
        int arrow = -1;
#ifdef _WIN32
        if (ch == 0 || ch == 0xE0) arrow = repl_getch();
#else
        if (ch == 27) {                // ESC [ ...
            if (repl_getch() == '[') arrow = repl_getch();
        }
#endif
        if (arrow != -1) {
            // normalize Windows extended codes and Unix CSI finals to a small
            // set we handle. 归一化 Windows 扩展键码与 Unix CSI 尾码。
            int key = arrow;
#ifndef _WIN32
            if (key == 'A') key = 72; else if (key == 'B') key = 80;
            else if (key == 'C') key = 77; else if (key == 'D') key = 75;
            else if (key == 'H') key = 71; else if (key == 'F') key = 79;
#endif
            if (key == 75) {                 // left
                if (pos > 0) { --pos; render(); }
            } else if (key == 77) {          // right
                if (pos < (int)line.size()) { ++pos; render(); }
            } else if (key == 71) {          // home
                pos = 0; render();
            } else if (key == 79) {          // end
                pos = (int)line.size(); render();
            } else if (key == 72) {          // up: older history
                if (!history.empty() && hist_idx > 0) {
                    --hist_idx;
                    line = history[hist_idx];
                    pos = (int)line.size();
                    render();
                }
            } else if (key == 80) {          // down: newer history
                if (!history.empty() && hist_idx < (long)history.size() - 1) {
                    ++hist_idx;
                    line = history[hist_idx];
                    pos = (int)line.size();
                    render();
                } else if (!history.empty()) {
                    hist_idx = (long)history.size();
                    line.clear(); pos = 0; render();
                }
            }
            continue;
        }
        // printable ASCII / Latin-1
        if (ch >= 32 && ch != 127) {
            line.insert(pos, 1, (char)ch);
            ++pos;
            render();
        }
    }
}

// Pick a directory for the REPL's scratch file that the *native* child process
// can actually open. std::filesystem::temp_directory_path() honours the TMP/TEMP
// environment variables, which under some shells (e.g. Git Bash) point at a
// POSIX-style '/tmp' that the Windows runtime/cmd.exe cannot open -- that
// surfaced as ERROR_INVALID_NAME ("文件名、目录名或卷标语法不正确"). We therefore
// only trust the system temp dir when it is a real, absolute, writable Windows
// path; otherwise we fall back to the current working directory (which the
// native executable provably opens, as all the `synth file.syn` runs do).
// 为 REPL 临时文件挑选原生子进程真正打得的目录。temp_directory_path() 受
// TMP/TEMP 环境变量影响，在部分 shell（如 Git Bash）下指向 POSIX 风格的 '/tmp'，
// 原生 Windows 运行时/cmd.exe 无法打开——这正是 ERROR_INVALID_NAME
// （“文件名、目录名或卷标语法不正确”）的来源。故仅当系统临时目录是真实、
// 绝对且可写的 Windows 路径时才采用；否则退回当前工作目录（原生 exe 已证明
// 能打开，所有 `synth file.syn` 运行皆是如此）。
static std::filesystem::path repl_scratch_dir() {
    std::error_code ec;
    std::filesystem::path t = std::filesystem::temp_directory_path(ec);
    if (!ec) {
        std::error_code wec;
        std::filesystem::create_directories(t, wec);
        std::filesystem::path probe =
            t / ("synth_repl_probe_" + std::to_string(repl_pid()) + ".tmp");
        {
            std::ofstream pf(probe, std::ios::binary);
            if (pf) { pf << 'x'; }
        }
        std::error_code rec;
        std::filesystem::remove(probe, rec);
        if (t.is_absolute() && t.string().find(':') != std::string::npos)
            return t;     // genuine Windows path
    }
    std::error_code cec;
    auto cwd = std::filesystem::current_path(cec);
    return cec ? std::filesystem::path(".") : cwd;
}

// Launch the child interpreter on `src_path` and return its combined
// stdout+stderr. On Windows we use CreateProcess (no shell): the POSIX MSYS `sh`
// that MinGW's popen routes through mangles backslash drive paths such as
// `V:\...\synth.exe` into "文件名、目录名或卷标语法不正确". Going straight to
// CreateProcess opens the image by path directly, so the path can never be
// mangled. On other platforms popen is used as before.
// 在子进程中按路径运行解释器并返回其合并后的 stdout+stderr。Windows 上改用
// CreateProcess（不经 shell）：MinGW 的 popen 经由 POSIX 版 MSYS `sh`，会把
// `V:\...\synth.exe` 这样的反斜杠盘符路径篡改，导致“文件名、目录名或卷标语法
// 不正确”。直接走 CreateProcess 按路径打开映像，路径就不会被篡改。其它平台沿用
// popen。
static std::wstring repl_widen(const std::string& s) {
#ifdef _WIN32
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
#else
    return std::wstring();
#endif
}

#ifdef _WIN32
static std::string repl_run_child(const std::wstring& app, const std::wstring& args) {
    // Command line: "<app>" <args> (CreateProcess may rewrite the buffer).
    // 命令行："<app>" <args>（CreateProcess 可能改写该缓冲）。
    std::wstring cmd = L"\"" + app + L"\" " + args;
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return std::string("repl: CreatePipe failed\n");
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);   // keep read end in parent
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;                                  // merge stderr -> stdout
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    std::wstring cmdBuf = cmd;                              // CreateProcess may modify
    BOOL ok = CreateProcessW(app.c_str(), &cmdBuf[0], nullptr, nullptr, TRUE,
                             0, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);                                    // close parent copy -> EOF on exit
    std::string out;
    if (ok) {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(hRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
            out.append(buf, (size_t)n);
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        DWORD err = GetLastError();
        LPSTR msgBuf = nullptr;
        DWORD sz = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                      FORMAT_MESSAGE_FROM_SYSTEM |
                                      FORMAT_MESSAGE_IGNORE_INSERTS,
                                  nullptr, err, 0, (LPSTR)&msgBuf, 0, nullptr);
        std::string msg = sz ? std::string(msgBuf, sz) : "unknown error";
        if (msgBuf) LocalFree(msgBuf);
        out = "repl: cannot start child interpreter: " + msg;
    }
    CloseHandle(hRead);
    return out;
}
#endif

static void repl_eval(const std::string& buffer, const std::string& exe) {
    if (buffer.empty()) return;

    // Write the buffered program to a scratch file the child opens by path
    // (stdin would re-trigger the REPL in the child). Use a uniquely named file
    // in a natively-accessible directory and clean it up afterwards.
    // 把缓冲程序写入临时文件供子进程按路径打开（stdin 会令子进程再次进入
    // REPL）。使用原生可访问目录下的唯一文件名，并在事后清理。
    std::error_code ec;
    std::filesystem::path dir = repl_scratch_dir();
    static unsigned long long s_counter = 0;
    std::filesystem::path tmp =
        dir / ("synth_repl_" + std::to_string(repl_pid()) + "_" +
               std::to_string(++s_counter) + ".syn");
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) {
            std::cerr << "repl: cannot write temp file '" << tmp.string() << "'\n";
            return;
        }
        f << buffer;
    }

    std::cerr << "\n";
#ifdef _WIN32
    std::string child_out =
        repl_run_child(repl_widen(exe),
                       L"\"" + repl_widen(std::filesystem::absolute(tmp).string()) + L"\"");
#else
    std::string cmd = "\"" + exe + "\" \"" +
                      std::filesystem::absolute(tmp).string() + "\" 2>&1";
    std::string child_out;
    std::unique_ptr<FILE, int(*)(FILE*)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        child_out = "repl: failed to start child interpreter\n";
    } else {
        char buf[4096];
        while (fgets(buf, sizeof(buf), pipe.get())) child_out += buf;
    }
#endif
    std::cout << child_out;
    std::cout << std::flush;
    std::error_code rec;
    std::filesystem::remove(tmp, rec);
}

static int repl(const std::string& lib_dir) {
    std::cout << SYNTH_NAME << " " << SYNTH_VERSION
              << " interactive shell. Type :help for help, :quit to exit.\n";
    std::cout << "Each entry is a complete $Program; press Enter on an empty\n";
    std::cout << "line or type :run to evaluate. Multi-line input accumulates.\n";

    // Enable ANSI colors on the console and switch to per-keystroke input so the
    // line editor can repaint with live syntax highlighting. When stdin is not a
    // TTY (piped input / automated tests) raw mode is skipped and repl_read_line
    // falls back to a plain getline, so behavior is unchanged there.
    // 在控制台启用 ANSI 并切到逐键输入，使行编辑器能即时重绘高亮。当 stdin 非
    // TTY（管道/自动化测试）时跳过原始模式，repl_read_line 退回普通 getline，
    // 行为不受影响。
    enable_vt_output();
    enable_raw_mode();

    std::string buffer;
    std::string line;
    std::vector<std::string> history;
    long hist_idx = 0;
    while (true) {
        std::string prompt = buffer.empty() ? "synth> " : "...>   ";
        if (!repl_read_line(prompt, line, history, hist_idx)) {
            std::cout << "\n";
            break; // EOF (Ctrl-D / Ctrl-Z)
        }

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
                << "  synth> &io;\n"
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
    disable_raw_mode();
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
