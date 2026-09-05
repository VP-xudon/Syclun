// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lexer.hpp
//
// Synth OOP lexical analyzer (per "Synth OOP Language Spec v1.26").
// Synth OOP 词法分析器（依据《Synth OOP 语言文档 v1.26》）。
//
// Design principle: the dumber the lexer, the better.
// 设计原则：Lexer 越笨越好。
//   The lexer only splits the character stream into tokens; it does
//   no contextual analysis:
//   Lexer 只负责把字符流切成 token，不做任何上下文分析：
//   - It recognizes no keywords (true / false / if_ / void are all
//     <name>);
//     不识别关键字（true / false / if_ / void 一律是 <name>）；
//   - It does not distinguish the meaning of `-`: the instantiation
//     prefix `-(Type var)` and the method name `a.-(b)` are
//     lexically identical (<name>"-" + <symbol>"("), and the parser
//     interprets them by position;
//     不区分 `-` 的含义：实例化前缀 `-(类型 变量)` 与方法名
//     `a.-(b)` 在词法层完全同形（<name>"-" + <symbol>"("），
//     由 Parser 按位置解释；
//   - It does not split the `!` modifier: `!` is a name character,
//     so `std::Number!`, `@!get`, `@!=:` are single <name> tokens,
//     and leading/trailing `!` is stripped by the parser
//     (the spec 2.3.2 writes `@!=`, `@<=`, proving operator chars
//     belong to names);
//     不拆 `!` 修饰符：`!` 是名称字符，`std::Number!`、`@!get`、
//     `@!=:` 整体是单个 <name>，前缀/后缀的 `!` 由 Parser 剥离
//     （文档 2.3.2 写作 `@!=`、`@<=`，印证运算符字符属于名称）；
//   - `=>`, `~>`, `->` are <name> (behavior-mode arrows are made of
//     operator characters).
//     `=>` `~>` `->` 是 <name>（行为模式箭头由运算符字符组成）。
//
// Five token categories (plus a starting line number `line`, used by
// the parser for error localization):
// Token 五类（另附起始行号 line，供 Parser 报错定位）：
//   <number>  numeric literal (negatives and decimals): 10, 3.14, -1
//   <number>  数字字面量（含负数与小数）：10、3.14、-1
//   <string>  string literal (value excludes quotes): "Yeah."
//   <string>  字符串字面量（值不含引号）："Yeah."
//   <name>    name = longest match of letters/digits/_ + operator
//             chars + - * / % < > = ! : ~
//   <name>    名称 = 字母/数字/_ + 运算符字符 + - * / % < > = ! : ~
//             longest match (so +, !=, <=, ::, ~, =:, :=, ->, ~>, =>,
//             std::Number, !get are all single <name>)
//             （故 +、!=、<=、::、~、=:、:=、->、~>、=>、
//             std::Number、!get 都是单个 <name>）
//   <symbol>  structural symbols: << >> ( ) [ ] { } , ; . $ & @ #
//   <symbol>  结构符号：<< >> ( ) [ ] { } , ; . $ & @ #
//   <eof>     end of input (subsequent lex() calls keep returning it)
//   <eof>     输入结束（此后反复调用 lex() 恒返回 <eof>）
//
// Key rules:
// 关键规则：
//   - `<<` `>>` are flow symbols, matched with priority over names
//     (x<<y becomes three tokens, not the name "x<<y"; a.<=(b) is
//     unaffected and stays the name "<=");
//     `<<` `>>` 是流符号，匹配优先于名称（x<<y 切成三个 token，
//     而不是名称 "x<<y"；a.<=(b) 不受影响，仍是名称 "<="）；
//   - `//` is a line comment (no block comments are defined; `//`
//     takes priority over the name `/`);
//     `//` 是行注释（语言未定义块注释，`//` 优先于名称 `/`）；
//   - `-` immediately followed by a digit is a negative literal (the
//     language has no infix operators, so `-digit` is unambiguous;
//     see spec 11.2.3 `value << -1;`);
//     `-` 后紧跟数字是负数字面量（语言无中缀运算，`-数字` 无歧义；
//     文档 11.2.3 的 `value << -1;`）；
//   - a `.` inside a number is a decimal point only when followed by
//     a digit; otherwise it is the call symbol
//     (3.repeat_(...), 10.+(5) both split correctly).
//     数字中的 `.` 仅在后跟数字时视为小数点，否则是调用符号
//     （3.repeat_(...)、10.+(5) 均正确切分）。
//   - Colons follow the "colon principle": a `:` is only legal as part of
//     the `::` separator (namespace / constructor) or the `=:` (publish) /
//     `:=` (receive) method names. Any other `:` is a lexical error.
//     冒号遵循“冒号原则”：`：` 仅允许出现在 `::`（命名空间 / 构造）
//     分隔符或 `=:`（公布）/ `:=`（接收）方法名中；其它 `:` 为词法错误。
// ============================================================

