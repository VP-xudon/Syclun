// ============================================================
// assert_lexer.cpp
//
// Synth OOP lexical analyzer — acceptance tests.
// Synth OOP 词法分析器 · 验收测试
//
// Acceptance target: all lexical rules implemented by lexer.hpp per
// the "Synth OOP Language Specification v1.26" (operator characters
// inside names, literals, structural symbols, comments, EOF).
// 验收对象：lexer.hpp 按《Synth OOP 语言文档 v1.26》实现的
// 全部词法规则（名称含运算符字符、字面量、结构符号、注释、EOF）。
// Note: v1.26 relative to v1.25 only refactors terminology and
// sections ("contract" renamed to "behavior mode", etc.); there is
// no glyph change, so the lexical layer is unaffected by version.
// 注：v1.26 相对 v1.25 仅术语与章节重构（"契约"改称"行为模式"等），
// 无任何字形变化，词法层不受版本影响。
//
// Test-case sources: document appendix D (D.1-D.18) and the chapters
// (2.3.2 / 3 / 4 / 5.4 / 5.5 / 6.2 / 6.3 / 7 / 8 / 9 / 10 / 11.2).
// 用例来源：文档附录 D（D.1-D.18）与正文章节
// （2.3.2 / 3 / 4 / 5.4 / 5.5 / 6.2 / 6.3 / 7 / 8 / 9 / 10 / 11.2）。
//
// Run: ./assert_lexer
// 运行：./assert_lexer
// ============================================================

#include <iostream>
#include <string>
#include <vector>
#include <utility>

#include "../../src/lexer.hpp"   // interpreter engine headers live in ../../src

using lexer::Token;
using lexer::Lexer;

namespace {

    // --------------------------------------------------------
    // Test skeleton: no classes, no macros — only free predicates and
    // counters.
    // 测试骨架：无类、无宏，只有自由的判定与统计。
    // --------------------------------------------------------

    int passed = 0;
    int failed = 0;

    void check(bool condition, const std::string &what) {
        if (condition) {
            ++passed;
            std::cout << "  [PASS] " << what << "\n";
        } else {
            ++failed;
            std::cout << "  [FAIL] " << what << "\n";
        }
    }

    void section(const std::string &title) {
        std::cout << "\n== " << title << " ==\n";
    }

    // --------------------------------------------------------
    // Convenience builders for expected sequences:
    // N=name S=symbol M=number T=string E=eof
    // 期望序列的便捷构造：N=name S=symbol M=number T=string E=eof
    // --------------------------------------------------------

    using Expect = std::pair<std::string, std::string>;

    Expect N(const char *v) { return {"<name>",   v}; }
    Expect S(const char *v) { return {"<symbol>", v}; }
    Expect M(const char *v) { return {"<number>", v}; }
    Expect T(const char *v) { return {"<string>", v}; }
    Expect E()              { return {"<eof>",    ""}; }

    std::vector<Token> lex_all(const std::string &src) {
        Lexer lx(src);
        std::vector<Token> out;
        while (true) {
            Token t = lx.lex();
            out.push_back(t);
            if (t.typetag == "<eof>") break;
        }
        return out;
    }

    void expect(const std::string &what, const std::string &src,
                std::initializer_list<Expect> want) {
        std::vector<Token> got = lex_all(src);
        bool ok = (got.size() == want.size());
        if (ok) {
            size_t i = 0;
            for (const auto &w : want) {
                if (got[i].typetag != w.first || got[i].value != w.second) {
                    ok = false;
                    break;
                }
                ++i;
            }
        }
        check(ok, what);
        if (!ok) {
            std::cout << "      source: " << src << "\n";
            std::cout << "      want :";
            for (const auto &w : want) std::cout << " " << w.first << "(" << w.second << ")";
            std::cout << "\n      got  :";
            for (const auto &t : got) std::cout << " " << t.typetag << "(" << t.value << ")";
            std::cout << "\n";
        }
    }
}

