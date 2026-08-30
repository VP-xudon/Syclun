// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// parser.hpp
//
// Synth OOP syntax analyzer (per "Synth OOP Language Spec v1.26").
// Synth OOP 语法分析器（依据《Synth OOP 语言文档 v1.26》）。
//
// Design principle: the parser only does syntax, never semantics.
// 设计原则：Parser 只做句法，不做语义。
//   - It does not check types, method existence, or behavior-mode
//     legality — those are the compiler / interpreter's job (a
//     constraint *is* a CallableSign);
//     不查类型、不查方法存在性、不查行为模式合法性——这些是
//     解释器 / 编译器核查（约束即 CallableSign）的事；
//   - `true` / `false` are recognized only as literals (a syntactic
//     fact); `void` and the like are not keywords;
//     `true` / `false` 仅作字面量识别（句法层事实），`void` 等
//     一律不是关键字；
//   - `>>` is normalized to `<<` direction at the syntax level
//     (A >> B == B << A), so the AST keeps a single flow direction
//     for the interpreter to handle uniformly.
//     `>>` 在句法层归一化为 `<<` 方向（A >> B ≡ B << A），AST 中
//     只保留一种流方向，供解释器统一处理。
//
// ---------------- Syntax rules (set by the language designer) ----------------
// ---------------- 句法规则（语言设计者钦定）----------------
//
// 1. Top level (the program's direct scope) allows only three items:
//    顶层（程序直接作用域）：仅允许三类条目——
//        &module;             import statement / 导入语句
//        $Class [Parent] { }  class definition / 类定义
//        #Contract [Parent] { } contract definition / 约束定义
//    All other statements (e.g. variable definitions) are NOT allowed
//    at the top level; they must live inside a closure (behavior body).
//    变量定义等其余一切语句不允许在程序直接使用，必须存在于闭包
//    （行为体 {}）内。
//
// 2. A class body allows only two statements:
//    类定义体：仅允许两种语句——
//        -(Type var);              member variable (optionally initialized)
//                                 / 成员变量定义（可带初始化）
//        @mod? method << behavior;  method injection (itself a var def)
//                                 / 方法注入（本质也是变量定义）
//
// 3. A contract body allows only method-signature definitions, and the
//    signature carries NO function body:
//    约束体：仅允许方法签名定义，且签名后不写函数体——
//        @mod? method << [(params) mode (outputs)];
//    (The `{}` after the signature in spec sections 9.1/9.3 is a typo and
//    must be omitted.)
//    （文档 9.1/9.3 示例中的 {} 是笔误，应省略。）
//
// 4. Inside a closure (behavior body): variable definitions and
//    expression statements (including flows). Class definitions,
//    contract definitions, method injections, and imports are forbidden.
//    闭包（行为体）内：变量定义与表达式语句（含流语句），不允许类
//    定义、约束定义、方法注入、模块导入。
//
// 5. Every statement must end with a semicolon; class / contract
//    definitions themselves take no trailing semicolon (consistent
//    with every example in the spec).
//    所有语句必须以分号结尾；类定义 / 约束定义本身不带分号
//    （与文档全部示例一致）。
//
// ---------------- Lexer contract (see lexer.hpp) ----------------
// ---------------- 词法衔接（Lexer 约定）----------------
//
//   - A name contains operator characters: `+` `-` `*` `/` `%` `<` `>`
//     `=` `!` `:` `~` are all single <name> tokens; hence method names
//     like `+`, `<=`, `::`, `=:`, `:=`, `~`, and even `std::Number`
//     are name tokens;
//     名称含运算符字符：`+` `-` `*` `/` `%` `<` `>` `=` `!` `:` `~`
//     都是单个 <name>；故方法名 `+`、`<=`、`::`、`=:`、`:=`、`~` 与
//     `std::Number` 一样都是名称 token；
//   - `-(Type var)` and `a.-(b)` are lexically identical; the parser
//     distinguishes them by position: a `-` at the start of a statement
//     or expression is the instantiation prefix, one after `.` is a
//     method name;
//     `-(类型 变量)` 与 `a.-(b)` 词法同形，Parser 按位置区分：语句 /
//     表达式起点的 `-` 是实例化前缀，`.` 之后的是方法名；
//   - `!` is folded into names: `@!get` is symbol(@)+name("!get"),
//     `std::Number!` is a single name, `@!#m` is name("!")+symbol(#)
//     +name("m"); modifier stripping is in parse_method_head();
//     `!` 并入名称：`@!get` 是 symbol(@)+name("!get")，`std::Number!`
//     是单个 name，`@!#m` 是 name("!")+symbol(#)+name(m)，修饰符
//     剥离见 parse_method_head()；
//   - `<<` `>>` are <symbol> (flow direction), `->` `~>` `=>` are
//     <name> (behavior-mode arrows).
//     `<<` `>>` 是 <symbol>（流方向），`->` `~>` `=>` 是 <name>
//     （行为模式箭头）。
//
// ---------------- AST node overview ----------------
// ---------------- AST 节点一览 ----------------
//
//   program    kids = top-level items (import / classdef / contractdef)
//   program    子节点 = 顶层条目（import / classdef / contractdef）
//   import     value = module name / 模块名
//   classdef   value = class name; kids[0] = parent (name, optional);
//              kids.back() = block (class body: vardef / methoddef)
//   classdef   值=类名；kids[0]=父（name，可缺省）；
//              kids.back()=block（类体：vardef / methoddef）
//   contractdef value = contract name; kids[0] = parent (optional);
//              kids.back() = block (contract body: signitem)
//   contractdef 值=约束名；kids[0]=父（可缺省）；
//              kids.back()=block（约束体：signitem）
//   vardef     kids[0..n-1] = decl; kids[n] = init (optional)
//   vardef     子节点 = 声明 + 可选初始化
//   decl       value = type name; name = variable name;
//              isConst / isPlaceholder(_)
//   decl       值=类型名；name=变量名；isConst / isPlaceholder(_)
//   methoddef  value = method name; isConst(@!) / isPrivate(@#);
//              kids[0] = right-hand expression (usually a behavior)
//   methoddef  值=方法名；isConst(@!) / isPrivate(@#)；
//              kids[0]=右侧表达式（通常是 behavior）
//   signitem   value = method name; modifiers as above; kids[0] = sign
//   signitem   值=方法名；修饰符同上；kids[0]=sign
//   sign       value = behavior mode; kids[0] = params; kids[1] = outputs
//   sign       值=行为模式；kids[0]=params 容器；kids[1]=outputs 容器
//   param      name = parameter name; value = type / contract name;
//              mode = none | type | constraint | behavior | behaviorsign;
//              kids[0] = signature (only for behaviorsign)
//   behavior   value = mode; kids[0] = params; kids[1] = outputs;
//              kids[2] = block (behavior body)
//   block      kids = statements / 语句
//   flow       value = "<<" (normalized); kids[0] = receiver; kids[1] = sender
//   flow       值="<<"（已归一化）；kids[0]=接收方；kids[1]=发送方
//   inst       value = type name; name = variable name; isConst (expr pos)
//   call       value = method name; kids[0] = receiver; kids[1] = args
//   call       值=方法名；kids[0]=接收者；kids[1]=args 容器
//   access     value = member / method name; kids[0] = receiver (no parens)
//   name / number / string / bool   value = text / 文本
//   tuple      kids = elements (>=2; single-element parens pass through)
//   tuple      子节点=元素（≥2；单元素括号是分组，直接透传）
// ============================================================