#ifndef LEXER_HPP
#define LEXER_HPP

#include <string>
#include <vector>
#include <ranges>

#include "exception_throw.hpp"

namespace lexer {

    // A single lexical token.
    // 单个词法 token。
    struct Token {
        std::string typetag;       // category, e.g. "<name>" / "<number>"
        std::string value = "";    // text payload (empty for <eof>) / 载荷
        long long line = 0;        // 1-based start line —— pure positional
                                   // info for the parser to localize errors.
                                   // 起始行号（1 起）——纯位置信息，供 Parser 报错定位
        long long col = 0;         // 1-based start column / 起始列号（1 起）
    };

    class Lexer {
        std::string self;               // whole source text / 源文本
        std::vector<std::string> selflines;  // source lines (for error context)
                                             // 源码行表（报错上下文）
        std::vector<Token> tokens;      // emitted token stream (debug/parser)
                                        // 已产出的 token 流（供 Parser/调试取用）
        long long int cur = 0;          // cursor into self / 源游标
        long long int curline = 1;      // current 1-based line / 当前行号
        long long int curcol = 0;       // 0-based column before current char
                                       // 当前字符之前的列数（0 起），token 列号 = curcol+1

        // ---- basic cursor movement / 基础移动 ----

        bool ended() {
            return cur >= (long long)self.size();
        }
        char peek(long long off = 0) {
            long long i = cur + off;
            if (i >= (long long)self.size()) return '\0';
            return self[i];
        }
        void next() {
            if (ended()) return;               // bounds guard (self[size()]
                                               // legally returns '\0')
                                               // 越界保护（self[size()] 合法地返回 '\0'）
            if (self[cur] == '\n') {
                curline++;
                curcol = 0;
            } else {
                curcol++;
            }
            cur++;
        }
        void eat(char ch) {
            if (self[cur] == ch) {
                next();
            } else if (ended()) {
                fail(std::string("Expected '") + ch + "', but reached end of file.");
            } else {
                fail(std::string("Expected '") + ch + "', but not '" + self[cur] + "'.");
            }
        }
        void fail(const std::string &message) {
            diag::set_locus(diag::source_file(), curline, curcol + 1);
            Thrower.throwE("SyntaxError", message,
                           {std::to_string(curline) + " | " + selflines[curline]});
        }

        // ---- character classification / 字符分类 ----

        static bool is_digit(char c) {
            return c >= '0' && c <= '9';
        }
        // A name character is a letter, digit, underscore, or any operator
        // character (the latter may appear inside method names such as
        // a.+(b) or @!=).
        // 名称字符：字母、数字、下划线，以及全部运算符字符
        // （+ - * / % < > = ! : ~ 均可出现在方法名中，如 a.+(b)、@!=）。
        static bool is_name_char(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9')
                || c == '_' || c == '+' || c == '-' || c == '*' || c == '/'
                || c == '%' || c == '<' || c == '>' || c == '=' || c == '!'
                || c == ':' || c == '~';
        }

        // ---- token readers (longest match) / 各类 token 读取（最长匹配）----

        Token read_number() {
            Token res;
            res.typetag = "<number>";
            res.line = curline;
            res.col = curcol + 1;
            if (self[cur] == '-') {            // negative literal (no infix ops)
                                           // 负数字面量（语言无中缀运算，-数字 无歧义）
                res.value += '-';
                next();
            }
            bool has_dot = false;
            while (!ended()) {
                if (is_digit(self[cur])) {
                    res.value += self[cur];
                    next();
                } else if (self[cur] == '.' && !has_dot && is_digit(peek(1))) {
                    has_dot = true;            // decimal point only if digit follows
                                           // 小数点仅当后跟数字，否则留给调用符号
                    res.value += '.';
                    next();
                } else {
                    break;
                }
            }
            return res;
        }

