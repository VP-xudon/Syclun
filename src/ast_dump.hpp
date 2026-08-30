// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// ast_dump.hpp
//
// Debug / inspection helper for the parser AST. Kept OUT of
// parser.hpp on purpose: dumping a tree is a testing / debugging
// convenience, not part of the parser's job, so the parser stays
// focused on producing the AST. Acceptance tests (assert_parser.cpp)
// include this header when they want a readable view.
// 解析器 AST 的调试 / 检视辅助。刻意放在 parser.hpp 之外：打印 AST
// 树是测试 / 调试用的便利工具，不属于 Parser 的职责，故 Parser 只
// 专注于产出 AST。验收代码（assert_parser.cpp）需要可读视图时
// 再包含本头文件。
// ============================================================

#ifndef AST_DUMP_HPP
#define AST_DUMP_HPP

#include <string>
#include <vector>

#include "parser.hpp"

namespace parser {

    // Append an indented, human-readable rendering of `node` to `out`.
    // 将 `node` 的缩进可读渲染追加到 `out`。
    inline void dump_to(std::string& out, const AstNodePtr& node,
                        int depth) {
        if (!node) {
            out += std::string(depth * 2, ' ') + "<null>\n";
            return;
        }
        std::string line(depth * 2, ' ');
        line += node->kind;
        if (!node->value.empty()) {
            line += " `" + node->value + "`";
        }
        if (!node->name.empty()) {
            line += " name:" + node->name;
        }
        if (!node->mode.empty()) {
            line += " mode:" + node->mode;
        }
        if (node->isConst) {
            line += " !const";
        }
        if (node->isPrivate) {
            line += " #private";
        }
        if (node->isPlaceholder) {
            line += " _";
        }
        line += "  @" + std::to_string(node->line);
        out += line + "\n";
        for (const auto& kid : node->kids) {
            dump_to(out, kid, depth + 1);
        }
    }

    // Render the whole tree rooted at `root` and return it as a string.
    // 渲染以 `root` 为根的整棵树并返回字符串。
    inline std::string dump(const AstNodePtr& root) {
        std::string out;
        dump_to(out, root, 0);
        return out;
    }

} // namespace parser

#endif