#ifndef PARSER_HPP
#define PARSER_HPP

#include <memory>
#include <string>
#include <vector>

#include "lexer.hpp"

namespace parser {

    // One AST node. The parser produces a tree of these; the interpreter
    // (future work) walks it. Kept as a single class with optional flags
    // rather than a deep hierarchy, so the data model stays simple.
    // 一个 AST 节点。Parser 产出这些节点的树，供（未来的）解释器遍历。
    // 用单一类 + 可选标志而非深层继承，使数据模型保持简单。
    class AstNode {
        public:

        std::string kind;          // node kind (see overview above) / 节点种类
        std::string value;         // main payload: name / number / mode / op
                                   // 主载荷：名称 / 数字 / 模式 / 操作符
        std::string name;          // secondary payload: var / param name
                                   // 次载荷：变量名 / 参数名
        std::string mode;          // param qualifier form (param node only)
        // 参数限定形式（param 节点专用）
        std::string constraint;    // variable / parameter constraint name
        // 变量 / 参数约束名（如 `[Addable]` 中的 Addable）
        bool isConst = false;      // `!`: const variable (type suffix) /
                                   // method-variable const (@!)
        // `!`：常数变量（类型尾缀）/ 方法变量 const（@!）
        bool isPrivate = false;    // `#`: private method (@#) / `#`：private 方法（@#）
        bool isPlaceholder = false;// `_`: destructuring placeholder / `_`：解耦占位符
        long long line = 0;        // start line (error localization) / 起始行号（报错定位）
        long long col = 0;         // start column (error localization) / 起始列号（报错定位）
        std::vector<std::shared_ptr<AstNode>> kids;
    };

    using AstNodePtr = std::shared_ptr<AstNode>;

    class Parser {
        // Token stream (includes a trailing <eof>) and the source lines
        // for error context.
        // token 流（含结尾 <eof>）与源码行表（报错上下文）。
        std::vector<lexer::Token> toks;
        std::vector<std::string> selflines;
        std::size_t pos = 0;

        // ---- cursor / 游标 ----

        const lexer::Token& peek(std::size_t off = 0) const {
            std::size_t i = pos + off;
            if (i >= toks.size()) {
                i = toks.size() - 1;      // always point at <eof>
            }
            return toks[i];
        }
        lexer::Token advance() {
            auto t = toks[pos];
            if (pos + 1 < toks.size()) {
                ++pos;
            }
            return t;
        }
        bool at(const std::string& typetag, const std::string& value) const {
            const auto& t = peek();
            return t.typetag == typetag && t.value == value;
        }
        bool at_type(const std::string& typetag) const {
            return peek().typetag == typetag;
        }
        bool at_eof() const {
            return peek().typetag == "<eof>";
        }

        lexer::Token expect_symbol(const std::string& value, const std::string& what) {
            if (at("<symbol>", value)) {
                return advance();
            }
            fail("Expected " + what + " '" + value + "' here, but saw '"
                 + peek().value + "'.");
            return peek();                 // unreachable (fail exits)
        }
        lexer::Token expect_name(const std::string& what) {
            if (at_type("<name>")) {
                return advance();
            }
            fail("Expected " + what + " (a name) here, but saw '"
                 + peek().value + "'.");
            return peek();
        }

        // Node factory: kind + start line (and, by default, the current
        // token's column, so runtime errors localize to the real character
        // instead of always column 1). 节点工厂：种类 + 起始行（默认再用
        // 当前 token 的列号，使运行时错误定位到真实字符而非恒为第 1 列）。
        AstNodePtr mknode(const std::string& kind, long long line,
                          long long col = 0) {
            auto node = std::make_shared<AstNode>();
            node->kind = kind;
            node->line = line;
            node->col = (col != 0) ? col : peek().col;
            return node;
        }