        Token read_string() {
            Token res;
            res.typetag = "<string>";
            res.line = curline;
            res.col = curcol + 1;
            eat('"');                          // open quote / 开引号
            while (!ended() && self[cur] != '"') {
                if (self[cur] == '\n') {
                    fail("String literal not terminated before end of line.");
                }
                if (self[cur] == '\\') {        // escape sequence / 转义序列
                    next();                     // consume the backslash
                    if (ended()) {
                        fail("String literal ends with a dangling escape '\\'.");
                    }
                    char e = self[cur];
                    next();                     // consume the escape char
                    switch (e) {
                        case 'n': res.value += '\n'; break;
                        case 't': res.value += '\t'; break;
                        case 'r': res.value += '\r'; break;
                        case 'a': res.value += '\a'; break;
                        case 'b': res.value += '\b'; break;
                        case 'f': res.value += '\f'; break;
                        case 'v': res.value += '\v'; break;
                        case '0': res.value += '\0'; break;
                        case '\\': res.value += '\\'; break;
                        case '"': res.value += '"'; break;
                        case '\'': res.value += '\''; break;
                        default:
                            // Unknown escape: keep it verbatim (backslash +
                            // char) so no data is silently lost.
                            // 未知转义：原样保留（反斜杠 + 字符），避免静默丢数据。
                            res.value += '\\';
                            res.value += e;
                            break;
                    }
                    continue;
                }
                res.value += self[cur];        // pass through bytes (UTF-8 safe)
                                           // 按字节透传（UTF-8 中文串安全）
                next();
            }
            if (ended()) {
                fail("String literal not terminated before end of file.");
            }
            eat('"');                          // close quote / 闭引号
            return res;
        }

        // `<<`, `>>`, `//` are structural symbols / line comments with
        // priority over name matching everywhere (no legal name contains
        // them, so name scanning must yield here).
        // `<<` `>>` `//` 是结构符号/行注释，在任何位置都优先于名称最长匹配
        // （语言中不存在包含它们的合法名称，故名称扫描至此必须让位）。
        bool at_flow_or_comment() {
            char c = self[cur];
            return (c == '<' && peek(1) == '<')
                || (c == '>' && peek(1) == '>')
                || (c == '/' && peek(1) == '/');
        }

        // A name may contain a colon ONLY as part of the `::` separator
        // (namespace / constructor) or as part of the publish `=:` / receive
        // `:=` method names. Any other colon is a lexical error — this is the
        // language's "colon principle": except for the constructor `::` and
        // the `=:` / `:=` method names, no other colon form is allowed.
        //
        // The one exception is the v1.30 object-injection operator: a `:`
        // directly followed by `@` or `-` never reaches this function, because
        // lex() emits it as a standalone <symbol> ':' first (see the branch
        // above is_name_char). So reaching the `fail` below still means the
        // source used an illegal colon form.
        // 名称中的冒号仅允许出现在 `::`（命名空间 / 构造）分隔符，或公布
        // `=:` / 接收 `:=` 方法名中；其它冒号均为词法错误——这是语言的
        // “冒号原则”：除构造 `::` 与 `=:` / `:=` 方法名外，不允许任何其他
        // 冒号形式。
        //
        // 唯一例外是 v1.30 的对象注入操作符：紧跟 `@` 或 `-` 的 `:` 根本
        // 到不了本函数——lex() 已在 is_name_char 分支之前把它作为独立的
        // <symbol> ':' 吐出（见 lex() 中该分支）。故走到下面 fail 的，仍然
        // 是源码使用了非法冒号形式。
        Token read_name() {
            Token res;
            res.typetag = "<name>";
            res.line = curline;
            res.col = curcol + 1;
            while (!ended() && is_name_char(self[cur]) && !at_flow_or_comment()) {
                if (self[cur] == ':') {
                    char nxt = peek(1);
                    if (nxt == ':') {                 // namespace / constructor
                        res.value += "::";
                        next(); next();
                        continue;
                    } else if (nxt == '=') {          // := (receive)
                        res.value += ":=";
                        next(); next();
                        continue;
                    } else if (nxt == '@' || nxt == '-') {
                        // v1.30 object injection: `obj:@method` /
                        // `obj:-(Type v)`. The colon is a standalone <symbol>
                        // here, so end the name and let lex()'s injection
                        // branch emit the ':'.
                        // v1.30 对象注入：`对象:@方法` / `对象:-(类型 变量)`。
                        // 此处的冒号是独立 <symbol>，故在此结束名称，由
                        // lex() 的注入分支吐出 ':'。
                        break;
                    } else if (!res.value.empty() && res.value.back() == '=') {
                        res.value += ':';             // =: (publish)
                        next();
                        continue;
                    } else {
                        fail("Unexpected ':' (colons are only allowed in '::', '=:', ':=', or object injection 'obj:...' preceded by '@' or '-').");
                    }
                }
                res.value += self[cur];
                next();
            }
            return res;
        }

