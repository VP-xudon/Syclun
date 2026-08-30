# Contributing to Syclun
# 为 Syclun 做贡献

Thanks for being here. Syclun is the reference interpreter for the
**Synth-OOP** language and, at the same time, a vehicle for verifying that
language's design philosophy — so a contribution has to satisfy two things at
once: it must be *correct code* and it must keep the *philosophy* intact.

感谢你的到来。Syclun 既是 **Synth-OOP** 语言的参考解释器，也是验证该语言设计
哲学的载体——所以一份贡献要同时满足两件事：它是**正确的代码**，并且它让**哲学**
保持不变。

> **Note / 提示：** this file is the short, practical version. The language
> itself is specified in [`doc/`](./doc); standard-library details live in
> [`doc/Syclun标准库参考.md`](./doc/Syclun标准库参考.md).
> 本文件是简明实用的贡献指南；语言本身由 [`doc/`](./doc) 规定，标准库细节见
> [`doc/Syclun标准库参考.md`](./doc/Syclun标准库参考.md)。

---

## 0. Two rules that exist to save you a wasted PR
## 0. 两条硬规则，为了让你少做无用功

### Rule 1 — after 1.0.0, the syntax is effectively frozen
### 规则一——1.0.0 之后，语法基本冻结

**Once 1.0.0 ships, a change to the language syntax will not be accepted unless
it is genuinely necessary.** "Genuinely necessary" means one of:

- the specification is self-contradictory or ambiguous;
- the current syntax makes a documented feature impossible to express;
- it fixes a security- or correctness-level hole.

"Nice to have", "more consistent with language X", "shorter to type" — these
will **not** clear the bar. Please be mentally prepared for that: a
well-written, fully-tested syntax PR can still be declined.

**1.0.0 发布后，除非确有十分的必要，否则语言语法的改动不会被接受。**「确有十分的
必要」仅指下列之一：

- 规范自相矛盾或存在歧义；
- 现有语法使某个已写入文档的特性无法表达；
- 修复安全性或正确性层面的漏洞。

「更好看」「和某某语言更一致」「打字更短」——这些都**不够**。请做好心理准备：
一份写得很好、测试齐全的语法 PR 依然可能被拒。

Right now there is no `1.0.0` tag yet, so the door is still open — but the
further a change reaches into the language (rather than into the interpreter),
the earlier you should open an issue and talk about it first.

目前仓库还没有 `1.0.0` 标签，窗口仍然开着——但改动越触及**语言**（而非解释器
实现），就越应该先开 issue 讨论。

### Rule 2 — do not invent a new library name
### 规则二——别轻易新开库名

**Before adding a new standard-library module, seriously check whether the
functionality belongs in an existing module.** If it fits — merge it in.
A PR that opens a needless new module will be sent back to be reworked.

**新增标准库模块前，请认真确认新功能是否真的有必要单开一个库名。** 若能归入
现有库，就归入现有库。不必要的开库会被打回重做。

Ask yourself / 先问自己：

| Question | 问题 |
|----------|------|
| Does an existing module already own this domain? | 现有库是否已覆盖该领域？ |
| Would a user look for this in `maths` / `structs` / `file` / `system` / `re` / `hash` / `io` / `async` / `assert` first? | 用户会不会先去上述库里找？ |
| Is it more than ~3 methods of genuinely new domain? | 是否真的超过约 3 个方法、且属于全新领域？ |
| Could it be a method on an existing class instead? | 它能不能直接作为现有类的一个方法？ |

Only if the answer to all four points at "new module" should you open one.
四条都指向「确需新库」时，才开新库。

---

## 1. Before you start / 开始之前

```bash
bash build.sh                      # must succeed / 必须成功
./build/assert_lexer.exe           # 83 / 83
./build/assert_parser.exe          # 64 / 64
./build/assert_runtimes.exe        # 143 / 143
for f in examples/*.syn; do ./build/synth.exe "$f"; done
```

If any of that is red on a clean checkout, **fix your environment first** —
do not start from a broken tree.
若全新检出下就有红灯，**先修环境**，不要从一棵坏树开始。

Then read, in this order / 然后按此顺序阅读：

1. [`README.md`](./README.md) — what Syclun is, and the 30-second tour.
2. [`doc/Synth-OOP语言文档-修正版.md`](./doc/Synth-OOP语言文档-修正版.md) — the
   specification (v1.29). **The spec is the contract**; the interpreter is one
   implementation of it.
3. [`doc/Syclun标准库参考.md`](./doc/Syclun标准库参考.md) — signatures and the
   "how to add a library" checklist.

