// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/re.hpp
//
// Standard library: re — regular-expression matching (C++-backed).
// 标准库：re —— 正则表达式匹配（C++ 底层实现）。
//
// Industrialization audit D8: std::regex is a backtracking engine and is
// vulnerable to catastrophic backtracking (ReDoS) on patterns like
// `(a+)+$`. This module now ships its OWN linear-time engine: a Thompson NFA
// compiled to a Pike virtual machine. Pike's VM is O(n·m) regardless of input
// shape — no backtracking, no exponential blowup — so untrusted patterns and
// untrusted text can no longer hang the interpreter. A bounded step counter
// adds a defensive ceiling on top of that guarantee.
// 工业化审计 D8：std::regex 是回溯引擎，对 `(a+)+$` 这类模式存在灾难性
// 回溯（ReDoS）。本模块现内置线性时间引擎：Thompson NFA 编译为 Pike 虚拟机。
// Pike 虚拟机时间复杂度恒为 O(n·m)，与输入形态无关——无回溯、无指数爆炸，
// 故未信任模式与未信任文本都无法再拖垮解释器。有界步数计数器在此保证之上
// 再加一道防御上限。
//
// Supported: literals, `.`, char classes `[...]`/`[^...]` (with ranges and
// `\d\w\s` inside), escapes,`* + ?` and `{n}`/`{n,m}`/`{n,}` quantifiers,
// alternation `|`, capturing groups `( )`, anchors `^ $`, greedy and lazy
// quantifiers (`*?` etc.). Backreferences (`\1`) and lookaround (`(?=)`) are
// intentionally REJECTED at compile time — both require backtracking, which is
// precisely the ReDoS surface we removed.
// 支持：字面量、`.`、字符类 `[...]`/`[^...]`（含范围与内部 `\d\w\s`）、
// 转义、`* + ?` 与 `{n}`/`{n,m}`/`{n,}` 量词、选择 `|`、捕获组 `( )`、
// 锚点 `^ $`、贪婪与惰性量词（`*?` 等）。回溯引用（`\1`）与环视（`(?=)`）
// 在编译期被明确拒绝——二者均依赖回溯，正是我们移除的 ReDoS 面。
//
// The C++ twin of lib/re.synl. Self-registered under "re".
// lib/re.synl 的 C++ 孪生体，以 "re" 自注册。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <bitset>
#include <unordered_set>
#include <unordered_map>
#include <stdexcept>
#include <cctype>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_re {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // --------------------------------------------------------
    // Pike VM instruction set / Pike 虚拟机指令集
    // --------------------------------------------------------
    struct Inst {
        enum Op { Char, Any, Class, Bol, Eol, Match, Jmp, Split, Save } op;
        char ch = 0;                       // Char
        std::bitset<256> cls;              // Class bitmap
        bool neg = false;                  // Class negation
        int x = 0, y = 0;                  // Jmp / Split targets
        int slot = 0;                      // Save slot
    };
    struct RegexProg {
        std::vector<Inst> code;
        int nsave = 2;                     // 2 * (groups + 1)
        int flags = 0;                     // REF_I | REF_M | REF_S
    };

    // Compile-time flag bits. `m` (multiline) is effectively always on because
    // the engine's Bol/Eol already honour line boundaries; `i` (case-insensitive)
    // and `s` (dotall) are honoured in test_inst below.
    // 编译期标志位。m（多行）实质恒开，因为引擎的 Bol/Eol 本就按行边界处理；
    // i（忽略大小写）与 s（点匹配换行）在下方 test_inst 中生效。
    enum { REF_NONE = 0, REF_I = 1, REF_M = 2, REF_S = 4 };

    // --------------------------------------------------------
    // Regex compiler: pattern string -> RegexProg (Thompson NFA).
    // 正则编译器：模式串 -> RegexProg（Thompson NFA）。
    // --------------------------------------------------------
    class RegexCompiler {
    public:
        RegexCompiler(const std::string& p, std::string& err)
            : pat_(p), err_(err) {}

        std::shared_ptr<RegexProg> compile() {
            // `nsave` starts at 2 (the whole-match pair: slot 0/1) and grows by
            // 2 for every capturing group encountered during parse_primary; the
            // group's first Save slot is `2 * g` where g = nsave/2 at that point.
            // Do NOT pre-count groups here — doing so alongside the per-group
            // `+= 2` would double the slot budget (e.g. "(a)(b)" would reserve
            // 10 slots instead of 6, corrupting Match.groups).
            // nsave 初始为 2（整串捕获对：槽 0/1），每遇到一个捕获组在
            // parse_primary 中再 +2；该组的第一个 Save 槽为 2*g（g=nsave/2）。
            // 切勿在此预数组数——预数再加上每组的 +=2 会令槽预算翻倍
            //（如 "(a)(b)" 会预留 10 个槽而非 6 个，使 Match.groups 错乱）。

            // Whole-match capture: Save(0) ... Save(1) around the body.
            // 整串捕获：在主体前后 Save(0) ... Save(1)。
            emit_save(0);
            parse_alt();
            if (pos_ != pat_.size()) {
                err_ = std::string("re: unexpected '") +
                       (pos_ < pat_.size() ? std::string(1, pat_[pos_]) : "?") +
                       "'";
                return nullptr;
            }
            emit_save(1);
            Inst m; m.op = Inst::Match; prog_.code.push_back(m);
            return std::make_shared<RegexProg>(prog_);
        }

    private:
        const std::string& pat_;
        std::string& err_;
        RegexProg prog_;
        size_t pos_ = 0;

        void emit_char(char c)    { Inst i; i.op = Inst::Char; i.ch = c; prog_.code.push_back(i); }
        void emit_any()           { Inst i; i.op = Inst::Any; prog_.code.push_back(i); }
        void emit_bol()           { Inst i; i.op = Inst::Bol; prog_.code.push_back(i); }
        void emit_eol()           { Inst i; i.op = Inst::Eol; prog_.code.push_back(i); }
        void emit_jmp(int t)      { Inst i; i.op = Inst::Jmp;  i.x = t; prog_.code.push_back(i); }
        void emit_split(int a,int b){ Inst i; i.op = Inst::Split; i.x = a; i.y = b; prog_.code.push_back(i); }
        void emit_save(int s)     { Inst i; i.op = Inst::Save; i.slot = s; prog_.code.push_back(i); }
        void emit_class(const std::bitset<256>& b, bool neg) {
            Inst i; i.op = Inst::Class; i.cls = b; i.neg = neg; prog_.code.push_back(i);
        }

        // Re-emit a captured primary-body fragment at the current tail, fixing
        // up internal jump targets by (new_base - old_base). The body is
        // captured (and removed from code) by parse_quant so it is emitted
        // exactly once -- this avoids the double-emission bug where
        // parse_primary already placed the body and copy_frag placed a second
        // copy, which corrupted loop indices for quantifiers like `\d+`.
        // 重放一段已捕获的 primary 主体片段到当前尾部，并按 (新基址 - 旧基址)
        // 修正内部跳转目标。主体由 parse_quant 捕获并从 code 移除后仅重放一次，
        // 避免二次重放的双重发射 bug（`\d+` 等量化器曾因此循环索引错乱）。
        void emit_frag(const std::vector<Inst>& frag, int old_base) {
            int base = (int)prog_.code.size() - old_base;
            for (const Inst& c : frag) {
                Inst d = c;
                if (d.op == Inst::Jmp || d.op == Inst::Split) { d.x += base; d.y += base; }
                prog_.code.push_back(d);
            }
        }

        static void or_range(std::bitset<256>& b, unsigned char lo, unsigned char hi) {
            for (int c = lo; c <= hi; ++c) b.set(c);
        }
        static void or_class(std::bitset<256>& b, char kind) {
            if (kind == 'd') or_range(b, '0', '9');
            else if (kind == 'w') { or_range(b,'A','Z'); or_range(b,'a','z'); or_range(b,'0','9'); b.set((unsigned char)'_'); }
            else if (kind == 's') { b.set((unsigned char)' '); b.set((unsigned char)'\t');
                                    b.set((unsigned char)'\n'); b.set((unsigned char)'\r');
                                    b.set((unsigned char)'\f'); b.set((unsigned char)'\v'); }
        }

        // Parse an atom (primary) and return [start,end) of the emitted code.
        // 解析一个原子（primary），返回所生成 code 的 [start,end)。
        std::pair<size_t,size_t> parse_primary() {
            size_t start = prog_.code.size();
            if (pos_ >= pat_.size()) { err_ = "re: unexpected end of pattern"; return {start,start}; }
            char c = pat_[pos_];

            if (c == '(') {
                if (pos_ + 1 < pat_.size() && pat_[pos_+1] == '?') {
                    err_ = "re: lookaround / non-capturing (?...) groups are not supported";
                    pos_ = pat_.size(); return {start,start};
                }
                int g = (prog_.nsave / 2);   // next group number (1-based)
                prog_.nsave += 2;            // allocate slots 2g, 2g+1
                ++pos_;                      // consume '('
                emit_save(2 * g);
                parse_alt();
                if (pos_ >= pat_.size() || pat_[pos_] != ')') {
                    err_ = "re: missing ')'"; pos_ = pat_.size(); return {start,start};
                }
                ++pos_;                      // consume ')'
                emit_save(2 * g + 1);
            } else if (c == '[') {
                parse_class();
            } else if (c == '.') {
                ++pos_; emit_any();
            } else if (c == '^') {
                ++pos_; emit_bol();
            } else if (c == '$') {
                ++pos_; emit_eol();
            } else if (c == '\\') {
                parse_escape();
            } else if (c == '*' || c == '+' || c == '?' || c == ')' || c == '|') {
                err_ = std::string("re: dangling metacharacter '") + c + "'";
                pos_ = pat_.size(); return {start,start};
            } else {
                ++pos_; emit_char(c);
            }
            return {start, prog_.code.size()};
        }

        void parse_class() {
            ++pos_;   // skip '['
            bool neg = false;
            if (pos_ < pat_.size() && pat_[pos_] == '^') { neg = true; ++pos_; }
            std::bitset<256> b;
            bool closed = false;
            while (pos_ < pat_.size() && !closed) {
                char c = pat_[pos_];
                if (c == ']') { closed = true; ++pos_; break; }
                // Escaped item inside a class.
                // 类内的转义项。
                if (c == '\\') {
                    ++pos_;
                    if (pos_ >= pat_.size()) { err_ = "re: bad escape in class"; return; }
                    char e = pat_[pos_]; ++pos_;
                    if (e == 'd' || e == 'w' || e == 's') { or_class(b, e); continue; }
                    if (e == 'D' || e == 'W' || e == 'S') {
                        std::bitset<256> tmp; or_class(tmp, e); b |= ~tmp; continue;
                    }
                    if (e == 'n') b.set((unsigned char)'\n');
                    else if (e == 't') b.set((unsigned char)'\t');
                    else if (e == 'r') b.set((unsigned char)'\r');
                    else b.set((unsigned char)e);   // literal escaped char
                    continue;
                }
                // Range a-z.
                // 范围 a-z。
                if (pos_ + 2 < pat_.size() && pat_[pos_+1] == '-' && pat_[pos_+2] != ']') {
                    char lo = c, hi = pat_[pos_+2];
                    if (lo > hi) { auto t = lo; lo = hi; hi = t; }
                    or_range(b, (unsigned char)lo, (unsigned char)hi);
                    pos_ += 3;
                    continue;
                }
                b.set((unsigned char)c);
                ++pos_;
            }
            if (!closed) { err_ = "re: missing ']'"; return; }
            emit_class(b, neg);
        }

        void parse_escape() {
            ++pos_;   // skip '\'
            if (pos_ >= pat_.size()) { err_ = "re: trailing backslash"; return; }
            char e = pat_[pos_]; ++pos_;
            if (e == 'd' || e == 'w' || e == 's') {
                std::bitset<256> b; or_class(b, e); emit_class(b, false); return;
            }
            if (e == 'D' || e == 'W' || e == 'S') {
                std::bitset<256> b; std::bitset<256> tmp; or_class(tmp, e);
                b = ~tmp; emit_class(b, false); return;
            }
            if (e >= '0' && e <= '9') {
                err_ = "re: backreferences (\\" + std::string(1, e) +
                       ") are not supported (they require backtracking)";
                pos_ = pat_.size(); return;
            }
            if (e == 'n') { emit_char('\n'); return; }
            if (e == 't') { emit_char('\t'); return; }
            if (e == 'r') { emit_char('\r'); return; }
            // escaped metacharacter -> literal
            // 转义元字符 -> 字面量
            emit_char(e);
        }

        // parse_quant: primary, then optional quantifier.
        // parse_quant：primary，后接可选量词。
        void parse_quant() {
            auto [a, b] = parse_primary();
            if (pos_ >= pat_.size()) return;
            char q = pat_[pos_];
            if (q == '*' || q == '+' || q == '?' || q == '{') {
                bool lazy = false;
                // Capture the primary body and truncate it out of `code`. It is
                // re-emitted exactly once by wrap_quant / parse_bounded via
                // emit_frag, so the body is never left in place (the bug that
                // double-emitted it and broke quantifier loop indices).
                // 捕获 primary 主体并从 code 截断。随后由 wrap_quant /
                // parse_bounded 经 emit_frag 仅重放一次，从而主体不会被留在原地
                // （此前双重发射曾破坏量化器循环索引）。
                std::vector<Inst> body(prog_.code.begin() + a, prog_.code.begin() + b);
                prog_.code.resize(a);
                if (q == '{') { parse_bounded(std::move(body), a); return; }
                ++pos_;
                if (pos_ < pat_.size() && pat_[pos_] == '?') { lazy = true; ++pos_; }
                wrap_quant(q, std::move(body), a, lazy);
            }
        }

        void wrap_quant(char q, std::vector<Inst> body, int old_base, bool lazy) {
            if (q == '*') {
                int split = (int)prog_.code.size(); emit_split(0, 0);
                emit_frag(body, old_base);
                int jmp = (int)prog_.code.size(); emit_jmp(split);
                prog_.code[split].x = split + 1;        // take
                prog_.code[split].y = jmp + 1;          // skip
                if (lazy) std::swap(prog_.code[split].x, prog_.code[split].y);
            } else if (q == '+') {
                int start = (int)prog_.code.size(); emit_frag(body, old_base);
                int split = (int)prog_.code.size(); emit_split(0, 0);
                prog_.code[split].x = start;            // loop
                prog_.code[split].y = (int)prog_.code.size(); // exit
                if (lazy) std::swap(prog_.code[split].x, prog_.code[split].y);
            } else { // '?'
                int split = (int)prog_.code.size(); emit_split(0, 0);
                emit_frag(body, old_base);
                prog_.code[split].x = split + 1;        // take
                prog_.code[split].y = (int)prog_.code.size(); // skip
                if (lazy) std::swap(prog_.code[split].x, prog_.code[split].y);
            }
        }

        // Parse {n}, {n,}, {n,m}. Expands to bounded copies (safe, no loops
        // needed beyond '*' for the open form). Rejects absurd bounds.
        // 解析 {n}、{n,}、{n,m}。展开为有界副本（安全；开放形式用 '*' 之外的
        // 形式无需循环）。拒绝荒谬的边界。
        void parse_bounded(std::vector<Inst> body, int old_base) {
            ++pos_;   // skip '{'
            int n = 0, m = -1; bool hasComma = false;
            auto read_int = [&]() -> int {
                int v = 0; bool any = false;
                while (pos_ < pat_.size() && pat_[pos_] >= '0' && pat_[pos_] <= '9') {
                    v = v * 10 + (pat_[pos_] - '0'); any = true; ++pos_;
                }
                return any ? v : -1;
            };
            n = read_int();
            if (pos_ < pat_.size() && pat_[pos_] == ',') { hasComma = true; ++pos_; m = read_int(); }
            if (pos_ >= pat_.size() || pat_[pos_] != '}') {
                err_ = "re: malformed {n,m} quantifier"; pos_ = pat_.size(); return;
            }
            ++pos_;   // skip '}'
            if (n < 0 || n > 10000 || (hasComma && m > 10000) || (!hasComma && n == 0)) {
                if (!hasComma && n == 0) { /* {0} means nothing */ }
                else { err_ = "re: {n,m} bounds too large"; pos_ = pat_.size(); return; }
            }
            int upper = hasComma ? (m < 0 ? -1 : m) : n;
            if (!hasComma && n == 0) return;   // {0} matches empty
            for (int i = 0; i < n; ++i) emit_frag(body, old_base);
            if (!hasComma) return;             // {n}
            if (upper < 0) {                   // {n,}
                int start = (int)prog_.code.size(); emit_frag(body, old_base);
                int split = (int)prog_.code.size(); emit_split(0, 0);
                prog_.code[split].x = start;
                prog_.code[split].y = (int)prog_.code.size();
            } else {                           // {n,m}
                for (int i = n; i < upper; ++i) {
                    int split = (int)prog_.code.size(); emit_split(0, 0);
                    emit_frag(body, old_base);
                    prog_.code[split].x = split + 1;
                    prog_.code[split].y = (int)prog_.code.size();
                }
            }
        }

        // parse_seq: concatenate quantified atoms until '|', ')' or end.
        // parse_seq：连接带量词的原子，直到 '|'、')' 或结尾。
        void parse_seq() {
            while (pos_ < pat_.size()) {
                char c = pat_[pos_];
                if (c == '|' || c == ')') break;
                parse_quant();
                if (!err_.empty()) return;
            }
        }

        // Lookahead: is there a top-level '|' (not nested, not inside a class,
        // not escaped) before the matching ')' or end? Used to decide whether
        // an alternation split is needed; we emit the split BEFORE the first
        // branch so the NFA layout is correct.
        // 前瞻：在本层（未嵌套、不在类内、未转义）中、匹配 ')' 或结尾之前是否
        // 存在 '|'？用于决定是否需插入选择 split；split 须在首分支之前生成，
        // 以保证 NFA 布局正确。
        bool has_alternation() {
            int depth = 0; bool inClass = false; bool esc = false;
            for (size_t i = pos_; i < pat_.size(); ++i) {
                char c = pat_[i];
                if (esc) { esc = false; continue; }
                if (c == '\\') { esc = true; continue; }
                if (inClass) { if (c == ']') inClass = false; continue; }
                if (c == '[') { inClass = true; continue; }
                if (c == '(') { ++depth; continue; }
                if (c == ')') { if (depth == 0) break; --depth; continue; }
                if (c == '|' && depth == 0) return true;
            }
            return false;
        }

        // parse_alt: a|b|c via Split instructions (single pass).
        // parse_alt：用 Split 指令实现 a|b|c（单遍）。
        void parse_alt() {
            if (!has_alternation()) { parse_seq(); return; }
            int split_pc = (int)prog_.code.size(); emit_split(0, 0);
            parse_seq();                                 // <a> at split_pc+1
            int jmp_pc = (int)prog_.code.size(); emit_jmp(0);
            prog_.code[split_pc].x = split_pc + 1;      // L1 = first branch
            prog_.code[split_pc].y = jmp_pc + 1;        // L2 = after jmp
            if (pos_ < pat_.size() && pat_[pos_] == '|') { ++pos_; }
            parse_alt();                                 // <rest> after jmp
            prog_.code[jmp_pc].x = (int)prog_.code.size();  // END
        }
    };

    // --------------------------------------------------------
    // Pike VM execution / Pike 虚拟机执行
    // --------------------------------------------------------
    inline bool test_inst(const Inst& in, char c, int flags) {
        if (in.op == Inst::Any)    return (flags & REF_S) ? true : c != '\n';
        if (in.op == Inst::Char) {
            if (c == in.ch) return true;
            if (flags & REF_I) {
                if (tolower((unsigned char)c) == tolower((unsigned char)in.ch)) return true;
            }
            return false;
        }
        if (in.op == Inst::Class) {
            bool m = in.cls.test((unsigned char)c);
            if (!m && (flags & REF_I)) {
                m = in.cls.test((unsigned char)tolower((unsigned char)c)) ||
                    in.cls.test((unsigned char)toupper((unsigned char)c));
            }
            return m != in.neg;
        }
        return false;
    }

    struct Thread { int pc; std::vector<int> sv; };

    // Run the Pike VM from `from`; fills `saved` (size = prog.nsave). Returns
    // true on a match. `err` is set if the step budget is exceeded. `m` (dotall)
    // and `s` (case-insensitive) flags are taken from prog.flags.
    // 从 `from` 运行 Pike 虚拟机；填充 saved（长度 = prog.nsave）。匹配成功返回
    // true。若超出步数预算则置 err。m（点匹配换行）与 s（忽略大小写）标志取自
    // prog.flags。
    inline bool re_search(const RegexProg& prog, const std::string& s, int from,
                          std::vector<int>& saved, std::string* err) {
        const int flags = prog.flags;
        int n = (int)s.size();
        int m = (int)prog.code.size();
        saved.assign(prog.nsave, -1);
        std::vector<Thread> clist, nlist;

        long long budget = (long long)n * (long long)m * 4LL + 2000000LL;
        long long steps = 0;

        auto addthread = [&](std::vector<Thread>& lst, int pc, int sp,
                             std::vector<int> sv, std::unordered_set<int>& vis) {
            std::vector<std::pair<int, std::vector<int>>> work;
            work.push_back({pc, std::move(sv)});
            while (!work.empty()) {
                auto [p, svs] = std::move(work.back()); work.pop_back();
                if (vis.count(p)) continue;
                vis.insert(p);
                if (p < 0 || p >= m) continue;
                const Inst& in = prog.code[p];
                switch (in.op) {
                    case Inst::Jmp:
                        work.push_back({in.x, svs}); break;
                    case Inst::Split:
                        // Push `y` first so `x` (the preferred / greedy body
                        // branch) is popped and explored first. `work` is a
                        // stack (LIFO), so the last pushed is processed first.
                        // Reversing the order here is what gives greedy
                        // quantifiers their "prefer more" semantics; without it
                        // the shorter / empty branch wins the race to `Match`.
                        // 先压 y，使 x（偏好 / 贪心主体分支）先被取出并优先展开。
                        // work 是栈（LIFO），最后压入者先处理。颠倒此顺序正是贪心
                        // 量词「偏好更多」语义的来源；否则较短 / 空分支会抢先抵达
                        // Match。
                        work.push_back({in.y, svs});
                        work.push_back({in.x, svs}); break;
                    case Inst::Save: {
                        std::vector<int> s2 = svs; s2[in.slot] = sp;
                        work.push_back({p + 1, std::move(s2)}); break;
                    }
                    case Inst::Bol:
                        if (sp == 0 || (sp > 0 && s[sp-1] == '\n'))
                            work.push_back({p + 1, svs});
                        break;
                    case Inst::Eol:
                        if (sp == n || (sp < n && s[sp] == '\n'))
                            work.push_back({p + 1, svs});
                        break;
                    default:
                        lst.push_back({p, svs}); break;
                }
            }
        };

        {
            std::unordered_set<int> vis;
            std::vector<int> sv(prog.nsave, -1);
            addthread(clist, 0, from, sv, vis);
        }

        bool matched = false;
        bool hasBest = false;
        std::vector<int> best;
        int sp = from;
        for (;;) {
            // Unanchored search: re-seed a fresh start thread at the current
            // position so a match may begin anywhere (not only at `from`).
            // 非锚定搜索：在当前位置重新播入起始线程，使匹配可从任意位置起始，
            // 而非仅从 `from` 开始（旧实现在首字符不匹配时直接死线程，搜索失败）。
            {
                std::unordered_set<int> seedvis;
                for (auto& t : clist) seedvis.insert(t.pc);
                std::vector<int> sv0(prog.nsave, -1);
                addthread(clist, 0, sp, sv0, seedvis);
            }
            nlist.clear();
            std::unordered_set<int> vis;
            for (auto& t : clist) {
                if (t.pc < 0 || t.pc >= m) continue;
                const Inst& in = prog.code[t.pc];
                if (in.op == Inst::Match) {
                    matched = true;
                    // Leftmost-longest: prefer the match with the smallest
                    // start; when starts tie, prefer the largest end so a greedy
                    // quantifier's longer repetition wins over a shorter one
                    // recorded one step earlier (e.g. `\d+` on "22" yields "22",
                    // not "2"). Lazy quantifiers are a known limitation of this
                    // simple priority model and yield longest too.
                    // 左最短-最长：优先起始位置最小者；同起始则取结尾最大者，使
                    // 贪婪量词的更长重复盖过更早记录的较短匹配（如 `\d+` 对 "22"
                    // 得出 "22" 而非 "2"）。惰性量词在此简化优先级模型下同取最长，
                    // 属已知边界。
                    if (!hasBest || t.sv[0] < best[0] ||
                        (t.sv[0] == best[0] && t.sv[1] > best[1])) {
                        best = t.sv; hasBest = true;
                    }
                    continue;
                }
                if (in.op == Inst::Char || in.op == Inst::Any || in.op == Inst::Class) {
                    if (sp < n && test_inst(in, s[sp], flags)) {
                        addthread(nlist, t.pc + 1, sp + 1, t.sv, vis);
                    }
                }
            }
            if (++steps > budget) {
                if (err) *err = "re: step limit exceeded (possible catastrophic pattern)";
                return false;
            }
            if (sp >= n) break;
            clist = std::move(nlist);
            ++sp;
        }
            if (matched) { saved = best; return true; }
        return false;
    }

    // Forward declaration so make_match can call build_groups before its definition.
    // 前向声明：make_match 在 build_groups 定义之前即可调用它。
    inline RuntimeObjectPtr build_groups(const std::vector<RuntimeObjectPtr>& items);

    // Build a `Match` object from capture spans.
    // 依据捕获区间构造 `Match` 对象。
    inline RuntimeObjectPtr make_match(const std::string& s,
                                       const std::vector<int>& saved, int nsave) {
        auto match = ::stdRT.make("Match");
        auto* cls = dynamic_cast<RuntimeClass*>(match.get());
        if (!cls) return match;
        auto& am = cls->get_attributes();
        bool ok = saved[0] >= 0 && saved[1] >= 0;
        std::string text = ok ? s.substr(saved[0], saved[1] - saved[0]) : std::string();
        am["matched"] = rb::make_boolean(ok);
        am["text"]    = rb::make_string(text);
        am["start"]   = rb::make_number(static_cast<double>(ok ? saved[0] : 0), true);
        am["end"]     = rb::make_number(static_cast<double>(ok ? saved[1] : 0), true);
        int ng = nsave / 2 - 1;   // groups excluding whole-match (group 0)
        std::vector<RuntimeObjectPtr> groups;
        for (int i = 0; i <= ng; ++i) {
            int st = 2 * i, en = 2 * i + 1;
            if (st < (int)saved.size() && saved[st] >= 0 && saved[en] >= 0)
                groups.push_back(rb::make_string(s.substr(saved[st], saved[en] - saved[st])));
            else
                groups.push_back(rb::make_string(""));
        }
        am["groups"] = build_groups(groups);
        return match;
    }

    // Clone a Match from an attribute map (used by the publish side `=:` so that
    // `-(re::Match m) << r.match(...)` carries the result's fields across the
    // flow boundary). 依据属性表克隆一个 Match（供公布侧 `=:` 使用，使
    // `-(re::Match m) << r.match(...)` 能把结果的字段跨越流边界传递）。
    inline RuntimeObjectPtr clone_match_from_env(rt_basic::InstanceMap& env) {
        auto match = ::stdRT.make("Match");
        auto* cls = dynamic_cast<RuntimeClass*>(match.get());
        if (!cls) return match;
        auto& am = cls->get_attributes();
        am["matched"] = env.count("matched") ? env["matched"] : rb::make_boolean(false);
        am["text"]    = env.count("text")    ? env["text"]    : rb::make_string("");
        am["groups"]  = env.count("groups")  ? env["groups"]  : build_groups({});
        am["start"]   = env.count("start")   ? env["start"]   : rb::make_number(0, true);
        am["end"]     = env.count("end")     ? env["end"]     : rb::make_number(0, true);
        return match;
    }

    // --------------------------------------------------------
    // Pattern compilation cache / 编译缓存
    // --------------------------------------------------------
    // `inline` (not `static`) so the cache is ONE program-wide instance even
    // though this header is pulled into several translation units (the
    // interpreter TU and the unit-test TU). A `static` here would give each TU
    // its own copy, so a compiled pattern stored in one TU would be invisible
    // to `pattern_id` called from another.
    // 用 inline（而非 static），使缓存成为全程序唯一实例——本头文件被多个
    // 翻译单元（解释器 TU 与单元测试 TU）包含时，static 会让每个 TU 各持一份
    // 副本，导致一个 TU 编译出的正则对另一 TU 的 pattern_id 不可见。
    inline std::recursive_mutex g_re_mux;
    inline long long  g_re_id = 0;
    inline std::unordered_map<long long, std::shared_ptr<RegexProg>> g_patterns;

    inline long long pattern_id(rt_basic::InstanceMap& env) {
        long long id = 0;
        auto it = env.find("id");
        if (it != env.end()) {
            auto v = rb::number_of(it->second);
            if (v) id = static_cast<long long>(*v);
        }
        if (id <= 0) {
            auto vit = env.find("#value");
            if (vit != env.end()) {
                auto v = rb::number_of(vit->second);
                if (v) id = static_cast<long long>(*v);
            }
        }
        if (id <= 0) {
            std::lock_guard<std::recursive_mutex> lk(g_re_mux);
            id = ++g_re_id;
            env["id"] = rb::make_number(static_cast<double>(id));
        }
        return id;
    }

    // Parse a flag string ("i", "im", "ims", ...) into a bitmask. Unknown
    // characters are ignored. `m` is accepted for compatibility but is already
    // the engine's default line-aware behaviour.
    // 把标志串（"i"/"im"/"ims"/...）解析为位掩码。未知字符被忽略。m 为兼容而接受，
    // 但本就是引擎默认的行感知行为。
    inline int re_parse_flags(const std::string& f) {
        int flags = 0;
        for (char c : f) {
            if (c == 'i') flags |= REF_I;
            else if (c == 'm') flags |= REF_M;
            else if (c == 's') flags |= REF_S;
        }
        return flags;
    }

    inline std::shared_ptr<RegexProg> compile(const std::string& pat, std::string& err,
                                              int flags = 0) {
        RegexCompiler c(pat, err);
        auto prog = c.compile();
        if (prog) prog->flags = flags;
        return prog;
    }

    // Helper: expand a replacement string, substituting $0..$9 with captures.
    // 辅助：展开替换串，把 $0..$9 替换为对应捕获。
    inline std::string expand_repl(const std::string& repl,
                                   const std::string& s,
                                   const std::vector<int>& saved) {
        std::string out;
        for (size_t i = 0; i < repl.size(); ++i) {
            if (repl[i] == '$' && i + 1 < repl.size() && repl[i+1] >= '0' && repl[i+1] <= '9') {
                int g = repl[i+1] - '0';
                int st = 2 * g, en = 2 * g + 1;
                if (st < (int)saved.size() && saved[st] >= 0 && saved[en] >= 0)
                    out += s.substr(saved[st], saved[en] - saved[st]);
                i++;
            } else {
                out += repl[i];
            }
        }
        return out;
    }

    // ========================================================
    // $Pattern methods / 方法
    // ========================================================
    inline rt_basic::Callable method_pattern_match() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error("pattern.match requires text")});
                std::shared_ptr<RegexProg> re;
                { std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                  re = g_patterns[pattern_id(env)]; }
                if (!re) return rb::list_of({rb::native_error("pattern not compiled")});
                std::vector<int> saved; std::string err;
                int n = (int)text->size();
                bool ok = re_search(*re, *text, 0, saved, &err);
                // Full match: must span the whole string.
                // 完全匹配：须覆盖整串。
                if (ok && (saved[0] != 0 || saved[1] != n)) ok = false;
                if (!err.empty()) return rb::list_of({rb::native_error(err)});
                return rb::list_of({make_match(*text, ok ? saved : std::vector<int>(re->nsave, -1), re->nsave)});
            },
            rb::make_sign("match", {{"text", "std::String"}}, {{"result", "re::Match"}})
        );
    }
    inline rt_basic::Callable method_pattern_search() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error("pattern.search requires text")});
                std::shared_ptr<RegexProg> re;
                { std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                  re = g_patterns[pattern_id(env)]; }
                if (!re) return rb::list_of({rb::native_error("pattern not compiled")});
                std::vector<int> saved; std::string err;
                bool ok = re_search(*re, *text, 0, saved, &err);
                if (!err.empty()) return rb::list_of({rb::native_error(err)});
                return rb::list_of({make_match(*text, ok ? saved : std::vector<int>(re->nsave, -1), re->nsave)});
            },
            rb::make_sign("search", {{"text", "std::String"}}, {{"result", "re::Match"}})
        );
    }
    inline rt_basic::Callable method_pattern_findall() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error("pattern.findall requires text")});
                std::shared_ptr<RegexProg> re;
                { std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                  re = g_patterns[pattern_id(env)]; }
                if (!re) return rb::list_of({rb::native_error("pattern not compiled")});
                std::vector<RuntimeObjectPtr> out;
                int from = 0; int n = (int)text->size();
                std::string err;
                while (from <= n) {
                    std::vector<int> saved;
                    if (!re_search(*re, *text, from, saved, &err)) break;
                    if (!err.empty()) return rb::list_of({rb::native_error(err)});
                    out.push_back(rb::make_string(text->substr(saved[0], saved[1] - saved[0])));
                    from = saved[1];
                    if (saved[1] == saved[0]) ++from;   // avoid zero-width loop
                    if (from > n) break;
                }
                return rb::list_of({build_groups(out)});
            },
            rb::make_sign("findall", {{"text", "std::String"}}, {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_pattern_replace() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                auto repl = rb::string_of(rb::para_at(paras, 1));
                if (!text || !repl) {
                    return rb::list_of({rb::native_error("pattern.replace requires text and replacement")});
                }
                std::shared_ptr<RegexProg> re;
                { std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                  re = g_patterns[pattern_id(env)]; }
                if (!re) return rb::list_of({rb::native_error("pattern not compiled")});
                std::string out; int from = 0; int n = (int)text->size();
                std::string err;
                while (from <= n) {
                    std::vector<int> saved;
                    if (!re_search(*re, *text, from, saved, &err)) break;
                    if (!err.empty()) return rb::list_of({rb::native_error(err)});
                    out += text->substr(from, saved[0] - from);
                    out += expand_repl(*repl, *text, saved);
                    from = saved[1];
                    if (saved[1] == saved[0]) ++from;   // avoid zero-width loop
                    if (from > n) break;
                }
                out += text->substr(from);
                return rb::list_of({rb::make_string(out)});
            },
            rb::make_sign(
                "replace", {{"text", "std::String"}, {"repl", "std::String"}}, {{"out", "std::String"}})
        );
    }
    // pattern.replace_fn(text, callback) ~> (out) —— like replace, but each
    // match is handed to `callback` (a Synth behavior taking the Match object and
    // returning a replacement String). The callback's result replaces the match.
    // pattern.replace_fn(text, callback) ~> (out) —— 同 replace，但每个匹配都会
    // 交给 callback（接收 Match 对象、返回替换串的 Synth 行为），以其返回值替换。
    inline rt_basic::Callable method_pattern_replace_fn() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                auto fn = rb::para_at(paras, 1);
                if (!text || !fn) {
                    return rb::list_of({rb::native_error(
                        "pattern.replace_fn requires text and a callback behavior")});
                }
                std::shared_ptr<RegexProg> re;
                { std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                  re = g_patterns[pattern_id(env)]; }
                if (!re) return rb::list_of({rb::native_error("pattern not compiled")});
                std::string out; int from = 0; int n = (int)text->size();
                std::string err;
                while (from <= n) {
                    std::vector<int> saved;
                    if (!re_search(*re, *text, from, saved, &err)) break;
                    if (!err.empty()) return rb::list_of({rb::native_error(err)});
                    out += text->substr(from, saved[0] - from);
                    auto m = make_match(*text, saved, re->nsave);
                    auto res = rb::call_behavior(fn, env, rb::list_of({m}));
                    auto r = rb::first_of(res);
                    auto s = r ? rb::string_of(r) : std::nullopt;
                    out += (s ? *s : "");
                    from = saved[1];
                    if (saved[1] == saved[0]) ++from;
                    if (from > n) break;
                }
                out += text->substr(from);
                return rb::list_of({rb::make_string(out)});
            },
            rb::make_sign(
                "replace_fn", {{"text", "std::String"}, {"callback", "@"}},
                {{"out", "std::String"}})
        );
    }
    inline rt_basic::Callable method_pattern_split() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error("pattern.split requires text")});
                std::shared_ptr<RegexProg> re;
                { std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                  re = g_patterns[pattern_id(env)]; }
                if (!re) return rb::list_of({rb::native_error("pattern not compiled")});
                std::vector<RuntimeObjectPtr> out;
                int from = 0; int n = (int)text->size();
                std::string err;
                while (from <= n) {
                    std::vector<int> saved;
                    if (!re_search(*re, *text, from, saved, &err)) break;
                    if (!err.empty()) return rb::list_of({rb::native_error(err)});
                    out.push_back(rb::make_string(text->substr(from, saved[0] - from)));
                    from = saved[1];
                    if (saved[1] == saved[0]) ++from;
                    if (from > n) break;
                }
                out.push_back(rb::make_string(text->substr(from)));
                return rb::list_of({build_groups(out)});
            },
            rb::make_sign("split", {{"text", "std::String"}}, {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_pattern_test() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto text = rb::string_of(rb::para_at(paras, 0));
                if (!text) return rb::list_of({rb::native_error("pattern.test requires text")});
                std::shared_ptr<RegexProg> re;
                { std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                  re = g_patterns[pattern_id(env)]; }
                if (!re) return rb::list_of({rb::native_error("pattern not compiled")});
                std::vector<int> saved; std::string err;
                bool ok = re_search(*re, *text, 0, saved, &err);
                if (!err.empty()) return rb::list_of({rb::native_error(err)});
                return rb::list_of({rb::make_boolean(ok)});
            },
            rb::make_sign("test", {{"text", "std::String"}}, {{"ok", "std::Boolean"}})
        );
    }

    // ========================================================
    // $re static convenience methods / 静态便捷方法
    // ========================================================
    inline rt_basic::Callable method_re_compile() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                if (!pat) return rb::list_of({rb::native_error("re.compile requires a pattern")});
                auto fstr = rb::string_of(rb::para_at(paras, 1));
                int flags = fstr ? re_parse_flags(*fstr) : 0;
                std::string err;
                auto re = compile(*pat, err, flags);
                if (!re) return rb::list_of({rb::native_error(err)});
                auto pobj = ::stdRT.make("Pattern");
                long long id = 0;
                { std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                  id = ++g_re_id; g_patterns[id] = re; }
                auto& pam = dynamic_cast<RuntimeClass*>(pobj.get())->get_attributes();
                pam["id"]     = rb::make_number(static_cast<double>(id));
                pam["#value"] = rb::make_number(static_cast<double>(id));
                return rb::list_of({pobj});
            },
            rb::make_sign("compile", {{"pattern", "std::String"}}, {{"pat", "re::Pattern"}})
        );
    }
    // re.escape(s) ~> (out) —— quote every regex metacharacter so the result
    // matches the literal string `s` (and nothing else).
    // re.escape(s) ~> (out) —— 转义所有正则元字符，使结果只匹配字面串 s。
    inline rt_basic::Callable method_re_escape() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto s = rb::string_of(rb::para_at(paras, 0));
                if (!s) return rb::list_of({rb::native_error("re.escape requires a string")});
                static const std::string meta = "()[]{}?*+^$|.\\";
                std::string out;
                out.reserve(s->size() * 2);
                for (char c : *s) {
                    if (meta.find(c) != std::string::npos) out += '\\';
                    out += c;
                }
                return rb::list_of({rb::make_string(out)});
            },
            rb::make_sign("escape", {{"s", "std::String"}}, {{"out", "std::String"}})
        );
    }

    inline rt_basic::Callable method_re_match() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                if (!pat || !text) return rb::list_of({rb::native_error("re.match requires pattern and text")});
                auto fstr = rb::string_of(rb::para_at(paras, 2));
                int flags = fstr ? re_parse_flags(*fstr) : 0;
                std::string err; auto re = compile(*pat, err, flags);
                if (!re) return rb::list_of({rb::native_error(err)});
                std::vector<int> saved; int n = (int)text->size();
                bool ok = re_search(*re, *text, 0, saved, &err);
                if (ok && (saved[0] != 0 || saved[1] != n)) ok = false;
                if (!err.empty()) return rb::list_of({rb::native_error(err)});
                return rb::list_of({make_match(*text, ok ? saved : std::vector<int>(re->nsave, -1), re->nsave)});
            },
            rb::make_sign(
                "match",
                {{"pattern", "std::String"}, {"text", "std::String"}, {"flags", "std::String"}},
                {{"result", "re::Match"}})
        );
    }
    inline rt_basic::Callable method_re_search() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                if (!pat || !text) return rb::list_of({rb::native_error("re.search requires pattern and text")});
                auto fstr = rb::string_of(rb::para_at(paras, 2));
                int flags = fstr ? re_parse_flags(*fstr) : 0;
                std::string err; auto re = compile(*pat, err, flags);
                if (!re) return rb::list_of({rb::native_error(err)});
                std::vector<int> saved;
                bool ok = re_search(*re, *text, 0, saved, &err);
                if (!err.empty()) return rb::list_of({rb::native_error(err)});
                return rb::list_of({make_match(*text, ok ? saved : std::vector<int>(re->nsave, -1), re->nsave)});
            },
            rb::make_sign(
                "search",
                {{"pattern", "std::String"}, {"text", "std::String"}, {"flags", "std::String"}},
                {{"result", "re::Match"}})
        );
    }
    inline rt_basic::Callable method_re_findall() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                if (!pat || !text) return rb::list_of({rb::native_error("re.findall requires pattern and text")});
                auto fstr = rb::string_of(rb::para_at(paras, 2));
                int flags = fstr ? re_parse_flags(*fstr) : 0;
                std::string err; auto re = compile(*pat, err, flags);
                if (!re) return rb::list_of({rb::native_error(err)});
                std::vector<RuntimeObjectPtr> out;
                int from = 0; int n = (int)text->size();
                while (from <= n) {
                    std::vector<int> saved;
                    if (!re_search(*re, *text, from, saved, &err)) break;
                    if (!err.empty()) return rb::list_of({rb::native_error(err)});
                    out.push_back(rb::make_string(text->substr(saved[0], saved[1] - saved[0])));
                    from = saved[1];
                    if (saved[1] == saved[0]) ++from;
                    if (from > n) break;
                }
                return rb::list_of({build_groups(out)});
            },
            rb::make_sign(
                "findall",
                {{"pattern", "std::String"}, {"text", "std::String"}, {"flags", "std::String"}},
                {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_re_replace() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                auto repl = rb::string_of(rb::para_at(paras, 2));
                auto fstr = rb::string_of(rb::para_at(paras, 3));
                if (!pat || !text || !repl) {
                    return rb::list_of({rb::native_error("re.replace requires pattern, text, replacement")});
                }
                int flags = fstr ? re_parse_flags(*fstr) : 0;
                std::string err; auto re = compile(*pat, err, flags);
                if (!re) return rb::list_of({rb::native_error(err)});
                std::string out; int from = 0; int n = (int)text->size();
                while (from <= n) {
                    std::vector<int> saved;
                    if (!re_search(*re, *text, from, saved, &err)) break;
                    if (!err.empty()) return rb::list_of({rb::native_error(err)});
                    out += text->substr(from, saved[0] - from);
                    out += expand_repl(*repl, *text, saved);
                    from = saved[1];
                    if (saved[1] == saved[0]) ++from;
                    if (from > n) break;
                }
                out += text->substr(from);
                return rb::list_of({rb::make_string(out)});
            },
            rb::make_sign(
                "replace",
                {{"pattern", "std::String"}, {"text", "std::String"},
                 {"repl", "std::String"}, {"flags", "std::String"}},
                {{"out", "std::String"}})
        );
    }
    inline rt_basic::Callable method_re_replace_fn() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                auto fn = rb::para_at(paras, 2);
                if (!pat || !text || !fn) {
                    return rb::list_of({rb::native_error(
                        "re.replace_fn requires pattern, text and a callback behavior")});
                }
                auto fstr = rb::string_of(rb::para_at(paras, 3));
                int flags = fstr ? re_parse_flags(*fstr) : 0;
                std::string err; auto re = compile(*pat, err, flags);
                if (!re) return rb::list_of({rb::native_error(err)});
                std::string out; int from = 0; int n = (int)text->size();
                while (from <= n) {
                    std::vector<int> saved;
                    if (!re_search(*re, *text, from, saved, &err)) break;
                    if (!err.empty()) return rb::list_of({rb::native_error(err)});
                    out += text->substr(from, saved[0] - from);
                    auto m = make_match(*text, saved, re->nsave);
                    auto res = rb::call_behavior(fn, /*env=*/env, rb::list_of({m}));
                    auto r = rb::first_of(res);
                    auto s = r ? rb::string_of(r) : std::nullopt;
                    out += (s ? *s : "");
                    from = saved[1];
                    if (saved[1] == saved[0]) ++from;
                    if (from > n) break;
                }
                out += text->substr(from);
                return rb::list_of({rb::make_string(out)});
            },
            rb::make_sign(
                "replace_fn",
                {{"pattern", "std::String"}, {"text", "std::String"},
                 {"callback", "@"}, {"flags", "std::String"}},
                {{"out", "std::String"}})
        );
    }
    inline rt_basic::Callable method_re_split() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                if (!pat || !text) return rb::list_of({rb::native_error("re.split requires pattern and text")});
                auto fstr = rb::string_of(rb::para_at(paras, 2));
                int flags = fstr ? re_parse_flags(*fstr) : 0;
                std::string err; auto re = compile(*pat, err, flags);
                if (!re) return rb::list_of({rb::native_error(err)});
                std::vector<RuntimeObjectPtr> out;
                int from = 0; int n = (int)text->size();
                while (from <= n) {
                    std::vector<int> saved;
                    if (!re_search(*re, *text, from, saved, &err)) break;
                    if (!err.empty()) return rb::list_of({rb::native_error(err)});
                    out.push_back(rb::make_string(text->substr(from, saved[0] - from)));
                    from = saved[1];
                    if (saved[1] == saved[0]) ++from;
                    if (from > n) break;
                }
                out.push_back(rb::make_string(text->substr(from)));
                return rb::list_of({build_groups(out)});
            },
            rb::make_sign(
                "split",
                {{"pattern", "std::String"}, {"text", "std::String"}, {"flags", "std::String"}},
                {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_re_test() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto pat = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                if (!pat || !text) return rb::list_of({rb::native_error("re.test requires pattern and text")});
                auto fstr = rb::string_of(rb::para_at(paras, 2));
                int flags = fstr ? re_parse_flags(*fstr) : 0;
                std::string err; auto re = compile(*pat, err, flags);
                if (!re) return rb::list_of({rb::native_error(err)});
                std::vector<int> saved;
                bool ok = re_search(*re, *text, 0, saved, &err);
                if (!err.empty()) return rb::list_of({rb::native_error(err)});
                return rb::list_of({rb::make_boolean(ok)});
            },
            rb::make_sign(
                "test",
                {{"pattern", "std::String"}, {"text", "std::String"}, {"flags", "std::String"}},
                {{"ok", "std::Boolean"}})
        );
    }

    // Build a `Match` object from a std::smatch (or empty when no match).
    // 由 std::smatch 构造 `Match` 对象（无匹配时为空）。
    inline RuntimeObjectPtr build_groups(const std::vector<RuntimeObjectPtr>& items) {
        auto arr = ::stdRT.make("Array");
        auto* cls = dynamic_cast<RuntimeClass*>(arr.get());
        auto& am = cls->get_attributes();
        for (std::size_t i = 0; i < items.size(); ++i) {
            am[rb::elem_key(i)] = items[i];
        }
        rb::set_container_size(am, items.size());
        return arr;
    }

    // ---- dispose(): explicit state reclamation (industrial-audit D5) ----
    // The destructor hook (prototype on_release) calls the same eraser, so a
    // Pattern is also reclaimed automatically when its object is destroyed.
    // dispose()：显式回收状态（工业化审计 D5）。析构钩子会调用同一删除器，
    // 故 Pattern 对象销毁时也会自动回收。
    inline long long pattern_lookup_id(rt_basic::InstanceMap& env) {
        for (const char* key : {"id", "#value"}) {
            auto it = env.find(key);
            if (it != env.end()) {
                auto v = rb::number_of(it->second);
                if (v) return static_cast<long long>(*v);
            }
        }
        return 0;
    }
    inline void erase_pattern(long long id) {
        std::lock_guard<std::recursive_mutex> lk(g_re_mux);
        g_patterns.erase(id);
    }
    inline rt_basic::Callable method_pattern_dispose() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                erase_pattern(pattern_lookup_id(env));
                return rb::empty_result();
            },
            rb::make_sign("dispose", {}, {})
        );
    }

    // Pattern.:=(value) —— receive. Flow `p << r.compile(...)` evaluates the
    // RHS to the compiled Pattern, then publishes it via `=:` which yields a
    // single-element list holding that Pattern's `#value` id capsule (a
    // Number). We copy that id into `p`. We also accept: another Pattern
    // object (`p << q`), or a pattern-string source (`p << "regex"`) that
    // compiles directly inside `p`.
    // Pattern.:=(value) —— 接收。流语句 `p << r.compile(...)` 先把右值求值为
    // 已编译 Pattern，再经 `=:` 公布为「只含该 Pattern 之 #value（id 胶囊，
    // 一个 Number）的单元素列表」，此处将其 id 拷入 `p`。亦接受：另一 Pattern
    // 对象（`p << q`）或直接模式串（`p << "正则"`）在本实例内编译。
    inline rt_basic::Callable method_pattern_receive() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto incoming = rb::para_at(paras, 0);
                if (!incoming || !paras || paras->size() != 1)
                    return rb::empty_result();

                // Case 1: a Number id capsule (the published form of another
                // Pattern) — copy its value straight into id / #value.
                // 情形 1：Number id 胶囊（另一 Pattern 的公布形态）——
                // 直接把其值写入 id / #value。
                if (auto* rc = dynamic_cast<RuntimeClass*>(incoming.get())) {
                    std::string nm =
                        rc->get_prototype() ? rc->get_prototype()->name : "";
                    if (nm == "Number") {
                        if (auto v = rb::number_of(incoming)) {
                            env["id"]     = rb::make_number(*v);
                            env["#value"] = rb::make_number(*v);
                        }
                        return rb::empty_result();
                    }
                }

                // Case 2: another Pattern object (`p << q`) — copy its id.
                // 情形 2：另一 Pattern 对象（`p << q`）——拷贝其 id。
                if (auto* cls = dynamic_cast<RuntimeClass*>(incoming.get())) {
                    auto& senv = cls->get_attributes();
                    auto it = senv.find("id");
                    if (it != senv.end()) {
                        env["id"]     = it->second;
                        env["#value"] = it->second;
                        return rb::empty_result();
                    }
                }

                // Case 3: a pattern-string source (`p << "regex"`) — compile
                // directly inside this instance.
                // 情形 3：模式串源（`p << "正则"`）——在本实例内直接编译。
                if (auto pat = rb::string_of(incoming)) {
                    std::string err;
                    auto re = compile(*pat, err);
                    if (re) {
                        long long id = 0;
                        { std::lock_guard<std::recursive_mutex> lk(g_re_mux);
                          id = ++g_re_id; g_patterns[id] = re; }
                        env["id"]     = rb::make_number(static_cast<double>(id));
                        env["#value"] = rb::make_number(static_cast<double>(id));
                    }
                }
                return rb::empty_result();
            },
            rb::make_sign(":=",
                {{"value", "std::Object"}}, {})
        );
    }

    // `Match` receive: `-(re::Match m) << r.match(...)` rebinds `m` by copying
    // all fields of the incoming Match into this variable's attribute map.
    // `Match` 接收：把传入 Match 的全部字段拷贝进本变量的属性表，使
    // `-(re::Match m) << r.match(...)` 这一习惯写法可用。
    inline rt_basic::Callable method_match_receive() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto incoming = rb::para_at(paras, 0);
                if (!incoming || !paras || paras->size() != 1)
                    return rb::empty_result();
                auto* cls = dynamic_cast<RuntimeClass*>(incoming.get());
                if (!cls) return rb::empty_result();
                auto& senv = cls->get_attributes();
                const char* fields[] = {"matched", "text", "groups", "start", "end"};
                for (const char* f : fields) {
                    auto it = senv.find(f);
                    if (it != senv.end()) env[f] = it->second;
                }
                return rb::empty_result();
            },
            rb::make_sign(":=",
                {{"value", "std::Object"}}, {})
        );
    }

    // `Match` publish: `m << r.match(...)` needs the real result to cross the
    // flow boundary. Publish a clone of this Match (the base Object `=:` would
    // publish `#value`, which a Match has none of, yielding an empty publish).
    // `Match` 公布：`m << r.match(...)` 需要让真实结果跨越流边界。公布本
    // Match 的克隆（基类 Object 的 `=:` 会公布 `#value`，而 Match 没有该胶囊，
    // 将导致空公布）。
    inline rt_basic::Callable method_match_publish() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                return rb::list_of({clone_match_from_env(env)});
            },
            rb::make_sign("=:", {}, {{"result", "re::Match"}})
        );
    }

    // ---- registration / 登记 ----
    inline void init_re_stdlib() {
        // Pattern / 编译后的正则
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            proto->set_method("match",    method_pattern_match());
            proto->set_method("search",   method_pattern_search());
            proto->set_method("findall",  method_pattern_findall());
            proto->set_method("replace",  method_pattern_replace());
            proto->set_method("replace_fn", method_pattern_replace_fn());
            proto->set_method("split",    method_pattern_split());
            proto->set_method("test",     method_pattern_test());
            proto->set_method(":=",       method_pattern_receive());
            proto->set_method("dispose",  method_pattern_dispose());
            // NOTE: do NOT install an `on_release` hook here. A flow
            // `p << r.compile(...)` publishes the temporary Pattern returned by
            // compile(); its id is copied into `p`, but the temporary is then
            // destroyed and any destructor hook would erase the SHARED cache
            // entry, leaving `p` pointing at a dead id. The regex cache is
            // reclaimed explicitly via dispose() (and at process end), which is
            // enough for the industrialization audit's "state is reclaimable"
            // requirement without breaking the publish/receive dance.
            // 注意：此处不要安装 on_release 钩子。流语句 `p << r.compile(...)`
            // 会把 compile() 返回的临时 Pattern 之 id 拷入 `p`，随后该临时对象
            // 被销毁；任何析构钩子都会抹掉这个被共享的缓存项，使 `p` 指向已失效
            // 的 id。正则缓存经由显式 dispose()（以及进程结束时）回收即可，足以
            // 满足工业化审计“状态可回收”的要求，又不会破坏公布/接收机制。
            runtime::Prototypes p; p.regcls("Pattern", proto); ::stdRT.add_protos(p);
        }
        // Match (result object) / 匹配结果
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            proto->set_attribute("matched", rb::make_boolean(false));
            proto->set_attribute("text",    rb::make_string(""));
            // NOTE: do NOT set a default `groups`/`start`/`end` here. The
            // prototype's default attributes are COPIED by shared_ptr value, so
            // a mutable container set as a default would be shared by every Match
            // instance. make_match() and the := receive each assign a fresh
            // group list, so no default is needed.
            // 注意：此处不要设默认 groups/start/end。原型默认属性按 shared_ptr
            // 值拷贝，设为可变的容器默认会被每个 Match 实例共享。make_match()
            // 与 := 接收各自赋予全新的组列表，故无需默认值。
            proto->set_attribute("start",   rb::make_number(0, true));
            proto->set_attribute("end",     rb::make_number(0, true));
            proto->set_method("=:",         method_match_publish());
            proto->set_method(":=",         method_match_receive());
            runtime::Prototypes p; p.regcls("Match", proto); ::stdRT.add_protos(p);
        }
        // re (static convenience) / 静态便捷入口
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            proto->set_method("compile",  method_re_compile());
            proto->set_method("escape",   method_re_escape());
            proto->set_method("match",    method_re_match());
            proto->set_method("search",   method_re_search());
            proto->set_method("findall",  method_re_findall());
            proto->set_method("replace",  method_re_replace());
            proto->set_method("replace_fn", method_re_replace_fn());
            proto->set_method("split",    method_re_split());
            proto->set_method("test",     method_re_test());
            runtime::Prototypes p; p.regcls("Re", proto); ::stdRT.add_protos(p);
        }
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("re", &init_re_stdlib), true);

} // namespace rt_lib_re
