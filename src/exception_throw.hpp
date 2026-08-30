// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

#include <string>
#include <iostream>
#include <vector>
#include <cstdlib>

#ifndef EXCEPTIONS_HPP
#define EXCEPTIONS_HPP

// ============================================================
// exception_throw.hpp
//   Unified diagnostic reporter for the Synth OOP interpreter.
//   Synth OOP 统一诊断上报器。
//
//   Design (post poison-water retirement):
//   - No more "Data-Flow Tree" / box-drawing. Diagnostics are typeset as
//     labeled fields (Type / Location / Message) with the offending source
//     line and caret, plus a real execution stack — NOT a g++ single-line
//     clone. Pure ASCII output (no box-drawing) so it never garbles in a
//     non-UTF-8 / GBK console; color is used only for subtle highlighting.
//   设计（毒水模型退役后）：
//   - 不再有“数据流树”/制表符。诊断以字段化排版呈现（类型 / 位置 / 消息），
//     附出错源码行与插入符及真实执行栈——并非 g++ 单行格式的翻版。输出纯
//     ASCII（无 box-drawing），在非 UTF-8 / GBK 控制台不致乱码；颜色仅作
//     适度高亮。
//   - A real execution stack (diag::call_stack) is collected as the
//     interpreter enters/exits user methods, so runtime errors show the
//     full chain of activations with line numbers instead of placeholder
//     strings such as "<Interpreter>" / "<rt_builtin>".
//   解释器进出用户方法时收集真实执行栈（diag::call_stack），运行时错误据此
//   呈现完整调用链与行号，取代 <Interpreter>/<rt_builtin> 占位符。
//   - Subtle ANSI color highlighting (can be disabled with NO_COLOR).
//   适度 ANSI 颜色高亮（可由 NO_COLOR 关闭）。
// ============================================================

namespace diag {

    // A single activation record in the execution stack.
    // 执行栈中的一条激活记录。
    struct StackFrame {
        std::string label;            // e.g. "in method 'Counter::inc'"
        std::string file  = "<program>";
        long long   line  = 0;
        long long   col   = 0;
    };

    // The locus of the node currently being evaluated. Set per AST node by
    // the interpreter; used as the primary error location. Builtins that
    // raise inherit the last interpreter-set locus (the call site).
    // 当前求值节点的位置。解释器按 AST 节点设置，作为报错主位置。
    // 原生库深处抛错时沿用解释器最后设置的位置（即调用点）。
    inline StackFrame& cur_locus() {
        static StackFrame s;
        return s;
    }

    // Set the current error locus (file / line / col). The label is cleared
    // because a fresh error site has no activation label of its own; the
    // execution-stack frames carry the labels instead.
    // 设置当前报错位置（文件 / 行 / 列）。label 清空，因为新的报错点自身
    // 不带激活标签，标签由执行栈帧承载。
    inline void set_locus(const std::string& file, long long line,
                          long long col) {
        StackFrame& s = cur_locus();
        s.file = file;
        s.line = line;
        s.col  = col;
        s.label.clear();
    }

    // The execution stack, pushed/popped by the interpreter / runtime as they
    // enter and leave user-method activations.
    // 执行栈：解释器 / 运行时进出用户方法激活时压入 / 弹出。
    inline std::vector<StackFrame>& call_stack() {
        static std::vector<StackFrame> s;
        return s;
    }

    // Source text, split by lines, for printing the offending line (g++ style).
    // 源文本（按行切分），用于打印出错行（类 g++）。
    inline std::vector<std::string>& source_lines() {
        static std::vector<std::string> s;
        return s;
    }
    inline std::string& source_file() {
        static std::string s = "<program>";
        return s;
    }

    // Width of the caret marker (the "^~~~" run). The highlighted span in the
    // source line uses the same width, so the in-line highlight and the caret
    // beneath it always cover exactly the same characters.
    // 插入符（"^~~~"）宽度。源码行中的高亮片段采用同一宽度，使行内高亮与
    // 其下方插入符始终覆盖完全相同的字符。
    inline constexpr std::size_t CARET_WIDTH = 4;