---

## 2. Where to change what / 改哪在哪

| Path | Change it when… / 何时改它 |
|------|---------------------------|
| `src/lexer.hpp` | tokens, the colon principle, string escapes, `//` comments |
| `src/parser.hpp` | grammar; rejects `void`; top level allows only import + classdef |
| `src/ast_dump.hpp` | AST pretty-printing (debug utility only) |
| `src/runtime.hpp` | object model, capsules, `Callable`, `ClsProto` inheritance |
| `src/builtin.hpp` | native objects (Number/String/Array/…), `arg_type_key` |
| `src/interpreter.hpp` | the evaluator: `exec_behavior` / `exec_vardef` / `exec_flow`, `invoke`, `rebind_method`, recursion guard |
| `src/exception_throw.hpp` | diagnostic layout — **two hard constraints**, see §7 |
| `src/main.cpp` | CLI (`synth <program.syn>`) |
| `lib/<name>.synl` + `lib/cpp/<name>.hpp` (+ `lib/cpp/std_libs.hpp`) | standard libraries |
| `verify/unit/*.cpp` | assertion suites (mechanical regression guards) |
| `verify/philosophy/*.syn` | philosophy cases — one runnable program per claim |
| `examples/*.syn` | the 30-second tour; must stay small and readable |
| `doc/` | specification and standard-library reference |
| `vscode-synth-oop/` | editor extension — **MIT-licensed, separate work** |

---

## 3. PR title format / PR 标题格式

One line, imperative mood, at most ~72 characters:

```
[scope][type] concise summary
```

Examples / 示例：

```
[interpreter][fix] keep constructor arguments in derived ClsProto
[lexer][add] support \e as an escape sequence
[libs][add] Hash.fnv1a
[syntax][remove] the void keyword
[docs][fix] sync Appendix E with the v1.29 semantics
[verify][add] cover method rebind through the flow form
```

**scope** — one of / 取其一：

| scope | covers |
|-------|--------|
| `lexer` `parser` `ast` `runtime` `builtin` `interpreter` `errors` `cli` | one engine module |
| `libs` | everything under `lib/` |
| `syntax` | **a change to the language itself** — expect the strictest review |
| `verify` | `verify/unit` and `verify/philosophy` |
| `examples` | `examples/` |
| `docs` | `doc/` and `README.md` |
| `build` | `build.sh`, toolchain |
| `editor` | `vscode-synth-oop/` |

**type** — one of / 取其一：

| type | means |
|------|-------|
| `fix` | corrects wrong behaviour |
| `add` | new capability |
| `remove` | deletes something (say why it is safe) |
| `refactor` | behaviour unchanged |
| `perf` | faster, same behaviour |
| `docs` | documentation only |
| `revert` | undoes a previous commit |

> A PR touching more than one scope usually wants to be **two PRs**.
> 一个 PR 若涉及多个 scope，通常应该拆成**两个 PR**。

---

## 4. PR body template / PR 正文模板

Copy this into the description (or use the template in
[`.github/PULL_REQUEST_TEMPLATE.md`](./.github/PULL_REQUEST_TEMPLATE.md)):