        void fail(const std::string& message) {
            const auto& t = peek();
            std::string where = std::to_string(t.line);
            std::string ctx = (t.line >= 1
                               && t.line < (long long)selflines.size())
                ? selflines[t.line] : "<unknown line>";
            diag::set_locus(diag::source_file(), t.line, t.col);
            Thrower.throwE(
                "SyntaxError",
                message + " (near line " + where + ", '"
                + (t.typetag == "<eof>" ? std::string("<end-of-file>")
                                        : t.value)
                + "')",
                {where + " | " + ctx}
            );
        }

        // Strip a trailing `!` from a type name (const marker, spec 4.5):
        // `std::Number!` -> ("std::Number", true).
        // 类型名剥尾部 `!`（常数标记，文档 4.5）：
        // `std::Number!` -> (`std::Number`, true)。
        static std::pair<std::string, bool> split_const_typename(
            const std::string& text
        ) {
            if (text.size() > 1 && text.back() == '!') {
                return {text.substr(0, text.size() - 1), true};
            }
            return {text, false};
        }

        // ---- `@` modifiers and the method name (spec 6.2) ----
        // Lexical forms: `@m` | `@!m`(name"!m") | `@#m`(sym# name m)
        //         | `@!#m`(name"!" sym# name m) | `@#!m` and variants.
        // Because `!` is a name character and `#` is a symbol, we peel them
        // off one at a time.
        // 词法形：`@m` | `@!m`(name"!m") | `@#m`(sym# name m)
        //         | `@!#m`(name"!" sym# name m) | `@#!m` 等变体。
        // `!` 是名称字符、`#` 是符号，逐个剥落即可。
        std::string parse_method_head(bool& is_const, bool& is_private) {
            expect_symbol("@", "method declaration symbol '@'");
            is_const = false;
            is_private = false;
            while (true) {
                if (at_type("<name>") && !peek().value.empty()
                    && peek().value.front() == '!') {
                    auto tok = advance();
                    is_const = true;
                    std::string rest = tok.value.substr(1);
                    if (!rest.empty()) {
                        return rest;       // `!m` -> const + "m"
                    }
                    // A bare `!`: keep reading (a `#` or method name follows)
                    // 单独的 `!`：继续读（后面可能是 `#` 或方法名）
                } else if (at("<symbol>", "#")) {
                    advance();
                    is_private = true;
                } else {
                    return expect_name("method name").value;
                }
            }
        }

        // ========================================================
        // Top level / 顶层
        // ========================================================

        AstNodePtr parse_import() {
            long long ln = peek().line;
            expect_symbol("&", "module import symbol '&'");
            auto mod = expect_name("module name");
            expect_symbol(";", "statement terminator ';'");
            auto node = mknode("import", ln);
            node->value = mod.value;
            return node;
        }

        AstNodePtr parse_classdef() {
            long long ln = peek().line;
            expect_symbol("$", "class definition symbol '$'");
            auto cls = expect_name("class name");

            auto node = mknode("classdef", ln);
            node->value = cls.value;
            if (at("<symbol>", "[")) {     // optional parent: type or instance
                advance();
                auto parent = mknode("name", peek().line);
                parent->value = expect_name("parent (type or instance name)").value;
                node->kids.push_back(parent);
                expect_symbol("]", "parent closing bracket ']'");
            }

            auto body = mknode("block", peek().line);
            expect_symbol("{", "class body opening brace '{'");
            while (!at("<symbol>", "}")) {
                if (at_eof()) {
                    fail("Unterminated class definition (missing '}').");
                }
                if (at_type("<name>") && peek().value == "-") {
                    body->kids.push_back(parse_vardef_stmt());
                } else if (at("<symbol>", "@")) {
                    body->kids.push_back(parse_methoddef());
                } else {
                    fail("Inside a class body only member-variable definitions "
                         "(-(Type var);) and method injections "
                         "(@method << behavior;) are allowed.");
                }
            }
            expect_symbol("}", "class body closing brace '}'");
            node->kids.push_back(body);
            return node;
        }

        AstNodePtr parse_contractdef() {
            long long ln = peek().line;
            expect_symbol("#", "contract definition symbol '#'");
            auto cname = expect_name("contract name");

            auto node = mknode("contractdef", ln);
            node->value = cname.value;
            if (at("<symbol>", "[")) {     // optional parent contract (9.6)
                advance();
                auto parent = mknode("name", peek().line);
                parent->value = expect_name("parent contract name").value;
                node->kids.push_back(parent);
                expect_symbol("]", "parent contract closing bracket ']'");
            }

            auto body = mknode("block", peek().line);
            expect_symbol("{", "contract body opening brace '{'");
            while (!at("<symbol>", "}")) {
                if (at_eof()) {
                    fail("Unterminated contract definition (missing '}').");
                }
                if (at("<symbol>", "@")) {
                    body->kids.push_back(parse_signitem());
                } else {
                    fail("Inside a contract body only method-signature "
                         "definitions (@method << [(params) mode (outputs)];) "
                         "are allowed, and the signature carries no body.");
                }
            }
            expect_symbol("}", "contract body closing brace '}'");
            node->kids.push_back(body);
            return node;
        }

        // Contract signature item: @mod? method << [sign]; (no body).
        // 约束签名条目：@修饰符? 方法名 << [签名];（无函数体）。
        AstNodePtr parse_signitem() {
            long long ln = peek().line;
            bool is_const = false;
            bool is_private = false;
            auto mname = parse_method_head(is_const, is_private);

            auto node = mknode("signitem", ln);
            node->value = mname;
            node->isConst = is_const;
            node->isPrivate = is_private;

            expect_symbol("<<", "flow symbol binding the signature");
            expect_symbol("[", "behavior (signature) opening bracket '['");
            node->kids.push_back(parse_sign_core());
            expect_symbol("]", "behavior (signature) closing bracket ']'");
            if (at("<symbol>", "{")) {
                // Per the language designer: a contract signature carries no
                // function body (the spec examples' `{}` is a typo).
                // 语言设计者钦定：约束签名不带函数体（文档示例笔误）。
                fail("A method signature inside a contract carries no function "
                     "body: omit '{', writing "
                     "@method << [(params) mode (outputs)];.");
            }
            expect_symbol(";", "statement terminator ';'");
            return node;
        }

