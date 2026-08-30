// ============================================================
// assert_parser.cpp
//   Parser acceptance tests: structural assertions verify that the
//   AST obeys "Synth OOP Language Spec v1.26" and the syntax rules
//   set by the language designer.
//   Parser 验收：用结构断言验证 AST 符合《Synth OOP 语言文档
//   v1.26》及语言设计者钦定的句法约束。
//
//   Style matches assert_runtimes.cpp: check() / section() plus
//   passed / failed counters. Valid sources are parsed directly and
//   their node structure is asserted; invalid sources (which trigger
//   Thrower.exit) are verified through the companion parser_cli.exe
//   child process.
//   风格与 assert_runtimes.cpp 对齐：check() / section() + passed /
//   failed 统计。合法源直接解析并断言节点结构；错误源（会触发
//   Thrower.exit）通过配套 parser_cli.exe 子进程验证其被拒绝。
// ============================================================
#include "../../src/parser.hpp"
#include "../../src/ast_dump.hpp"   // exercises the extracted debug/dump utility

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>   // locate the sibling parser_cli.exe from argv[0] / 依 argv[0] 定位同级 parser_cli.exe
#ifndef _WIN32
#include <sys/wait.h>   // WIFEXITED / WEXITSTATUS: decode std::system() status / 解码 std::system() 状态
#endif

static int passed = 0;
static int failed = 0;
static int section_no = 0;

static void section(const std::string& title) {
    section_no++;
    std::cout << "\n=== [" << section_no << "] " << title << " ===" << std::endl;
}
static void check(bool cond, const std::string& what) {
    if (cond) {
        passed++;
        std::cout << "  [OK]   " << what << std::endl;
    } else{
        failed++;
        std::cout << "  [FAIL] " << what << std::endl;
    }
}

// ---- AST traversal helpers / AST 遍历辅助 ----
static parser::AstNodePtr first_kind(const parser::AstNodePtr& n,
                                     const std::string& kind) {
    if (!n) return nullptr;
    if (n->kind == kind) return n;
    for (auto& k : n->kids) {
        auto r = first_kind(k, kind);
        if (r) return r;
    }
    return nullptr;
}
static int count_kind(const parser::AstNodePtr& n, const std::string& kind) {
    if (!n) return 0;
    int c = (n->kind == kind) ? 1 : 0;
    for (auto& k : n->kids) c += count_kind(k, kind);
    return c;
}

// Parse a valid source (invalid sources exit the process, so they
// must be checked via a child process).
// 解析合法源（非法源会让进程退出，需用子进程验证）。
static parser::AstNodePtr parse(const std::string& src) {
    parser::Parser p(src);
    return p.parse_program();
}

// Wrap a statement inside a method body and return its AST node
// (used for closure-level expression / flow tests).
// 把一条语句包进方法体，返回其 AST 节点（用于闭包内表达式/流测试）。
static parser::AstNodePtr first_stmt(const std::string& stmt) {
    std::string src = "$A { @m << [()->() {\n" + stmt + "\n}]; }";
    auto root = parse(src);
    auto cls = root->kids[0];            // classdef
    auto body = cls->kids.back();        // block (kids[0] when no parent)
    auto mdef = body->kids[0];           // methoddef
    auto beh = mdef->kids[0];            // behavior
    auto blk = beh->kids[1];             // block (behavior body)
    return blk->kids[0];
}

// ---- invalid-source child-process check / 错误源子进程验证 ----
// Path to the sibling parser_cli.exe, resolved from argv[0] so the check works
// no matter which directory assert_parser is launched from.
// 同级 parser_cli.exe 的路径，由 argv[0] 推导，使本检查在任何工作目录下都能运行。
static std::string g_cli_path = "parser_cli.exe";

// Result of one parser_cli child-process run: captured output plus its exit
// status, already decoded to a plain exit code.
// 一次 parser_cli 子进程运行的结果：捕获的输出 + 已解码为普通退出码的状态。
struct CliResult {
    std::string out;
    int code = -1;
};