```markdown
## Problem / 问题

What is wrong, or what is missing? Link the issue if there is one.
For a bug: how do I reproduce it on a clean checkout?

## Done List / 改动清单

1.
2.

## Test Result and Ways / 测试结果与测试方式

| Suite              | Before | After |
|--------------------|--------|-------|
| assert_lexer       | 83/83  |       |
| assert_parser      | 64/64  |       |
| assert_runtimes    | 143/143|       |
| examples + philosophy | all pass |     |

Commands you ran / 你执行的命令：

## Compatibility / 兼容性影响

- [ ] none / 无
- [ ] breaks existing `.syn` programs — described below / 破坏既有程序，见下
- [ ] changes user-visible messages — described below / 改变用户可见输出，见下
- [ ] needs a documentation update — which file? / 需同步文档，哪个文件？

## Self-check / 提交自检

- [ ] `bash build.sh` passes / 构建通过
- [ ] all three suites pass (any count change is explained above) / 三个套件通过
- [ ] new behaviour is covered by a test or a `verify/philosophy/*.syn` case
- [ ] every user-visible string is English and **pure ASCII**
- [ ] new source files carry the 3-line licence header
- [ ] docs updated (README / 语言文档 / Syclun标准库参考.md) where relevant
- [ ] one PR = one topic
- [ ] every commit is signed off — `git commit -s` (DCO, see §10.1)
- [ ] I license this contribution under GPL-3.0-or-later (see §10)

Date: 2026.08.29
Developer: YourName
```

Why the fields ended up like that / 为什么这样安排：

- **The title stays short.** Putting `Problem` / `Done List` / `Test Result` in
  the *title* makes it unreadable in the PR list and gets truncated in emails
  and notifications; they belong in the body, where they are searchable.
  **标题保持简短。**把「问题 / 改动清单 / 测试结果」都塞进标题会让 PR 列表无法
  阅读，也会在邮件与通知里被截断；它们属于正文，且正文中可被搜索。
- **Date and developer sit at the bottom**, where your draft put them — git
  already records both, so they are a courtesy, not a requirement.
  **日期与开发者名放在末尾**，按你原本的位置。git 本身已记录这两项，故属礼节性
  信息而非硬性要求。
- **`Compatibility` is the field that decides most reviews.** If a change can
  break existing `.syn` programs, say so in the first line.
  **`兼容性影响` 是决定多数评审结论的字段。**若改动可能破坏既有 `.syn` 程序，
  请在第一行就说清楚。

---

## 5. Rejected on sight / 当场拒收清单

A PR will be sent back immediately if it:
出现下列任一情况，PR 会被立即打回：

1. **Changes the syntax with no necessity argument** (Rule 1). /
   **改语法但拿不出必要性论证**（规则一）。
2. **Opens a new library module that fits an existing one** (Rule 2). /
   **新开一个明明可以并入现有库的模块**（规则二）。
3. **Turns a suite red**, or lowers a count without explaining why in
   `Test Result`. / **让任一测试套件变红**，或降低了用例数却未在「测试结果」中说明。
4. **Puts non-ASCII characters in user-visible output.** This includes
   box-drawing (`╭ ╰ │ ─`) and symbols like `⚠` — on a GBK Windows console they
   render as mojibake. / **在用户可见输出中出现非 ASCII 字符**，含制表符
   （`╭ ╰ │ ─`）与 `⚠` 等符号——在 GBK 控制台上会变成乱码。
5. **Bundles several unrelated topics** into one diff. /
   **把多件不相干的事塞进一个 diff**。
6. **Adds a third-party dependency.** Syclun depends on the C++23 standard
   library and nothing else, and that is deliberate. /
   **引入第三方依赖。**Syclun 只依赖 C++23 标准库，这是刻意为之。
7. **Misses the licence header** on a new file, **or** edits an English error
   message in `builtin.hpp` without updating the substring match in
   `verify/unit/assert_runtimes.cpp` (that silently voids 143 assertions). /
   **新文件缺许可证头**，**或**改了 `builtin.hpp` 的英文报错却未同步
   `verify/unit/assert_runtimes.cpp` 的子串匹配（那会让 143 项断言静默失效）。
8. **Commits build output** — `build/`, `*.exe`, `_cli_out.txt`, or editor junk. /
   **提交了构建产物**——`build/`、`*.exe`、`_cli_out.txt` 或编辑器垃圾文件。
9. **Resurrects something retired**: the poison-water model, `_case`, or the
   `void` keyword. / **复活已退役的东西**：毒水模型、`_case`、`void` 关键字。
10. **Breaks the `lib/cpp/` layout** — C++ backends live in `lib/cpp/`, never
    beside the `.synl`, never folded into `builtin.hpp`. /
    **破坏 `lib/cpp/` 布局约定**——C++ 底层必须位于 `lib/cpp/`，不得与 `.synl`
    并列，也不得并入 `builtin.hpp`。
11. **Is a bug report wearing a PR costume** — no reproduction steps, no
    failing test. / **是伪装成 PR 的 bug 报告**——没有复现步骤，也没有失败的测试。
12. **Is a pure reformat / line-ending churn** that was not agreed first; it
    buries real changes in the diff. This repo has mixed LF/CRLF files on
    purpose. / **未经事先同意的纯格式化 / 换行符大改**；它会把真正的改动埋进
    diff。本仓库**故意**混用 LF 与 CRLF。
13. **Has commits without a `Signed-off-by` trailer** (DCO, §10.1). Fix it with
    `git rebase --signoff origin/main`. / **提交缺少 `Signed-off-by` 尾注**
    （DCO，§10.1）。用 `git rebase --signoff origin/main` 补签即可。

> Rejected ≠ rejected forever. Most of these are a five-minute fix and the PR
> can be reopened.
> 打回 ≠ 永远拒绝。上述多数情况只需五分钟即可修正，之后可重新开启 PR。

---

## 6. Testing / 测试要求

```bash
bash build.sh
./build/assert_lexer.exe && ./build/assert_parser.exe && ./build/assert_runtimes.exe
for f in examples/*.syn;        do ./build/synth.exe "$f"; done
for f in verify/philosophy/*.syn; do ./build/synth.exe "$f"; done
```

- Every behaviour change needs a test: an assertion in `verify/unit/`, or a
  runnable `verify/philosophy/*.syn` case that prints what it claims.
  每处行为改动都需要测试：`verify/unit/` 中的断言，或一个可运行、能打印其主张的
  `verify/philosophy/*.syn` 用例。
- Bug fixes: add the case that used to fail. Bug 修复：请补上原先会失败的用例。
- Keep the counts moving **up**, not down. 用例数应当**上升**，不应下降。
- Caveat / 注意：a demo that reads stdin can leave `synth.exe` locked if the
  process does not exit; clear it with `tasklist` / `taskkill` before rebuilding.
  读取 stdin 的示例若进程未退出会锁住 `synth.exe`；重新构建前用 `tasklist` /
  `taskkill` 清场。

---

## 7. Code conventions / 代码约定

- **Bilingual comments** — English line first, Chinese line below.
  **注释双语**——英文在上，中文在下。
- **User-visible output and errors: English, pure ASCII.** Error frames use
  `+`, `-`, `|`; never box-drawing, never `⚠`.
  **用户可见输出与报错：英文、纯 ASCII。**错误框用 `+` `-` `|`；禁用制表符与 `⚠`。
- **Diagnostics layout — two hard constraints** in `exception_throw.hpp`:
  ① the caret line's prefix must be *exactly* as wide as the source line's
  prefix; ② in-line highlighting and the caret underneath must always cover the
  same characters. Touch `emit()` and you own both.
  **诊断排版的两条硬约束**（`exception_throw.hpp`）：① 插入符行前缀宽度必须
  **严格等于**源码行前缀宽度；② 行内高亮与行下插入符必须始终覆盖相同字符。动了
  `emit()` 就要对这两条负责。
- **Keep `-Wl,--stack,33554432` in `build.sh`** — the recursion guard (limit
  1000) needs a 32 MB stack to fire cleanly instead of overflowing.
  **保留 `build.sh` 中的 `-Wl,--stack,33554432`**——递归护栏（上限 1000）需要
  32MB 栈才能干净触发而非溢出。
- **New files** under `src/` and `lib/cpp/` carry the 3-line header:
  **`src/` 与 `lib/cpp/` 下的新文件**带三行声明头：
  ```cpp
  // Copyright (C) 2026 VP_xudon
  // SPDX-License-Identifier: GPL-3.0-or-later
  // See LICENSE in the project root for the full license text.
  ```
- **Adding a standard library**: follow the checklist in
  [`doc/Syclun标准库参考.md`](./doc/Syclun标准库参考.md) exactly.
  **新增标准库**：严格照
  [`doc/Syclun标准库参考.md`](./doc/Syclun标准库参考.md) 中的清单执行。
- **Docs travel with code.** Change a behaviour → update the README and/or the
  specification in the same PR.
  **文档随代码走。**改了行为就在同一个 PR 里更新 README 与/或语言规范。

---

## 8. Review process / 评审流程

1. **Open an issue first** for anything that touches syntax, a new library, or
   the object model. A five-minute conversation saves a wasted weekend.
   凡涉及语法、新库或对象模型，**请先开 issue**。五分钟的讨论能省掉一个周末。
2. **One PR, one topic.** Small PRs get reviewed; large ones get postponed.
   **一个 PR 一个主题。**小 PR 会被评审，大 PR 会被搁置。
3. **Expect questions.** Review comments are about the code, not about you.
   **会有提问。**评审意见针对代码，不针对你。
4. **A rejection comes with a reason**, and usually with what would make it
   acceptable. **拒收会附带理由**，通常还会说明怎样就能被接受。
5. Merges are squashed; write the final message as if it were the only commit.
   合并采用 squash；请把最终提交信息当成唯一一次提交来写。

---

## 9. Commit messages / 提交信息

Same `[scope][type]` prefix as the PR title, then a short imperative summary:

```
[interpreter][fix] keep constructor arguments in derived ClsProto

ClsProto's copy constructor inherited Object's no-op `::`, so
`-(std::Number(5) n)` silently produced 0. Derived types no longer inherit it.

Signed-off-by: Your Name <you@example.com>
```

Wrap the body at ~72 characters; explain **why**, not just what.
正文约 72 字符换行；说明**为什么**，而不只是做了什么。

**Every commit must carry a `Signed-off-by` trailer** (DCO — see §10). Get it
for free with `git commit -s`.
**每个提交都必须带 `Signed-off-by` 尾注**（DCO，见 §10）。用 `git commit -s`
即可自动添加。

---

## 10. Licensing of your contribution / 你的贡献如何授权

Syclun is licensed **GPL-3.0-or-later** ([`LICENSE`](./LICENSE)). By opening a
PR you agree that your contribution is licensed under the same terms, and that
you have the right to license it.

Syclun 采用 **GPL-3.0-or-later** 授权（[`LICENSE`](./LICENSE)）。开启 PR 即表示
你同意你的贡献以相同条款授权，且你有权如此授权。

### 10.1 DCO: sign off your commits / DCO：为提交签名

This project uses the **Developer Certificate of Origin** instead of a CLA. It
is one line per commit, and it costs you nothing to add:
本项目采用 **DCO（开发者原创证书）** 而非 CLA。每个提交加一行即可，代价为零：

```bash
git config user.name  "Your Name"          # real name / 真实姓名
git config user.email "you@example.com"    # real address / 真实邮箱

git commit -s                              # sign off this commit / 签署本次提交
git commit --amend -s                      # sign off the last one / 补签最后一个
git rebase --signoff origin/main           # sign off a whole branch / 补签整条分支
```

That produces / 效果如下：

```
Signed-off-by: Your Name <you@example.com>
```

The full, verbatim certificate is in [`DCO.md`](./DCO.md); the official text
lives at <https://developercertificate.org/>. In short, signing off means you
wrote the code (or otherwise have the right to submit it under this project's
licence), and you accept that the contribution and your sign-off are public and
kept indefinitely.
完整原文见 [`DCO.md`](./DCO.md)，官方文本见
<https://developercertificate.org/>。简而言之，签名表示这段代码是你写的（或你
有权以本项目许可证提交），且你接受该贡献与你的签名将被公开并永久留存。

**A PR whose commits are not signed off will not be merged.** If the DCO check
is red, `git rebase --signoff origin/main && git push --force-with-lease`
usually fixes it in one shot.
**提交未签名的 PR 不会被合并。**若 DCO 检查是红的，通常一条
`git rebase --signoff origin/main && git push --force-with-lease` 就能解决。

> Use your **real name and a real address** — the sign-off is a legal
> attestation, not a handle.
> 请使用**真实姓名与真实邮箱**——签名是法律意义上的声明，不是昵称。

Two more things worth knowing / 另外两点值得知道：

- **Programs you write in Synth-OOP are yours.** `.syn` / `.synl` / `.syni`
  files and their output are independent works, not derivative works of the
  interpreter. Contributing an example under `examples/` or
  `verify/philosophy/` is still a contribution to *this repository* (GPL), but
  it does not put anyone else's Synth-OOP programs under the GPL.
  **你用 Synth-OOP 写的程序归你自己。**`.syn` / `.synl` / `.syni` 文件及其输出
  是独立作品，不是解释器的演绎作品。向 `examples/` 或 `verify/philosophy/`
  贡献示例仍是对**本仓库**的贡献（GPL），但这不会把其他人的 Synth-OOP 程序置于
  GPL 之下。
- **`vscode-synth-oop/` is MIT**, a separate and independently distributed
  work. Contributions there are governed by its own `LICENSE`.
  **`vscode-synth-oop/` 采用 MIT**，是独立分发的作品，受其自带 `LICENSE` 约束。

If you would rather not sign away anything, say so in the PR — we can talk
about it before merging, not after.
若你不想做此授权，请在 PR 中说明——我们会在合并**之前**谈，而不是之后。

---

## 11. Quick reference / 速查

```
scopes : lexer parser ast runtime builtin interpreter errors cli
         libs syntax verify examples docs build editor
types  : fix add remove refactor perf docs revert
title  : [scope][type] short imperative summary
body   : Problem · Done List · Test Result and Ways · Compatibility · Self-check
         Date: YYYY.MM.DD
         Developer: name
commit : git commit -s        # every commit needs Signed-off-by (DCO)
rules  : (1) syntax is frozen after 1.0.0   (2) don't invent a library name
```

Questions are welcome — an issue that asks "should I do this?" is a
contribution too.
欢迎提问——一个问「我该不该做这件事」的 issue 本身也是一种贡献。