        // ========================================================
        // Statements / 语句
        // ========================================================

        // Variable-definition statement (shared by class body and closure).
        // 变量定义语句（类体 / 闭包体共用）：
        //   -(Type var);              zero-value init
        //   -(Type! var) << value;    declare and assign
        //   -(Type a, Type b) << val;  multi-decl (tuple destructuring, 5.4.3)
        //   -(Type a, _) << val;      `_` placeholder
        AstNodePtr parse_vardef_stmt() {
            long long ln = peek().line;
            expect_name("instantiation prefix '-'");   // `-` is a name token
            expect_symbol("(", "instantiation opening paren '('");

            auto node = mknode("vardef", ln);
            node->kids.push_back(parse_decl());
            while (at("<symbol>", ",")) {
                advance();
                node->kids.push_back(parse_decl());
            }
            expect_symbol(")", "instantiation closing paren ')'");

            if (at("<symbol>", "<<")) {
                advance();
                node->kids.push_back(parse_postfix());   // init
            }
            expect_symbol(";", "statement terminator ';'");
            return node;
        }

        // A single instantiation declaration: TypeName varName | `_` (placeholder).
        // 实例化声明项：类型名 变量名 | `_`（占位）。
        //
        // Supported forms (per the language designer's constraint feature):
        // 支持以下形式（依据语言设计者的约束特性）：
        //   Type var                    classic two-name form / 经典双名形式
        //   Type(args) var              with constructor args / 带构造实参
        //   Type[Constraint] var        constrained type / 带类型约束
        //   Type[Constraint](args) var  constraint + constructor args
        //   name                        Object shorthand -> (std::Object name)
        //                                简写：等价 -(std::Object name)
        AstNodePtr parse_decl() {
            long long ln = peek().line;
            if (at_type("<name>") && peek().value == "_") {
                advance();
                auto d = mknode("decl", ln);
                d->isPlaceholder = true;
                d->name = "_";
                return d;
            }
            auto first = expect_name("type name (prototype name) or variable name");
            // `void` is no longer a type / name keyword: use empty parentheses
            // `()` for "no value". Reject it so authors migrate to `()`.
            // `void` 不再是类型 / 名称关键字：用空括号 `()` 表示「无值」。
            if (first.value == "void") {
                fail("'void' is not a type or name; use empty parentheses '()' "
                     "for no value / no return.");
            }
            // Greedily consume an optional `[Constraint]` and an optional
            // constructor-argument list `(args)` that may follow the leading
            // name, then decide whether a *separate* variable name follows.
            // 在首个名称之后贪婪地吃掉可选的 `[约束]` 与可选的构造实参 `(实参)`，
            // 再据此判断其后是否还有另一个变量名。
            std::string constraint;
            AstNodePtr ctorArgs = nullptr;
            if (at("<symbol>", "[")) {                 // optional [Constraint]
                advance();
                constraint = expect_name("constraint or class name").value;
                if (constraint == "void") {
                    fail("'void' is not a constraint; use a contract / class "
                         "name, or empty parentheses '()' for no value.");
                }
                expect_symbol("]", "constraint closing bracket ']'");
            }
            if (at("<symbol>", "(")) {                 // optional (args)
                advance();
                ctorArgs = mknode("args", peek().line);
                if (!at("<symbol>", ")")) {
                    ctorArgs->kids.push_back(parse_postfix());
                    while (at("<symbol>", ",")) {
                        advance();
                        ctorArgs->kids.push_back(parse_postfix());
                    }
                }
                expect_symbol(")", "constructor argument list closing paren ')'");
            }
            std::string type_name;
            std::string var_name;
            bool is_const = false;
            if (at_type("<name>")) {
                // Two-name form: `first` is the type, the next name is the var.
                // 双名形式：first 是类型，下一个名称是变量名。
                auto [tn, c] = split_const_typename(first.value);
                if (tn == "_" || tn.empty()) {
                    fail("Instantiation must name a prototype: the parentheses "
                         "should be 'TypeName variableName', not just a variable "
                         "name.");
                }
                type_name = tn;
                is_const = c;
                // Optional standalone const marker '!' (e.g. Type(args)! name).
                // 可选独立常数标记 '!'（如 类型(实参)! 名）。
                if (at_type("<name>") && peek().value == "!") {
                    advance();
                    is_const = true;
                }
                auto var_tok = expect_name("variable name");
                if (var_tok.value == "void") {
                    fail("'void' is not a variable name; use empty parentheses "
                         "'()' for no value / no return.");
                }
                var_name = var_tok.value;
            } else {
                // Object shorthand `-(name)`: `first` is the variable, the type
                // is the universal `std::Object`. A shorthand may not carry a
                // constraint or constructor args (write `-(std::Object[Constraint]
                // name)` instead). 简写 `-(名)`：first 即变量，类型为通用
                // std::Object。简写不得带约束或构造实参（请改用
                // `-(std::Object[约束] 名)`）。
                if (!constraint.empty() || ctorArgs) {
                    fail("An Object-shorthand instantiation '-(name)' cannot "
                         "carry a constraint or constructor arguments; write "
                         "'-(std::Object name)' or '-(Type[Constraint] name)'.");
                }
                type_name = "std::Object";
                var_name = first.value;
            }
            auto d = mknode("decl", ln);
            d->value = type_name;
            d->name = var_name;
            d->isConst = is_const;
            d->constraint = constraint;
            if (ctorArgs) d->kids.push_back(ctorArgs);
            return d;
        }