int main() {

    // --------------------------------------------------------
    // 1. Document D.1: zero-value init (complete program)
    // 1. 文档 D.1：零值初始化（完整程序）
    // --------------------------------------------------------
    section("1. Document D.1: zero-value init (complete program)");
    expect("D.1 full program lexing",
        "&io;\n"
        "$Program {\n"
        "    @:: << [() -> (void) {\n"
        "        -(std::Number num);\n"
        "        -(io::OStream out);\n"
        "        out << num;\n"
        "    }];\n"
        "}\n",
        {S("&"), N("io"), S(";"),
         S("$"), N("Program"), S("{"),
         S("@"), N("::"), S("<<"), S("["),
         S("("), S(")"), N("->"), S("("), N("void"), S(")"), S("{"),
         N("-"), S("("), N("std::Number"), N("num"), S(")"), S(";"),
         N("-"), S("("), N("io::OStream"), N("out"), S(")"), S(";"),
         N("out"), S("<<"), N("num"), S(";"),
         S("}"), S("]"), S(";"),
         S("}"), E()});

    // --------------------------------------------------------
    // 2. Arithmetic and comparison method names (Section 5.5)
    // 2. 算术与比较方法名（5.5 节）
    // --------------------------------------------------------
    section("2. Arithmetic and comparison method names "
        "(Section 5.5: + - * / % < > <= >= == !=)");
    expect("a.+(b)",  "a.+(b)",  {N("a"), S("."), N("+"),  S("("), N("b"), S(")"), E()});
    expect("a.-(b)",  "a.-(b)",  {N("a"), S("."), N("-"),  S("("), N("b"), S(")"), E()});
    expect("a.*(b)",  "a.*(b)",  {N("a"), S("."), N("*"),  S("("), N("b"), S(")"), E()});
    expect("a./(b)",  "a./(b)",  {N("a"), S("."), N("/"),  S("("), N("b"), S(")"), E()});
    expect("a.%(b)",  "a.%(b)",  {N("a"), S("."), N("%"),  S("("), N("b"), S(")"), E()});
    expect("a.<(b)",  "a.<(b)",  {N("a"), S("."), N("<"),  S("("), N("b"), S(")"), E()});
    expect("a.>(b)",  "a.>(b)",  {N("a"), S("."), N(">"),  S("("), N("b"), S(")"), E()});
    expect("a.<=(b)", "a.<=(b)", {N("a"), S("."), N("<="), S("("), N("b"), S(")"), E()});
    expect("a.>=(b)", "a.>=(b)", {N("a"), S("."), N(">="), S("("), N("b"), S(")"), E()});
    expect("a.==(b)", "a.==(b)", {N("a"), S("."), N("=="), S("("), N("b"), S(")"), E()});
    expect("a.!=(b)", "a.!=(b)", {N("a"), S("."), N("!="), S("("), N("b"), S(")"), E()});
    expect("chained a.+(b).*(3)", "a.+(b).*(3)",
        {N("a"), S("."), N("+"), S("("), N("b"), S(")"),
         S("."), N("*"), S("("), M("3"), S(")"), E()});

    // --------------------------------------------------------
    // 3. Reserved method names (Section 6.3) and @! modifier
    // 3. 保留方法名（6.3 节）与 @! 修饰
    // --------------------------------------------------------
    section("3. Reserved method names (Section 6.3: :: ~ =: :=) and @! modifier");
    expect("@:: constructor (D.13)", "@:: << [() ~> (void) {}];",
        {S("@"), N("::"), S("<<"), S("["), S("("), S(")"), N("~>"),
         S("("), N("void"), S(")"), S("{"), S("}"), S("]"), S(";"), E()});
    expect("@~ destructor (D.13)", "@~ << [() -> (void) {}];",
        {S("@"), N("~"), S("<<"), S("["), S("("), S(")"), N("->"),
         S("("), N("void"), S(")"), S("{"), S("}"), S("]"), S(";"), E()});
    expect("@=: publish function (D.13)", "@=: << [() ~> (result) {}];",
        {S("@"), N("=:"), S("<<"), S("["), S("("), S(")"), N("~>"),
         S("("), N("result"), S(")"), S("{"), S("}"), S("]"), S(";"), E()});
    expect("@:= receive function (D.13)", "@:= << [(msg) -> (void) {}];",
        {S("@"), N(":="), S("<<"), S("["), S("("), N("msg"), S(")"), N("->"),
         S("("), N("void"), S(")"), S("{"), S("}"), S("]"), S(";"), E()});
    expect("@!=: i.e. @! + =: (! merged into name, stripped by Parser)",
        "@!=: << [() ~> (void) {}];",
        {S("@"), N("!=:"), S("<<"), S("["), S("("), S(")"), N("~>"),
         S("("), N("void"), S(")"), S("{"), S("}"), S("]"), S(";"), E()});
    expect("@!:= i.e. @! + :=", "@!:= << [() ~> (void) {}];",
        {S("@"), N("!:="), S("<<"), S("["), S("("), S(")"), N("~>"),
         S("("), N("void"), S(")"), S("{"), S("}"), S("]"), S(";"), E()});

    // --------------------------------------------------------
    // 4. Operator method declarations (Section 2.3.2)
    // 4. 运算符方法声明（2.3.2 节）
    // --------------------------------------------------------
    section("4. Operator method declarations "
        "(Section 2.3.2: @+ @% @<= @!= ...)");
    expect("@+ declaration",  "@+ << [(other) => (result) {}];",
        {S("@"), N("+"), S("<<"), S("["), S("("), N("other"), S(")"), N("=>"),
         S("("), N("result"), S(")"), S("{"), S("}"), S("]"), S(";"), E()});
    expect("@% declaration",  "@% << [(other) => (result) {}];",
        {S("@"), N("%"), S("<<"), S("["), S("("), N("other"), S(")"), N("=>"),
         S("("), N("result"), S(")"), S("{"), S("}"), S("]"), S(";"), E()});
    expect("@<= declaration", "@<= << [(other) => (result) {}];",
        {S("@"), N("<="), S("<<"), S("["), S("("), N("other"), S(")"), N("=>"),
         S("("), N("result"), S(")"), S("{"), S("}"), S("]"), S(";"), E()});
    expect("@!= declaration", "@!= << [(other) => (result) {}];",
        {S("@"), N("!="), S("<<"), S("["), S("("), N("other"), S(")"), N("=>"),
         S("("), N("result"), S(")"), S("{"), S("}"), S("]"), S(";"), E()});

    // --------------------------------------------------------
    // 5. Number literals
    // 5. 数字字面量
    // --------------------------------------------------------
    section("5. Number literals (integer / decimal / negative / literal receiver)");
    expect("integer",   "10",   {M("10"), E()});
    expect("decimal",   "3.14", {M("3.14"), E()});
    expect("zero",      "0",    {M("0"), E()});
    expect("negative (Section 11.2.3: value << -1;)", "value << -1;",
        {N("value"), S("<<"), M("-1"), S(";"), E()});
    expect("literal call 3.repeat_ (D.16)", "3.repeat_(",
        {M("3"), S("."), N("repeat_"), S("("), E()});
    expect("10.+(5): non-digit after dot is a call symbol", "10.+(5)",
        {M("10"), S("."), N("+"), S("("), M("5"), S(")"), E()});
    expect("10.5 is a single number", "10.5", {M("10.5"), E()});
    expect("10.foo(): number followed by a method", "10.foo()",
        {M("10"), S("."), N("foo"), S("("), S(")"), E()});
    expect("10./(0)._case chain (Section 11.2.1)", "10./(0)._case(",
        {M("10"), S("."), N("/"), S("("), M("0"), S(")"),
         S("."), N("_case"), S("("), E()});
    expect("D.16 repeat_ full call", "3.repeat_([(state) -> (next) { next << state.+(1); }]);",
        {M("3"), S("."), N("repeat_"), S("("), S("["), S("("), N("state"), S(")"),
         N("->"), S("("), N("next"), S(")"), S("{"),
         N("next"), S("<<"), N("state"), S("."), N("+"), S("("), M("1"), S(")"), S(";"),
         S("}"), S("]"), S(")"), S(";"), E()});

    // --------------------------------------------------------
    // 6. String literals
    // 6. 字符串字面量
    // --------------------------------------------------------
    section("6. String literals");
    expect("plain string (D.2)",        "\"Yeah.\"",        {T("Yeah."), E()});
    expect("with spaces and punctuation (D.3)",     "\"Hello, World!\"", {T("Hello, World!"), E()});
    expect("empty string",                "\"\"",             {T(""), E()});
    expect("Chinese string (Section 11.2.3)", "\"捕获到异常：\".+(error);",
        {T("捕获到异常："), S("."), N("+"), S("("), N("error"), S(")"), S(";"), E()});
    expect("D.12 string concatenation chain", "name.+(\" (Research: \").+(topic).+(\")\");",
        {N("name"), S("."), N("+"), S("("), T(" (Research: "), S(")"),
         S("."), N("+"), S("("), N("topic"), S(")"),
         S("."), N("+"), S("("), T(")"), S(")"), S(";"), E()});

    // Escape sequences must be decoded into real bytes by the lexer
    // (regression for the user-reported missing \\n / \\a handling).
    // 转义序列须由词法器解码为真实字节（用户报告的 \n/\a 缺失回归）。
    expect("escape sequences (\\n \\t \\\\)", R"("a\nb\tc\\d")",
        {T("a\nb\tc\\d"), E()});
    expect("quote escape inside string (\\\")", R"("He said \"hi\"")",
        {T("He said \"hi\""), E()});
    expect("unknown escape kept verbatim (\\q)", R"("\q")",
        {T("\\q"), E()});

    // --------------------------------------------------------
    // 7. Line comments
    // 7. 行注释
    // --------------------------------------------------------
    section("7. Line comments (//)");
    expect("whole-line comment", "// nothing here", {E()});
    expect("trailing comment", "-(std::Number a); // trailing",
        {N("-"), S("("), N("std::Number"), N("a"), S(")"), S(";"), E()});
    expect("comment then newline continues", "// line one\n10", {M("10"), E()});
    expect("a./(b) not mistaken for a comment", "a./(b)",
        {N("a"), S("."), N("/"), S("("), N("b"), S(")"), E()});

    // --------------------------------------------------------
    // 8. Flow symbols << and >> (Chapter 3 / Appendix A)
    // 8. 流符号 << 与 >>（第三章 / 附录 A）
    // --------------------------------------------------------
    section("8. Flow symbols << and >> (Chapter 3 / Appendix A)");
    expect("leftward flow", "out << s;", {N("out"), S("<<"), N("s"), S(";"), E()});
    expect("rightward flow", "in >> txt;", {N("in"), S(">>"), N("txt"), S(";"), E()});
    expect("no-space x<<y is still three tokens", "x<<y",   {N("x"), S("<<"), N("y"), E()});
    expect("no-space in>>txt",             "in>>txt", {N("in"), S(">>"), N("txt"), E()});
    expect("<= unaffected by the << rule", "a.<=(b)",
        {N("a"), S("."), N("<="), S("("), N("b"), S(")"), E()});

    // --------------------------------------------------------
    // 9. Same-form split of prefix - (Chapter 4: instantiation vs method
    //    name)
    // 9. 前缀 - 的同形切分（第四章：实例化 vs 方法名）
    // --------------------------------------------------------
    section("9. Same-form split of prefix - "
        "(instantiation vs method name, distinguished by Parser)");
    expect("instantiation -(std::Number a)", "-(std::Number a)",
        {N("-"), S("("), N("std::Number"), N("a"), S(")"), E()});
    expect("method name a.-(b) (same form at the lexical layer)", "a.-(b)",
        {N("a"), S("."), N("-"), S("("), N("b"), S(")"), E()});
    expect("mode arrow -> is also a name", "->", {N("->"), E()});
    expect("~> and =>", "~> =>", {N("~>"), N("=>"), E()});

    // --------------------------------------------------------
    // 10. Instantiation expression (Chapter 4)
    // 10. 实例化表达式（第四章）
    // --------------------------------------------------------
    section("10. Instantiation expression (Chapter 4)");
    expect("declare and assign (Section 4.2)", "-(std::String strlock) << \"Yahoo!\";",
        {N("-"), S("("), N("std::String"), N("strlock"), S(")"),
         S("<<"), T("Yahoo!"), S(";"), E()});
    expect("D.3 nested instantiation expression", "out << (-(std::String msg) << \"Hello, World!\");",
        {N("out"), S("<<"), S("("), N("-"), S("("), N("std::String"), N("msg"), S(")"),
         S("<<"), T("Hello, World!"), S(")"), S(";"), E()});
    expect("D.14 chained call", "(-(Calculator c2)).add(10).get();",
        {S("("), N("-"), S("("), N("Calculator"), N("c2"), S(")"), S(")"), S("."),
         N("add"), S("("), M("10"), S(")"), S("."), N("get"), S("("), S(")"), S(";"), E()});

    // --------------------------------------------------------
    // 11. Const marker ! (Section 4.5)
    // 11. 常数标记 !（4.5 节）
    // --------------------------------------------------------
    section("11. Const marker ! (Section 4.5: type-name suffix merged into name)");
    expect("-(std::Number! frozen) << 7; (D.6)", "-(std::Number! frozen) << 7;",
        {N("-"), S("("), N("std::Number!"), N("frozen"), S(")"),
         S("<<"), M("7"), S(";"), E()});
    expect("const stream declaration (Section 3.4.3)", "-(io::OStream! out);",
        {N("-"), S("("), N("io::OStream!"), N("out"), S(")"), S(";"), E()});

    // --------------------------------------------------------
    // 12. Method modifiers (Section 6.2)
    // 12. 方法修饰符（6.2 节）
    // --------------------------------------------------------
    section("12. Method modifiers (Section 6.2: @! @# @!#)");
    expect("@!get: ! merged into name",   "@!get << b;",
        {S("@"), N("!get"), S("<<"), N("b"), S(";"), E()});
    expect("@#get: # is a symbol",     "@#get << b;",
        {S("@"), S("#"), N("get"), S("<<"), N("b"), S(";"), E()});
    expect("@!#get: ! as its own token then #", "@!#get << b;",
        {S("@"), N("!"), S("#"), N("get"), S("<<"), N("b"), S(";"), E()});
    expect("@!_case (Section 2.3.1)", "@!_case << b;",
        {S("@"), N("!_case"), S("<<"), N("b"), S(";"), E()});

    // --------------------------------------------------------
    // 13. Namespace-qualified names (Chapter 10)
    // 13. 命名空间限定名（第十章）
    // --------------------------------------------------------
    section("13. Namespace-qualified names (Chapter 10)");
    expect("std::Number is a single name",    "std::Number",    {N("std::Number"), E()});
    expect("io::OStream is a single name",    "io::OStream",    {N("io::OStream"), E()});
    expect("import &io; (Section 10.1)",      "&io;",           {S("&"), N("io"), S(";"), E()});
    expect("qualified reference after import (Section 10.2)", "shapes::Vector", {N("shapes::Vector"), E()});

    // --------------------------------------------------------
    // 14. Tuple literal and placeholder _ (Section 5.4 / D.15 / D.18)
    // 14. 元组字面量与占位符 _（5.4 节 / D.15 / D.18）
    // --------------------------------------------------------
    section("14. Tuple literal and placeholder _ (Section 5.4 / D.15 / D.18)");
    expect("tuple literal (D.18.1)", "(10, \"Alice\", true)",
        {S("("), M("10"), S(","), T("Alice"), S(","), N("true"), S(")"), E()});
    expect("destructure placeholder _ (D.15)", "-(std::Number q, _) << m.divide(10, 3);",
        {N("-"), S("("), N("std::Number"), N("q"), S(","), N("_"), S(")"),
         S("<<"), N("m"), S("."), N("divide"), S("("), M("10"), S(","), M("3"), S(")"),
         S(";"), E()});

    // --------------------------------------------------------
    // 15. Parameter constraints and behavior qualifier @ (Sections 9.4 /
    //     9.5 / 9.7)
    // 15. 参数约束与行为限定符 @（9.4 / 9.5 / 9.7 节）
    // --------------------------------------------------------
    section("15. Parameter constraints and behavior qualifier @ "
        "(Sections 9.4 / 9.5 / 9.7)");
    expect("a[Addable] (Section 9.4)", "[(a[Addable]) -> (result) {}]",
        {S("["), S("("), N("a"), S("["), N("Addable"), S("]"), S(")"),
         N("->"), S("("), N("result"), S(")"), S("{"), S("}"), S("]"), E()});
    expect("s[std::String] class name as constraint (Section 9.5)", "[(s[std::String]) -> (void) {}]",
        {S("["), S("("), N("s"), S("["), N("std::String"), S("]"), S(")"),
         N("->"), S("("), N("void"), S(")"), S("{"), S("}"), S("]"), E()});
    expect("handler[@] (Section 9.7)", "[(handler[@]) -> (void) {}]",
        {S("["), S("("), N("handler"), S("["), S("@"), S("]"), S(")"),
         N("->"), S("("), N("void"), S(")"), S("{"), S("}"), S("]"), E()});
    expect("handler[@(x) -> (y)] with signature qualifier (Section 9.7)",
        "[(handler[@(x) -> (y)]) -> (void) {}]",
        {S("["), S("("), N("handler"), S("["), S("@"), S("("), N("x"), S(")"),
         N("->"), S("("), N("y"), S(")"), S("]"), S(")"),
         N("->"), S("("), N("void"), S(")"), S("{"), S("}"), S("]"), E()});

    // --------------------------------------------------------
    // 16. Contracts and class declarations (Chapter 8 / 9.3 / 9.6)
    // 16. 约束与类声明（第八章 / 9.3 / 9.6 节）
    // --------------------------------------------------------
    section("16. Contracts and class declarations (Chapter 8 / 9.3 / 9.6)");
    expect("contract #Printable (Section 9.3)", "#Printable { @print << [() -> (void) {}]; }",
        {S("#"), N("Printable"), S("{"), S("@"), N("print"), S("<<"), S("["), S("("), S(")"),
         N("->"), S("("), N("void"), S(")"), S("{"), S("}"), S("]"), S(";"), S("}"), E()});
    expect("contract inheritance #Comparable [Addable] (Section 9.6)",
        "#Comparable [Addable] { @< << [(other) => (result) {}]; }",
        {S("#"), N("Comparable"), S("["), N("Addable"), S("]"), S("{"),
         S("@"), N("<"), S("<<"), S("["), S("("), N("other"), S(")"), N("=>"),
         S("("), N("result"), S(")"), S("{"), S("}"), S("]"), S(";"), S("}"), E()});
    expect("class with inheritance $Student [Human] (D.11)", "$Student [Human] { }",
        {S("$"), N("Student"), S("["), N("Human"), S("]"), S("{"), S("}"), E()});
    expect("derive from an instance $GraduateStudent [s] (D.12)", "$GraduateStudent [s] { }",
        {S("$"), N("GraduateStudent"), S("["), N("s"), S("]"), S("{"), S("}"), E()});

    // --------------------------------------------------------
    // 17. Boolean literals and control flow (Chapter 7)
    // 17. 布尔字面量与控制流（第七章）
    // --------------------------------------------------------
    section("17. Boolean literals and control flow (Chapter 7)");
    expect("true/false/void are all names", "(true).if_(void)",
        {S("("), N("true"), S(")"), S("."), N("if_"), S("("), N("void"), S(")"), E()});
    expect("D.16 if_ two branches", "(last.>(2)).if_([() ~> (value) { value << last; }], [() ~> (value) { value << 0; }]);",
        {S("("), N("last"), S("."), N(">"), S("("), M("2"), S(")"), S(")"), S("."),
         N("if_"), S("("), S("["), S("("), S(")"), N("~>"), S("("), N("value"), S(")"), S("{"),
         N("value"), S("<<"), N("last"), S(";"), S("}"), S("]"), S(","),
         S("["), S("("), S(")"), N("~>"), S("("), N("value"), S(")"), S("{"),
         N("value"), S("<<"), M("0"), S(";"), S("}"), S("]"), S(")"), S(";"), E()});
    expect("while_ two behavior arguments (Section 7.2)", "(true).while_(",
        {S("("), N("true"), S(")"), S("."), N("while_"), S("("), E()});

    // --------------------------------------------------------
    // 18. EOF semantics
    // 18. EOF 语义
    // --------------------------------------------------------
    section("18. EOF semantics");
    expect("empty input",   "",      {E()});
    expect("pure whitespace",   "  \t \r\n ", {E()});
    {
        Lexer lx("// only a comment");
        Token t1 = lx.lex();
        Token t2 = lx.lex();
        Token t3 = lx.lex();
        check(t1.typetag == "<eof>" && t2.typetag == "<eof>" && t3.typetag == "<eof>",
              "calling lex() repeatedly after EOF always returns <eof>");
    }

    // --------------------------------------------------------
    // Appendix: full token stream of document D.1 (manual check)
    // 附：文档 D.1 完整 token 流（人工核对）
    // --------------------------------------------------------
    section("Appendix: full token stream of document D.1 (manual check)");
    {
        const char *src =
            "&io;\n"
            "$Program {\n"
            "    @:: << [() -> (void) {\n"
            "        -(std::Number num);\n"
            "        -(io::OStream out);\n"
            "        out << num;\n"
            "    }];\n"
            "}\n";
        Lexer lx(src);
        int n = 0;
        while (true) {
            Token t = lx.lex();
            std::cout << "  " << n++ << "  " << t.typetag << "  " << t.value << "\n";
            if (t.typetag == "<eof>") break;
        }
    }

    // --------------------------------------------------------
    // Result
    // 结果
    // --------------------------------------------------------
    std::cout << "\n== Result ==\n";
    std::cout << "  passed: " << passed << "\n";
    std::cout << "  failed: " << failed << "\n";
    return failed == 0 ? 0 : 1;
}