    // UTF-8-safe slicing helpers: never split a multi-byte sequence. A source
    // line may carry CJK comments, and a highlight/caret that began or ended
    // in the middle of such a character would emit a partial byte (garbling
    // the console). Rewinding / advancing over continuation bytes (10xxxxxx)
    // keeps every slice aligned to a character boundary.
    // UTF-8 安全切片：绝不切断多字节序列。源码行可能含中文注释，若高亮 /
    // 插入符的起点或终点落在某字符中间，就会输出半个字形字节（控制台乱码）。
    // 前退 / 后跨过续字节（10xxxxxx）可保证切片对齐到字符边界。
    inline std::size_t utf8_safe_start(const std::string& s, std::size_t i) {
        if (i >= s.size()) return s.size();
        while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) --i;
        return i;
    }
    inline std::size_t utf8_safe_end(const std::string& s, std::size_t i) {
        while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) ++i;
        return i;
    }

    // ---- g++-like ANSI coloring -------------------------------------
    inline bool color_enabled() {
        static int cached = -1;
        if (cached != -1) return cached != 0;
        const char* nc = std::getenv("NO_COLOR");
        cached = (nc && *nc) ? 0 : 1;
        return cached != 0;
    }
    inline std::string C(const char* code) {
        return color_enabled() ? ("\033[" + std::string(code) + "m") : "";
    }
    inline std::string R() { return C("0"); }

    inline void emit(
        const std::string& severity,    // "error" / "warning" / "note"
        const std::string& type,         // exception class name
        const std::string& message
    ) {
        const StackFrame& loc = cur_locus();
        bool isWarn = (severity == "warning");
        std::string sevCol  = isWarn ? C("1;35") : C("1;31"); // magenta / red
        std::string headCol = C("1;31");                       // red header
        std::string dimCol  = C("1;36");                       // cyan-ish dim
        std::string emphCol = C("1");                           // bold label
        // Underlined severity color: highlights the offending span inside the
        // source line so the error site stands out in the line itself.
        // 带下划线的严重色：高亮源码行内出错片段，使错误位置在行内凸显。
        std::string hlCol   = isWarn ? C("1;35;4") : C("1;31;4");

        // ---- Header / 标题（纯 ASCII，避免 box-drawing 在非 UTF-8/GHK
        //      控制台乱码；仅用颜色作高亮，不复刻 g++ 的单行风格） ----
        std::cerr << '\n'
                  << headCol
                  << (isWarn ? "Synth-OOP warning" : "Synth-OOP error")
                  << R() << '\n';

        // ---- Labeled fields / 字段 ----
        std::string file = loc.file.empty() ? "<program>" : loc.file;
        long long L = (loc.line > 0) ? loc.line : 0;
        long long Cc = (loc.col > 0) ? loc.col : 1;
        std::cerr << "  " << emphCol << "Type" << R() << "     "
                  << sevCol << type << R() << '\n';
        std::cerr << "  " << emphCol << "Location" << R() << " "
                  << dimCol << file << R() << ", line " << L
                  << ", column " << Cc << '\n';
        std::cerr << "  " << emphCol << "Message" << R() << "  "
                  << message << '\n';

        // ---- Offending source line + caret / 出错源码行 + 插入符 ----
        if (loc.line > 0
                && loc.line <= (long long)source_lines().size()) {
            const std::string& lineText = source_lines()[loc.line - 1];
            std::string num = std::to_string(loc.line);
            // Print the line with the offending span highlighted, so the error
            // site is visible IN the line itself and not only beneath it.
            // 打印该行并高亮出错片段，使错误位置「在行内」即可见，而不仅是
            // 行下方有插入符。
            std::size_t start = utf8_safe_start(
                lineText, (std::size_t)((Cc > 1) ? (Cc - 1) : 0));
            std::size_t wantEnd = start + CARET_WIDTH;
            if (wantEnd > lineText.size()) wantEnd = lineText.size();
            std::size_t end = utf8_safe_end(lineText, wantEnd);
            std::cerr << '\n'
                      << "    " << dimCol << num << R() << " | "
                      << lineText.substr(0, start)
                      << hlCol << lineText.substr(start, end - start) << R()
                      << lineText.substr(end)
                      << '\n';
            // Caret line alignment: its prefix must be EXACTLY as wide as the
            // source line's prefix (4 spaces + number + " | "), otherwise the
            // caret drifts one column left of the real error site. The prefix
            // built here is  indent(4 + num.size() + 1) + '|' + ' '
            //   == 4 spaces + number + " | "  (both put '|' at the same column
            //      and start payload at the same column).
            // 插入符行对齐：其前缀宽度必须与源码行前缀（4 空格 + 行号 +
            // " | "）完全一致，否则插入符会相对真实出错位置左偏一列。此处
            // 前缀为 indent(4 + num.size() + 1) + '|' + ' '
            //   == 4 空格 + 行号 + " | "（二者把 '|' 与正文起始列对齐）。
            long long caretCol = (loc.col > 1) ? loc.col - 1 : 0;
            std::string indent(4 + num.size() + 1, ' ');
            // Clip the caret to the highlighted span so both always cover
            // exactly the same characters (it matters at end-of-line, where
            // fewer than CARET_WIDTH characters remain after the error site).
            // 把插入符裁剪到高亮片段长度，使二者始终覆盖完全相同的字符
            // （行尾在出错点之后不足 CARET_WIDTH 个字符时尤为要紧）。
            std::size_t spanChars = (end > start) ? (end - start) : 1;
            std::string marker = "^" + std::string(spanChars - 1, '~');
            std::string caret((size_t)caretCol, ' ');
            caret += sevCol + marker + R();
            std::cerr << indent << dimCol << "|" << R() << " " << caret << '\n';
        }

        // ---- Execution stack / 执行栈 ----
        if (!call_stack().empty()) {
            std::cerr << '\n' << emphCol << "Call stack" << R() << '\n';
            int depth = 1;
            for (auto& fr : call_stack()) {
                std::string floc
                    = (fr.file.empty() ? "<program>" : fr.file)
                    + ", line " + std::to_string(fr.line)
                    + ", column " + std::to_string(fr.col > 0 ? fr.col : 1);
                std::cerr << "    [" << depth++ << "] " << fr.label
                          << "  " << dimCol << "(" << floc << ")" << R() << '\n';
            }
        }
    }

} // namespace diag