        // Build a behavior node with an empty signature and empty body.
        // 构造一个签名与函数体均为空的行为节点（用于空方法声明）。
        AstNodePtr make_empty_behavior(long long ln) {
            auto beh = mknode("behavior", ln);
            auto sign = mknode("sign", ln);
            sign->value = "->";
            sign->kids.push_back(mknode("params", ln));   // empty input list
            sign->kids.push_back(mknode("params", ln));   // empty output list
            beh->kids.push_back(sign);
            beh->kids.push_back(mknode("block", ln));
            return beh;
        }

        // Method-injection statement (class body, and now also closures):
        //   @mod? method << behavior;     bind via flow (original form)
        //   @mod? method .= behavior;     bind via assignment (alias of <<)
        //   @mod? method;                 empty method (no-op body)
        // 方法注入语句（类体，现也支持闭包体）：
        //   @修饰符? 方法名 << 行为;       经流绑定（原形式）
        //   @修饰符? 方法名 .= 行为;       经赋值绑定（<< 的别名）
        //   @修饰符? 方法名;              空方法（无操作函数体）
        AstNodePtr parse_methoddef() {
            long long ln = peek().line;
            bool is_const = false;
            bool is_private = false;
            auto mname = parse_method_head(is_const, is_private);

            auto node = mknode("methoddef", ln);
            node->value = mname;
            node->isConst = is_const;
            node->isPrivate = is_private;

            if (at("<symbol>", "<<")) {
                advance();
                node->kids.push_back(parse_postfix());       // behavior
                expect_symbol(";", "statement terminator ';'");
                return node;
            }
            if (at("<symbol>", ".")) {
                // `.=` assignment binding: `@name .= (behavior)`. The `=` is a
                // name token (it is a name character), so after consuming the
                // `.` we read `=` and then the RHS expression.
                // `.=` 赋值绑定：`@名 .= (行为)`。消费 `.` 后读到 `=`（名称字符），
                // 再解析右侧表达式。
                advance();
                auto eq = expect_name("'=' in method assignment");
                if (eq.value != "=") {
                    fail("Expected '=' after '.' when binding method '"
                         + mname + "'.");
                }
                node->kids.push_back(parse_postfix());       // behavior (RHS)
                expect_symbol(";", "statement terminator ';'");
                return node;
            }
            if (at("<symbol>", ";")) {
                // Empty method: an explicit declaration with no body produces
                // a no-op behavior (so `@::;` is a valid empty constructor).
                // 空方法：仅声明、未赋函数体，产生一个无操作行为
                //（故 `@::;` 是合法的空构造函数）。
                advance();
                node->kids.push_back(make_empty_behavior(ln));
                return node;
            }
            fail("Expected flow '<<', assignment '.=', or ';' to bind method '"
                 + mname + "'.");
            return nullptr;                                  // unreachable
        }

        // Closure (behavior body) statement: a variable definition or an
        // expression statement (including flows).
        // 闭包（行为体）语句：变量定义或表达式语句（含流语句）。
        AstNodePtr parse_stmt() {
            if (at("<symbol>", "@")) {
                // A method/closure declaration is also valid inside a closure
                // body: it defines a local closure variable bound to a behavior
                // (so `@name << [behavior]; name();` works inside a behavior).
                // 方法 / 闭包声明在闭包体内亦合法：定义绑定行为的局部闭包变量
                //（故 `@名 << [行为]; 名();` 可在行为体内使用）。
                return parse_methoddef();
            }
            if (at("<symbol>", "$")) {
                fail("Class definitions are only allowed at the top level.");
            }
            if (at("<symbol>", "#")) {
                fail("Contract definitions are only allowed at the top level.");
            }
            if (at("<symbol>", "&")) {
                fail("Module imports are only allowed at the top level.");
            }
            if (at_type("<name>") && peek().value == "-") {
                return parse_dash_stmt();
            }
            auto expr = parse_postfix();
            expect_symbol(";", "statement terminator ';'");
            return expr;                   // expression statement: node is stmt
        }

        // A `-`-started closure statement: either a variable definition, or an
        // instantiation expression chain (e.g. (-(T x)).m(); , -(T x).m(); ,
        // spec 4.6 / D.14).
        // `-` 起始的闭包语句：变量定义，或实例化表达式链
        // （如 (-(T x)).m();、-(T x).m();，文档 4.6 / D.14）。
        AstNodePtr parse_dash_stmt() {
            long long ln = peek().line;
            expect_name("instantiation prefix '-'");
            expect_symbol("(", "instantiation opening paren '('");

            std::vector<AstNodePtr> decls;
            decls.push_back(parse_decl());
            while (at("<symbol>", ",")) {
                advance();
                decls.push_back(parse_decl());
            }
            expect_symbol(")", "instantiation closing paren ')'");

            // Instantiation expression chain: `-(T x).m(...)` — continue the
            // postfix (`.` call / `<<` flow) as an expression statement.
            // 实例化表达式链：`-(T x).m(...)` —— 从 inst 继续后缀
            // （`.` 调用 / `<<` 流），作为表达式语句。
            if (decls.size() == 1 && at("<symbol>", ".")) {
                auto inst = mknode("inst", ln);
                inst->value = decls[0]->value;
                inst->name = decls[0]->name;
                inst->isConst = decls[0]->isConst;
                if (!decls[0]->kids.empty()) {
                    inst->kids.push_back(decls[0]->kids[0]);   // ctor args
                }
                auto expr = parse_postfix_tail(inst);
                expect_symbol(";", "statement terminator ';'");
                return expr;
            }

            auto node = mknode("vardef", ln);
            for (auto& d : decls) {
                node->kids.push_back(d);
            }
            if (at("<symbol>", "<<")) {
                advance();
                node->kids.push_back(parse_postfix());   // init
            }
            expect_symbol(";", "statement terminator ';'");
            return node;
        }

