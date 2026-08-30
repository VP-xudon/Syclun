// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// main.cpp
//
// Synth OOP interpreter entry point.
// Synth OOP 解释器入口。
//
// Reads a Synth OOP source program from a file argument (argv[1]) or from
// standard input, then parses and runs it via interp::run_program. The
// program's entry is the `$Program` class's `@::` (construct) behavior, which
// prints to the standard output through io::OStream.
// 从文件参数（argv[1]）或标准输入读取 Synth OOP 源程序，
// 经 interp::run_program 解析并运行。程序的入口是 `$Program` 类的
// `@::`（构造）行为，它通过 io::OStream 向标准输出打印。
//
// Build / 构建：
//   g++ -std=c++23 -O2 main.cpp -o synth
// Usage / 用法：
//   ./synth program.syn        run a source file
//   ./synth < program.syn      run from standard input
// ============================================================

#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <filesystem>

#include "interpreter.hpp"

int main(int argc, char** argv) {
    std::string source;

    if (argc > 1) {
        // Read the source file named by the first argument.
        // 读取第一个参数指定的源文件。
        std::ifstream fin(argv[1]);
        if (!fin) {
            std::cerr << "error: cannot open source file '" << argv[1] << "'\n";
            return 1;
        }
        std::stringstream buffer;
        buffer << fin.rdbuf();
        source = buffer.str();
    } else {
        // Otherwise read everything from standard input.
        // 否则从标准输入读取全部内容。
        std::stringstream buffer;
        buffer << std::cin.rdbuf();
        source = buffer.str();
    }

    // Resolve the standard-library directory relative to the executable so
    // that `&module;` works no matter the current working directory. We look
    // for a `lib/` next to the executable, then one level up (the layout
    // build/synth.exe + lib/ at the project root).
    // 依可执行文件位置解析标准库目录，使 `&module;` 不受当前工作目录影响。
    // 先找可执行文件同级的 lib/，再找上一级的 lib/（布局为
    // build/synth.exe 与项目根下的 lib/）。
    std::string lib_dir = "lib";
    if (argc > 0 && argv[0] && *argv[0]) {
        namespace fs = std::filesystem;
        // An explicit override wins (handy for relocatable / packaged installs
        // that place the libraries somewhere unusual).
        // 显式覆盖优先（便于把库放到非常规位置的可重定位 / 打包安装）。
        if (const char* ov = std::getenv("SYNTH_LIB_DIR")) {
            if (*ov && fs::exists(ov)) lib_dir = ov;
        }
        // Otherwise search relative to the executable. Order
        // 否则依可执行文件位置搜索。顺序：
        //   <exe_dir>/lib          current layout: bin/synth + lib/
        //   <exe_dir>/../lib       build/synth.exe + root lib/
        //   <exe_dir>/libs         distribution layout: bin/synth + libs/
        //   <exe_dir>/../libs
        if (lib_dir == "lib") {
            fs::path exe(argv[0]);
            fs::path cands[4] = {
                exe.parent_path() / "lib",
                exe.parent_path().parent_path() / "lib",
                exe.parent_path() / "libs",
                exe.parent_path().parent_path() / "libs",
            };
            for (const auto& cand : cands) {
                if (fs::exists(cand)) {
                    lib_dir = cand.string();
                    break;
                }
            }
        }
    }

    // run_program drives the whole pipeline: lex -> parse -> define types ->
    // instantiate $Program (runs @::) -> execute. Syntax / runtime errors are
    // reported by Thrower and terminate the process. When reading from a file
    // we pass its path so diagnostics show "file:line:col" (g++-style).
    // run_program 驱动整条管线：词法 -> 句法 -> 定义类型 ->
    // 实例化 $Program（运行 @::）-> 执行。句法 / 运行时错误由 Thrower
    // 上报并终止进程。从文件读取时传入其路径，使诊断呈现 "file:line:col"
    // （类 g++）。
    std::string src_name = (argc > 1) ? std::string(argv[1]) : std::string();
    interp::run_program(source, lib_dir, src_name);

    return 0;
}