    public:
        Lexer(std::string _self) : self(_self) {
            selflines.push_back("<errorLine>");
            for (auto line : self | std::views::split('\n')) {
                // `line` is a view; convert it to a std::string before use.
                // 注意：这里 line 是一个 view，需要转换为 std::string 才能直接使用。
                selflines.push_back(std::string(line.begin(), line.end()));
            }
        }

        // Read the next token; skips whitespace and // comments; returns
        // <eof> forever once the input is exhausted.
        // 取下一个 token；自动跳过空白与 // 注释；结束后恒返回 <eof>。
        Token lex() {
            while (true) {
                while (!ended() && (self[cur] == ' '  || self[cur] == '\t'
                                 || self[cur] == '\r' || self[cur] == '\n'
                                 || self[cur] == '\v' || self[cur] == '\f')) {
                    next();                    // newline counted by next() /
                                           // 换行由 next() 统一计数
                }
                if (ended()) {
                    Token res;
                    res.typetag = "<eof>";
                    res.line = curline;
                    res.col = curcol + 1;
                    tokens.push_back(res);
                    return res;
                }
                char c = self[cur];
                if (c == '/' && peek(1) == '/') {          // line comment
                                                       // 行注释：吞到行尾（不含换行）
                    while (!ended() && self[cur] != '\n') next();
                    continue;                 // back to whitespace loop, let next()
                                           // count this newline
                                           // 回到空白循环，让 next() 数到这个换行
                }

                Token res;
                res.typetag = "<symbol>";
                res.line = curline;              // token start line (error locale)
                                           // token 起始行（供 Parser 定位报错）
                res.col = curcol + 1;            // token start column / 起始列
                if (c == '"') {
                    res = read_string();
                } else if (is_digit(c) || (c == '-' && is_digit(peek(1)))) {
                    res = read_number();
                } else if (c == '<' && peek(1) == '<') {    // flow symbol wins
                                                       // 流符号优先于名称匹配
                    res.value = "<<";
                    next(); next();
                } else if (c == '>' && peek(1) == '>') {
                    res.value = ">>";
                    next(); next();
                } else if (c == '(' || c == ')' || c == '[' || c == ']'
                        || c == '{' || c == '}' || c == ',' || c == ';'
                        || c == '.' || c == '$' || c == '&' || c == '@'
                        || c == '#') {
                    res.value = std::string(1, c);
                    next();
                } else if (c == ':' && (peek(1) == '@' || peek(1) == '-')) {
                    // v1.30 object-injection symbol. `Object:@method;` injects
                    // a behavior as a method; `Object:-(Type v);` adds a
                    // private attribute. A standalone ':' is a <symbol> here
                    // because the grammar reads it as a statement operator,
                    // not as part of a name. Every other colon still obeys the
                    // colon principle: only '::', '=:', ':=' remain legal.
                    // v1.30 对象注入符号。`Object:@方法;` 把行为注入为方法；
                    // `Object:-(类型 变量);` 新增私有属性。此处的 ':' 是
                    // <symbol>（语法把它当语句操作符读），而非名称的一部分。
                    // 其余冒号仍遵循冒号原则：仅 '::'、'=:'、':=' 合法。
                    res.value = ":";
                    next();
                } else if (is_name_char(c)) {
                    res = read_name();
                } else {
                    fail(std::string("Unexpected character '") + c + "'.");
                }
                tokens.push_back(res);
                return res;
            }
        }
    };
}

#endif