class ExceptionThrower {
    public:

    ExceptionThrower() {}

    // Exit status handed back to the shell when a fatal error is reported.
    // It MUST be non-zero: `synth bad.syn && echo ok` must not print "ok",
    // and CI has to be able to see that the run failed. Success stays 0,
    // which main() returns when run_program completes normally.
    // 报告致命错误时交还 shell 的退出码。它**必须非 0**：
    // `synth bad.syn && echo ok` 不应打印 ok，CI 也必须能看出运行失败。
    // 成功仍为 0，由 main() 在 run_program 正常结束时返回。
    static constexpr int kFatalExitStatus = 1;

    // Primary API: name + message. Locus and stack come from diag:: globals.
    // 主接口：名称 + 消息；位置与栈取自 diag 全局。
    void throwE(const std::string &exception_name,
                const std::string &exception_message) {
        diag::emit("error", exception_name, exception_message);
        exit(kFatalExitStatus);
    }

    // Legacy 3-arg form kept for call-site compatibility. The single-element
    // `go_way` marker (e.g. "<Interpreter>") is intentionally dropped: the
    // real execution stack (diag::call_stack) now supplies the context.
    // 旧式三参形式保留以兼容既有调用点；原 `go_way` 标记（如 "<Interpreter>"）
    // 不再使用——真实执行栈（diag::call_stack）已提供上下文。
    void throwE(const std::string &exception_name,
                const std::string &exception_message,
                const std::vector<std::string> & /*go_way*/) {
        throwE(exception_name, exception_message);
    }
} Thrower;

#endif