        // ========================================================
        // Expressions / 表达式
        // ========================================================

        AstNodePtr parse_postfix() {
            return parse_postfix(true);
        }

        // primary + postfix chain: .name(args) | .name | <<sender | >>receiver.
        // The language has no infix operators; every expression is this chain
        // (spec 5.5). Flow is left-associative: a<<b<<c == (a<<b)<<c.
        // primary + 后缀链：.名(实参) | .名 | <<发送方 | >>接收方。
        // 语言无中缀运算符，全部表达式都是这条链（文档 5.5）。
        // 流为左结合：a << b << c ≡ (a << b) << c。
        AstNodePtr parse_postfix(bool allow_flow) {
            auto cur = parse_primary();
            return parse_postfix_tail(cur, allow_flow);
        }

        AstNodePtr parse_postfix_tail(AstNodePtr cur, bool allow_flow = true) {
            while (true) {
                if (at("<symbol>", ".")) {
                    advance();
                    auto mname = expect_name("method name");
                    if (at("<symbol>", "(")) {
                        advance();
                        auto args = mknode("args", mname.line);
                        if (!at("<symbol>", ")")) {
                            args->kids.push_back(parse_postfix());
                            while (at("<symbol>", ",")) {
                                advance();
                                args->kids.push_back(parse_postfix());
                            }
                        }
                        expect_symbol(")", "argument list closing paren ')'");
                        auto call = mknode("call", mname.line);
                        call->value = mname.value;
                        call->kids.push_back(cur);
                        call->kids.push_back(args);
                        cur = call;
                    } else {
                        // No-paren access: member attribute or method
                        // reference (s.foo, a method-variable reference);
                        // semantics are left to the interpreter.
                        // 无括号访问：成员属性或方法引用（8.3 的 s.name、
                        // 方法变量的引用），语义留给解释器。
                        auto access = mknode("access", mname.line);
                        access->value = mname.value;
                        access->kids.push_back(cur);
                        cur = access;
                    }
                } else if (allow_flow && at("<symbol>", "<<")) {
                    advance();
                    auto rhs = parse_postfix(false);     // sender (no nested flow)
                    auto flow = mknode("flow", rhs->line);
                    flow->value = "<<";
                    flow->kids.push_back(cur);            // receiver
                    flow->kids.push_back(rhs);            // sender
                    cur = flow;
                } else if (allow_flow && at("<symbol>", ">>")) {
                    advance();
                    auto rhs = parse_postfix(false);
                    auto flow = mknode("flow", rhs->line);
                    flow->value = "<<";                   // normalized direction
                    flow->kids.push_back(rhs);            // receiver
                    flow->kids.push_back(cur);            // sender
                    cur = flow;
                } else if (at("<symbol>", "(") && cur->kind == "name") {
                    // Bare call `name(args)`: a method of the current
                    // instance (self). Built as a `selfcall` node; the
                    // interpreter dispatches it to self's methods.
                    // 裸调用 `name(args)`：当前实例（self）的方法。
                    // 构造为 `selfcall` 节点，由解释器派发到 self 的方法。
                    advance();
                    auto args = mknode("args", cur->line);
                    if (!at("<symbol>", ")")) {
                        args->kids.push_back(parse_postfix());
                        while (at("<symbol>", ",")) {
                            advance();
                            args->kids.push_back(parse_postfix());
                        }
                    }
                    expect_symbol(")", "argument list closing paren ')'");
                    auto call = mknode("selfcall", cur->line);
                    call->value = cur->value;     // method name
                    call->kids.push_back(args);
                    cur = call;
                } else {
                    break;
                }
            }
            return cur;
        }

        AstNodePtr parse_primary() {
            const auto& t = peek();
            if (t.typetag == "<number>") {
                auto n = mknode("number", t.line);
                n->value = advance().value;
                return n;
            }
            if (t.typetag == "<string>") {
                auto n = mknode("string", t.line);
                n->value = advance().value;
                return n;
            }
            if (t.typetag == "<name>") {
                if (t.value == "true" || t.value == "false") {
                    auto n = mknode("bool", t.line);
                    n->value = advance().value;
                    return n;
                }
                if (t.value == "-") {
                    return parse_inst_expr();            // instantiation expr
                }
                auto n = mknode("name", t.line);
                n->value = advance().value;
                return n;
            }
            if (at("<symbol>", "(")) {
                // Parentheses: grouping (single element passes through) or a
                // tuple literal (>=2 elements).
                // 圆括号：分组（单元素透传）或元组字面量（≥2 元素）。
                advance();
                if (at("<symbol>", ")")) {
                    fail("Empty parentheses are not a valid expression.");
                }
                std::vector<AstNodePtr> elems;
                elems.push_back(parse_postfix());
                while (at("<symbol>", ",")) {
                    advance();
                    elems.push_back(parse_postfix());
                }
                expect_symbol(")", "closing paren ')'");
                if (elems.size() == 1) {
                    return elems[0];                      // grouping passthrough
                }
                auto n = mknode("tuple", elems[0]->line);
                n->kids = elems;
                return n;
            }
            if (at("<symbol>", "[")) {
                return parse_behavior();
            }
            fail("Expected an expression (number / string / name / parentheses "
                 "/ instantiation / behavior literal) here, but saw '"
                 + (at_eof() ? std::string("<end-of-file>") : t.value) + "'.");
            return nullptr;                               // unreachable
        }