// Portable decoding of std::system()'s return value: on Windows it already is
// the child's exit code; on POSIX it must be unwrapped.
// std::system() 返回值的可移植解码：Windows 下它本身就是子进程退出码；
// POSIX 下需另行拆解。
static int cli_exit_code(int status) {
#ifdef _WIN32
    return status;
#else
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

static CliResult run_cli(const std::string& src, const std::string& tmp) {
    {
        std::ofstream f(tmp);
        f << src;
    }
    std::string cmd = g_cli_path + " " + tmp + " > _cli_out.txt 2>&1";
    int status = std::system(cmd.c_str());
    std::ifstream o("_cli_out.txt");
    std::stringstream ss;
    ss << o.rdbuf();
    return CliResult{ss.str(), cli_exit_code(status)};
}
static void check_rejected(const std::string& src, const std::string& what) {
    CliResult r = run_cli(src, "_neg_tmp.sy");
    bool is_err = r.out.find("SyntaxError") != std::string::npos
               && r.out.find("PARSE_OK") == std::string::npos;
    check(is_err, what + " (should be rejected with a SyntaxError)");
}

int main(int argc, char** argv) {
    // Resolve the sibling parser_cli.exe path so the negative tests can spawn
    // it regardless of the current working directory.
    // 推导同级 parser_cli.exe 的路径，使错误源子进程检查不受工作目录影响。
    if (argc > 0 && argv[0] && *argv[0]) {
        namespace fs = std::filesystem;
        fs::path self(argv[0]);
        g_cli_path = (self.parent_path() / "parser_cli.exe").string();
    }
    // ----------------------------------------------------------
    section("Top-level whitelist: import / class / contract");
    {
        auto root = parse("&io;");
        check(root->kind == "program" && root->kids.size() == 1,
              "single import -> program with 1 item");
        check(first_kind(root, "import") != nullptr
              && first_kind(root, "import")->value == "io", "import node value=io");

        root = parse("$A { -(std::Number x); }");
        check(first_kind(root, "classdef") != nullptr, "class definition accepted");
        check(first_kind(root, "classdef")->value == "A", "class name is A");

        root = parse("#C { @m << [()->()]; }");
        check(first_kind(root, "contractdef") != nullptr, "contract definition accepted");
        check(first_kind(root, "contractdef")->value == "C", "contract name is C");

        root = parse("&io;\n$A { -(std::Number x); }\n#C { @m << [()->()]; }");
        check(root->kids.size() == 3, "all three top-level items coexist (3 items)");
        check(root->kids[0]->kind == "import"
              && root->kids[1]->kind == "classdef"
              && root->kids[2]->kind == "contractdef",
              "item kinds and order are correct");
    }

    // ----------------------------------------------------------
    section("Class body: variable definitions + method injections");
    {
        auto root = parse(
            "$A {\n"
            "  -(std::Number x);\n"
            "  -(std::String s) << \"hi\";\n"
            "  @add << [(std::Number a) -> (std::Number) {\n"
            "    x << x.+(a);\n"
            "  }];\n"
            "}");
        auto cls = first_kind(root, "classdef");
        auto body = cls->kids.back();            // block (kids[0] when no parent)
        check(body->kind == "block", "class body is a block");
        int vardefs = 0, methods = 0;
        for (auto& k : body->kids) {
            if (k->kind == "vardef") vardefs++;
            if (k->kind == "methoddef") methods++;
        }
        check(vardefs == 2, "class body has 2 variable definitions");
        check(methods == 1, "class body has 1 method injection");

        // A vardef with init should carry an init child node.
        // 带初始化的 vardef 应有 init 子节点
        auto vd = first_kind(body->kids[1], "vardef");
        check(vd->kids.size() == 2 && vd->kids[1]->kind == "string",
              "vardef with init carries an init child");
    }

    // ----------------------------------------------------------
    section("Contract body: signatures only (no function body)");
    {
        auto root = parse(
            "#Addable {\n"
            "  @add << [(std::Number a) -> (std::Number)];\n"
            "  @!zero << [()->(std::Number)];\n"
            "}");
        auto c = first_kind(root, "contractdef");
        auto body = c->kids.back();              // block (kids[0] when no parent)
        check(body->kids.size() == 2, "contract body has 2 signature items");
        auto si = body->kids[0];                  // signitem
        check(si->kind == "signitem", "item is a signitem");
        check(si->kids.size() == 1 && si->kids[0]->kind == "sign",
              "signitem holds only a sign, no body (contract sign has no {})");
        check(si->kids[0]->value == "->", "signature mode -> captured");
        check(body->kids[1]->isConst, "contract sign @! modifier -> isConst");

        // Contract signature vs class method: a class method is a behavior
        // (with a block body).
        // 约束签名 vs 类方法：类方法是 behavior（含 block 体）
        auto root2 = parse("$A { @m << [()->() { }]; }");
        auto mdef = first_kind(root2, "methoddef");
        check(mdef->kids[0]->kind == "behavior"
              && mdef->kids[0]->kids.size() == 2,
              "class method injection is a behavior (sign + block body)");
    }

    // ----------------------------------------------------------
    section("Modifier stripping: @ / @! / @# / @!#");
    {
        auto root = parse("$A { @m << [()->() { }]; }");
        auto m = first_kind(root, "methoddef");
        check(!m->isConst && !m->isPrivate && m->value == "m",
              "@m -> non-const, non-private, name m");

        root = parse("$A { @!m << [()->() { }]; }");
        m = first_kind(root, "methoddef");
        check(m->isConst && !m->isPrivate && m->value == "m",
              "@!m -> const, name m (! stripped)");

        root = parse("$A { @#m << [()->() { }]; }");
        m = first_kind(root, "methoddef");
        check(!m->isConst && m->isPrivate && m->value == "m",
              "@#m -> private, name m (# stripped)");

        root = parse("$A { @!#m << [()->() { }]; }");
        m = first_kind(root, "methoddef");
        check(m->isConst && m->isPrivate && m->value == "m",
              "@!#m -> const and private, name m");

        // @!# in a contract is stripped the same way.
        // 约束中 @!# 同样剥离
        root = parse("#C { @!#m << [()->()]; }");
        auto si = first_kind(root, "signitem");
        check(si->isConst && si->isPrivate && si->value == "m",
              "contract sign @!#m -> const and private, name m (!,# stripped)");
    }

    // ----------------------------------------------------------
    section("Flow direction normalized: >> to <<");
    {
        // a << b : receiver a, sender b
        auto s = first_stmt("a << b;");
        check(s->kind == "flow" && s->value == "<<", "a << b -> flow node, dir <<");
        check(s->kids[0]->value == "a" && s->kids[1]->value == "b",
              "a << b: receiver=a, sender=b");

        // a >> b : normalized to b << a (receiver b, sender a)
        s = first_stmt("a >> b;");
        check(s->kind == "flow" && s->value == "<<", "a >> b -> normalized to flow <<");
        check(s->kids[0]->value == "b" && s->kids[1]->value == "a",
              "a >> b: receiver=b, sender=a (direction flipped)");

        // Left-associative: a << b << c == (a<<b) << c
        s = first_stmt("a << b << c;");
        check(s->kind == "flow", "a << b << c is a flow overall");
        check(s->kids[1]->value == "c", "outermost sender is c");
        check(s->kids[0]->kind == "flow"
              && s->kids[0]->kids[0]->value == "a"
              && s->kids[0]->kids[1]->value == "b",
              "inner is (a << b), left-associative");
    }

    // ----------------------------------------------------------
    section("Tuple / placeholder / single-element grouping");
    {
        // (a, b, c) -> tuple with 3 elements
        auto s = first_stmt("x << (a, b, c);");
        auto t = first_kind(s, "tuple");
        check(t != nullptr && t->kids.size() == 3, "(a,b,c) -> tuple with 3 elements");

        // Single-element parens are grouping, passed through as a name.
        // 单元素括号是分组，透传为 name（无 tuple）
        s = first_stmt("x << (a);");
        check(first_kind(s, "tuple") == nullptr, "(a) does not make a tuple node");
        // The sender should be the name a.
        auto flow = first_kind(s, "flow");
        check(flow->kids[1]->kind == "name" && flow->kids[1]->value == "a",
              "(a) grouping passes through as name a");

        // Placeholder _ and multi-declaration destructuring (in a closure).
        // 占位符 _ 与多声明解耦（置于闭包内）
        auto vd = first_stmt("-(std::Number a, _) << src;");
        int ndecl = 0;
        for (auto& k : vd->kids) if (k->kind == "decl") ndecl++;
        check(ndecl == 2, "multi-decl vardef has 2 decls");
        check(vd->kids[0]->name == "a" && !vd->kids[0]->isPlaceholder, "decl[0]=a, not placeholder");
        check(vd->kids[1]->isPlaceholder && vd->kids[1]->name == "_", "decl[1]=_ placeholder");

        // Trailing ! on a type name marks a constant (in a closure).
        // 类型名尾 ! 标记常数（置于闭包内）
        vd = first_stmt("-(std::Number! c);");
        check(vd->kids[0]->value == "std::Number" && vd->kids[0]->isConst,
              "type name std::Number! -> const marker");
    }

    // ----------------------------------------------------------
    section("Behavior modes: -> / ~> / =>");
    {
        auto root = parse("$A { @m << [()->() { }]; }");
        auto sign = first_kind(root, "sign");
        check(sign->value == "->", "-> mode captured");

        root = parse("#C { @m << [(std::Number a) ~> (std::Number)]; }");
        sign = first_kind(root, "sign");
        check(sign->value == "~>", "~> mode captured");

        root = parse("#C { @m << [(a) => (b)]; }");
        sign = first_kind(root, "sign");
        check(sign->value == "=>", "=> mode captured");

        // Illegal mode must be rejected.
        check_rejected("$A { @m << [(a) <> (b) { }]; }", "illegal behavior mode <> rejected");
    }

    // ----------------------------------------------------------
    section("'=' method / reserved-name calls (lexical level)");
    {
        // a.=(b) -> call name "="
        auto s = first_stmt("a.=(b);");
        check(s->kind == "call" && s->value == "=", "a.=(b) -> call name '='");
        check(s->kids[1]->kids.size() == 1, "=(b) has 1 argument");

        // a.=:() -> call name "=:"
        s = first_stmt("a.=:();");
        check(s->kind == "call" && s->value == "=:", "a.=:() -> call name '=:'");

        // a.:=() -> call name ":="
        s = first_stmt("a.:=();");
        check(s->kind == "call" && s->value == ":=", "a.:=() -> call name ':='");

        // a.::() -> call name "::" (constructor reserved name)
        s = first_stmt("a.::();");
        check(s->kind == "call" && s->value == "::", "a.::() -> call name '::'");

        // a.-(b) -> call name "-" (method name with operator chars)
        s = first_stmt("a.-(b);");
        check(s->kind == "call" && s->value == "-", "a.-(b) -> call name '-'");
    }

    // ----------------------------------------------------------
    section("Instantiation expression chain (D.14): -(T x).m()");
    {
        auto s = first_stmt("-(std::Number x).print();");
        check(s->kind == "call" && s->value == "print", "-(T x).print() -> call print");
        check(s->kids[0]->kind == "inst", "call receiver is an inst node");
        check(s->kids[0]->value == "std::Number" && s->kids[0]->name == "x",
              "inst: type std::Number, variable x");
    }

    // ----------------------------------------------------------
    section("Spec example rewritten (inside a closure)");
    {
        // 11.3 style: a class with flow-using methods.
        // 11.3 风格：类 + 带流的方法
        auto root = parse(
            "$Counter {\n"
            "  -(std::Number value);\n"
            "  @inc << [(std::Number n) -> () {\n"
            "    value << value.+(n);\n"
            "  }];\n"
            "  @show << [()->() {\n"
            "    -(std::OStream o);\n"
            "    o << value;\n"
            "  }];\n"
            "}");
        auto cls = first_kind(root, "classdef");
        auto body = cls->kids.back();
        int v = 0, m = 0;
        for (auto& k : body->kids) {
            if (k->kind == "vardef") v++;
            if (k->kind == "methoddef") m++;
        }
        check(v == 1 && m == 2, "Counter: 1 variable + 2 methods");
        check(count_kind(root, "flow") == 2, "each of the two methods has 1 flow");

        // Subclass with a parent: the parent name lives in classdef's kids[0].
        root = parse("$Sub [Base] { -(std::Number x); }");
        auto c = first_kind(root, "classdef");
        check(c->kids.size() >= 1 && c->kids[0]->kind == "name"
              && c->kids[0]->value == "Base", "parent Base recorded in kids[0]");
    }

    // ----------------------------------------------------------
    section("AST dump utility (extracted from parser.hpp)");
    {
        // The dump helper lives in ast_dump.hpp; assert it renders the tree
        // and exposes the node kinds we expect.
        // dump 辅助位于 ast_dump.hpp；断言它能渲染树并暴露期望的节点种类。
        auto root = parse("$A { @m << [()->() { }]; }");
        std::string out = parser::dump(root);
        check(out.find("classdef") != std::string::npos, "dump shows 'classdef'");
        check(out.find("methoddef") != std::string::npos, "dump shows 'methoddef'");
        check(out.find("behavior") != std::string::npos, "dump shows 'behavior'");
    }

    // ----------------------------------------------------------
    section("Error paths (child process: must be rejected)");
    {
        check_rejected("-(std::Number x);", "top-level variable definition rejected");
        check_rejected("$A { foo; }", "illegal class-body statement (bare expr) rejected");
        check_rejected("#C { @m << [()->()] { }; }", "contract sign with {} rejected");
        check_rejected("$A { -(std::Number x) }", "missing semicolon rejected");
        check_rejected("$A { @m << [()->() { }] }", "method injection missing ';' rejected");
        check_rejected("&io", "import missing ';' rejected");
        check_rejected("@m << [()->() { }];", "top-level method injection rejected");

        // A rejected program must also FAIL the process, not merely print a
        // diagnostic: `synth bad.syn && cmd` must never run cmd, and CI has to
        // be able to see the failure. This guards the fatal-error path against
        // regressing back to exit status 0.
        // 被拒绝的程序还必须让**进程失败**，而非仅打印诊断：
        // `synth bad.syn && cmd` 绝不能执行 cmd，CI 也必须能看出失败。
        // 本断言守护致命错误路径，防止其退出码退化为 0。
        CliResult bad = run_cli("$A { -(std::Number x) }", "_neg_tmp.sy");
        check(bad.code != 0,
              "rejected program exits non-zero (got " + std::to_string(bad.code)
              + ") / 被拒绝的程序以非 0 退出码结束");
    }

    // ----------------------------------------------------------
    std::cout << "\n============================================" << std::endl;
    std::cout << "Parser tests: PASSED " << passed << " / FAILED " << failed
              << std::endl;
    std::cout << "============================================" << std::endl;
    std::remove("_cli_out.txt");
    std::remove("_neg_tmp.sy");
    return (failed == 0) ? 0 : 1;
}
