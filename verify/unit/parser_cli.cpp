// parser_cli: a standalone child-process entry point used by
// assert_parser to verify that "syntax errors must be rejected".
// parser_cli：独立子进程入口，供 assert_parser 验证“语法错误应被拒绝”。
//
// On success it prints PARSE_OK; on failure Thrower prints a
// SyntaxError and exits (exit code is always 0, so the caller tells
// success from failure by inspecting the output text).
// 成功：打印 PARSE_OK；失败：Thrower 打印 SyntaxError 并 exit(0)。
// 调用方依据输出区分成功 / 失败（exit 码恒为 0，故看输出内容）。
#include "../../src/parser.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: parser_cli <source-file>" << std::endl;
        return 2;
    }
    std::ifstream f(argv[1]);
    if (!f) {
        std::cerr << "cannot open file: " << argv[1] << std::endl;
        return 2;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string src = ss.str();
    parser::Parser p(src);
    p.parse_program();
    std::cout << "PARSE_OK" << std::endl;
    return 0;
}