        // Instantiation expression (expression position, single declaration):
        // -(Type var)
        // 实例化表达式（表达式位置，单声明）：-(类型 变量)
        AstNodePtr parse_inst_expr() {
            long long ln = peek().line;
            expect_name("instantiation prefix '-'");
            expect_symbol("(", "instantiation opening paren '('");
            auto first = expect_name("type name (prototype name) or variable name");
            if (first.value == "void") {
                fail("'void' is not a type or name; use empty parentheses '()' "
                     "for no value / no return.");
            }
            // Same optional [Constraint] / (args) greediness as parse_decl.
            // 与 parse_decl 相同的可选 [约束] / (实参) 贪婪处理。
            std::string constraint;
            AstNodePtr ctorArgs = nullptr;
            if (at("<symbol>", "[")) {
                advance();
                constraint = expect_name("constraint or class name").value;
                if (constraint == "void") {
                    fail("'void' is not a constraint; use a contract / class "
                         "name, or empty parentheses '()' for no value.");
                }
                expect_symbol("]", "constraint closing bracket ']'");
            }
            if (at("<symbol>", "(")) {
                advance();
                ctorArgs = mknode("args", peek().line);
                if (!at("<symbol>", ")")) {
                    ctorArgs->kids.push_back(parse_postfix());
                    while (at("<symbol>", ",")) {
                        advance();
                        ctorArgs->kids.push_back(parse_postfix());
                    }
                }
                expect_symbol(")", "constructor argument list closing paren ')'");
            }
            std::string type_name;
            std::string var_name;
            bool is_const = false;
            if (at_type("<name>")) {
                auto [tn, c] = split_const_typename(first.value);
                if (tn == "_" || tn.empty()) {
                    fail("Instantiation must name a prototype (TypeName variableName).");
                }
                type_name = tn;
                is_const = c;
                if (at_type("<name>") && peek().value == "!") {
                    advance();
                    is_const = true;
                }
                auto var_tok = expect_name("variable name");
                if (var_tok.value == "void") {
                    fail("'void' is not a variable name; use empty parentheses "
                         "'()' for no value / no return.");
                }
                var_name = var_tok.value;
            } else {
                if (!constraint.empty() || ctorArgs) {
                    fail("An Object-shorthand instantiation '-(name)' cannot "
                         "carry a constraint or constructor arguments; write "
                         "'-(std::Object name)' or '-(Type[Constraint] name)'.");
                }
                type_name = "std::Object";
                var_name = first.value;
            }
            if (at("<symbol>", ",")) {
                fail("An instantiation expression supports only a single "
                     "declaration; multi-declaration destructuring "
                     "(-(Type a, Type b)) can only be used as a statement.");
            }
            expect_symbol(")", "instantiation closing paren ')'");
            auto n = mknode("inst", ln);
            n->value = type_name;
            n->name = var_name;
            n->isConst = is_const;
            n->constraint = constraint;
            if (ctorArgs) n->kids.push_back(ctorArgs);
            return n;
        }

        // Behavior literal: [(params) mode (outputs) { body }]
        // 行为字面量：[(参数) 模式 (输出) { 体 }]
        AstNodePtr parse_behavior() {
            long long ln = peek().line;
            expect_symbol("[", "behavior opening bracket '['");
            auto node = mknode("behavior", ln);
            node->kids.push_back(parse_sign_core());

            expect_symbol("{", "behavior body opening brace '{'");
            auto body = mknode("block", peek().line);
            while (!at("<symbol>", "}")) {
                if (at_eof()) {
                    fail("Unterminated behavior body (missing '}').");
                }
                body->kids.push_back(parse_stmt());
            }
            expect_symbol("}", "behavior body closing brace '}'");
            node->kids.push_back(body);
            expect_symbol("]", "behavior closing bracket ']'");
            return node;
        }

        // Behavior-signature core: (params) mode (outputs). Shared by behavior
        // literals and contract signature items.
        // 行为签名核心：(参数列表) 模式 (输出列表)。行为字面量与约束签名
        // 条目共用。
        AstNodePtr parse_sign_core() {
            long long ln = peek().line;
            auto node = mknode("sign", ln);

            expect_symbol("(", "parameter list opening paren '('");
            auto params = mknode("params", peek().line);
            if (!at("<symbol>", ")")) {
                params->kids.push_back(parse_param());
                while (at("<symbol>", ",")) {
                    advance();
                    params->kids.push_back(parse_param());
                }
            }
            expect_symbol(")", "parameter list closing paren ')'");
            node->kids.push_back(params);

            auto mode_tok = expect_name("behavior mode ('->', '~>', or '=>')");
            if (mode_tok.value != "->" && mode_tok.value != "~>"
                && mode_tok.value != "=>") {
                fail("Behavior mode must be one of '->', '~>', or '=>', "
                     "but saw '" + mode_tok.value + "'.");
            }
            node->value = mode_tok.value;

            expect_symbol("(", "output list opening paren '('");
            auto outputs = mknode("params", peek().line);
            if (!at("<symbol>", ")")) {
                outputs->kids.push_back(parse_param());
                while (at("<symbol>", ",")) {
                    advance();
                    outputs->kids.push_back(parse_param());
                }
            }
            expect_symbol(")", "output list closing paren ')'");
            node->kids.push_back(outputs);
            return node;
        }

        // Signature parameter (spec 5.1 / 2.3.2 / 9.4 / 9.7), three forms:
        // 签名参数（文档 5.1 / 2.3.2 / 9.4 / 9.7），三种形式：
        //   name                     [(a) -> (r) ...]
        //   TypeName varName         [(std::Number other) => ...]
        //   name[qualifier]          [(a[Addable]) ...], qualifier ∈
        //                            {contract/class, @, @signature}
        AstNodePtr parse_param() {
            long long ln = peek().line;
            auto first = expect_name("parameter (name or type name)");
            // `void` is no longer a type / name keyword: use empty parentheses
            // `()` for "no value". Reject it as a parameter or output name.
            // `void` 不再是类型 / 名称关键字：用空括号 `()` 表示「无值」。
            // 拒绝把 `void` 用作参数 / 输出名。
            if (first.value == "void") {
                fail("'void' is not a type or name; use empty parentheses '()' "
                     "for no value / no return.");
            }

            // Greedily consume an optional [Constraint] (or a behavior
            // qualifier `[@]` / `[@(sign)]`) that may follow the leading name,
            // then decide whether a *separate* parameter name follows. This
            // mirrors parse_decl so both `Type[Constraint] VarName` and the
            // bare `Name[Constraint]` forms parse correctly.
            // 在首个名称之后贪婪吃掉可选的 [约束]（或行为限定 `[@]` / `[@(签名)]`），
            // 再判断其后是否还有独立参数名。与 parse_decl 对称，使
            // `类型[约束] 名` 与裸 `名[约束]` 两种形式都正确解析。
            std::string constraint;
            std::string behaviorMode;          // "", "behavior", "behaviorsign"
            AstNodePtr behaviorSign = nullptr;

            if (at("<symbol>", "[")) {
                advance();
                if (at("<symbol>", "@")) {
                    advance();
                    if (at("<symbol>", "(")) {
                        // Behavior qualifier with a signature:
                        // handler[@(x) -> (y)]
                        // 带签名的行为限定：handler[@(x) -> (y)]
                        behaviorMode = "behaviorsign";
                        behaviorSign = parse_sign_core();
                    } else {
                        behaviorMode = "behavior";   // handler[@]
                    }
                    expect_symbol("]", "qualifier closing bracket ']'");
                    auto p = mknode("param", ln);
                    p->name = first.value;
                    if (behaviorMode == "behaviorsign") {
                        p->mode = "behaviorsign";
                        p->kids.push_back(behaviorSign);
                    } else {
                        p->mode = "behavior";
                    }
                    return p;
                }
                // Constraint / class qualifier: name[Constraint]
                // 约束 / 类名限定：名[约束]
                auto limit = expect_name("constraint or class name");
                if (limit.value == "void") {
                    fail("'void' is not a constraint; use a contract / "
                         "class name, or empty parentheses '()' for no value.");
                }
                constraint = limit.value;
                expect_symbol("]", "qualifier closing bracket ']'");
            }

            // Two-name form `Type[Constraint] VarName`: a separate name follows.
            // 双名形式 类型[约束] 名：后面还有独立名称。
            if (at_type("<name>")) {
                auto var_tok = advance();
                auto p = mknode("param", ln);
                p->mode = "type";
                p->value = first.value;     // type name (e.g. std::Number)
                p->name = var_tok.value;    // parameter name
                p->constraint = constraint; // may be empty
                return p;
            }

            // Single-name form: `first` IS the parameter name (optionally
            // constrained, e.g. `x[Addable]`).
            // 单名形式：first 即参数名（可带约束，如 `x[Addable]`）。
            auto p = mknode("param", ln);
            p->name = first.value;
            if (!constraint.empty()) {
                p->mode = "constraint";
                p->value = constraint;       // type tag seen by enforce_sign
                p->constraint = constraint;  // #Contract name seen by
                                            // check_constraint at call time
            } else {
                p->mode = "none";           // bare name: no type constraint
            }
            return p;
        }

    public:

        // Construct a parser over the given source: run the lexer, collect the
        // token stream (with a trailing <eof>), and split source lines for
        // error context.
        // 用给定源码构造解析器：跑 Lexer 收集 token 流（含结尾 <eof>），
        // 并切分源码行表供报错上下文。
        explicit Parser(const std::string& source) {
            lexer::Lexer lx(source);
            while (true) {
                auto t = lx.lex();
                toks.push_back(t);
                if (t.typetag == "<eof>") {
                    break;
                }
            }
            selflines.push_back("<errorLine>");
            for (auto line : source | std::views::split('\n')) {
                selflines.push_back(
                    std::string(line.begin(), line.end())
                );
            }
        }

        // The single public parsing entry point: parse the whole program.
        // 唯一的公开解析入口：解析整个程序。
        AstNodePtr parse_program() {
            auto root = mknode("program", peek().line);
            while (!at_eof()) {
                if (at("<symbol>", "&")) {
                    root->kids.push_back(parse_import());
                } else if (at("<symbol>", "$")) {
                    root->kids.push_back(parse_classdef());
                } else if (at("<symbol>", "#")) {
                    root->kids.push_back(parse_contractdef());
                } else {
                    // Per the language designer: only import / class /
                    // contract at the top level.
                    // 语言设计者钦定：顶层只放导入 / 类 / 约束。
                    fail("Top level only allows module import (&name;), class "
                         "definition ($name {...}), and contract definition "
                         "(#name {...}); other statements such as variable "
                         "definitions must live inside a closure "
                         "(behavior body).");
                }
                // A top-level definition is a statement and may be terminated
                // by a semicolon. Tolerate an optional (possibly repeated) `;`
                // so `};` / `&m; $C {...};` parse exactly like `}` / `&m; $C {...}`.
                // 顶层定义是语句，可有分号结尾。容忍可选（乃至连续多个）的 `;`，
                // 使 `};`、`&m; $C {...};` 与无分号写法等价可解析。
                while (at("<symbol>", ";")) advance();
            }
            return root;
        }
    };

} // namespace parser

#endif
