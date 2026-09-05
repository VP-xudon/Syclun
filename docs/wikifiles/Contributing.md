# Contributing
## 贡献指南

> Syclun 既是 Synth-OOP 的参考解释器，也是验证其设计哲学的载体——一份贡献要同时满足：是**正确的代码**，且让**哲学**保持不变。
> Syclun is both the reference interpreter and a philosophy-verification vehicle — a contribution must be *correct code* and keep the *philosophy* intact.

---

## 0. 两条硬规则 / Two hard rules

### 规则一：1.0.0 之后语法基本冻结 / Rule 1: syntax frozen after 1.0.0
**1.0.0 发布后，除非确有十分的必要，否则语法改动不会被接受。**「确有十分的必要」仅指：规范自相矛盾、现有语法使已文档化的特性无法表达、或修复安全/正确性漏洞。「更好看」「和某语言更一致」都不够。
**After 1.0.0, a syntax change is accepted only with genuine necessity** (self-contradictory spec, an documented feature that can't be expressed, or a security/correctness hole). "Prettier" or "more consistent with X" is not enough.

> 目前**没有 1.0.0 标签，窗口仍开着**——但越触及「语言」本身（而非解释器实现），越应先开 issue 讨论。
> There is **no `1.0.0` tag yet, so the window is open** — but the more a change touches the *language* (vs the interpreter), the sooner you should open an issue first.

### 规则二：别轻易新开库名 / Rule 2: don't invent a library name
新增标准库前，先确认功能能否归入现有模块（`maths`/`structs`/`file`/`system`/`re`/`hash`/`io`/`async`/`assert`/`sugar`）。能归入就归入；不必要的开库会被打回。
Before adding a module, check whether it fits an existing one. Unnecessary new modules are sent back.

---

## 1. 开干之前 / Before you start

```bash
bash build.sh
./build/assert_lexer && ./build/assert_parser && ./build/assert_runtimes
for f in examples/*.syn; do ./build/synth "$f"; done
```

全新检出下就有红灯？**先修环境**，不要从坏树开始。
If a clean checkout is red, **fix your environment first**.

建议阅读顺序 / suggested reading order:
1. [`README.md`](https://github.com/VP-xudon/Syclun/blob/main/README.md)
2. [基本语法 / Basic Syntax](Basic-Syntax) 与语言规范 [`Synth-OOP语言文档-修正版.md`](https://github.com/VP-xudon/Syclun/blob/main/doc/Synth-OOP语言文档-修正版.md)
3. [标准库总览 / Standard Libraries](Standard-Libraries)

---

## 2. PR 标题格式 / PR title format

```
[scope][type] concise summary
```

- **scope**: `lexer` `parser` `ast` `runtime` `builtin` `interpreter` `errors` `cli` `libs` `syntax` `verify` `examples` `docs` `build` `editor`
- **type**: `fix` `add` `remove` `refactor` `perf` `docs` `revert`

```
[interpreter][fix] keep constructor arguments in derived ClsProto
[libs][add] Hash.fnv1a
[syntax][remove] the void keyword
```

> 一个 PR 涉及多个 scope，通常应拆成两个 PR。
> A PR touching multiple scopes usually wants to be two PRs.

---

## 3. PR 正文模板 / PR body template

```markdown
## Problem / 问题
## Done List / 改动清单
## Test Result and Ways / 测试结果与方式
| Suite            | Before | After |
|------------------|--------|-------|
| assert_lexer     | 83/83  |       |
| assert_parser    | 71/71  |       |
| assert_runtimes  | 158/158|       |
## Compatibility / 兼容性影响
- [ ] none
## Self-check / 提交自检
- [ ] bash build.sh 通过
- [ ] 三个套件通过（计数变动已说明）
- [ ] 新行为有测试或 verify/philosophy/*.syn 用例
- [ ] 用户可见字符串为英文纯 ASCII
- [ ] 新文件带三行许可证头
- [ ] 文档已同步
- [ ] 每提交带 Signed-off-by（DCO）
```

---

## 4. 当场拒收清单（节选）/ Rejected on sight (excerpt)

1. 改语法但无必要性论证（规则一）
2. 新开可并入现有库的模块（规则二）
3. 让任一测试套件变红，或降低计数却不说明
4. 用户可见输出出现非 ASCII（含 `⚠`、制表符）——GBK 控制台会乱码
5. 把多件不相干的事塞进一个 diff
6. 引入第三方依赖（只依赖 C++23 标准库）
7. 新文件缺许可证头；或改了 `builtin.hpp` 的英文报错却没同步 `assert_runtimes` 的子串匹配
8. 提交构建产物（`build/`、`*.exe`）
9. 复活已退役的：毒水模型、`_case`、`void`
10. 破坏 `lib/cpp/` 布局（C++ 底层必须放 `lib/cpp/`，不得与 `.synl` 并列或并入 `builtin.hpp`）
11. 提交缺 `Signed-off-by`（DCO）
12. 伪装成 PR 的 bug 报告（无复现、无失败测试）

> 打回 ≠ 永远拒绝。多数只需五分钟即可修正重开。
> Rejected ≠ rejected forever. Most are a five-minute fix.

---

## 5. 测试要求 / Testing

- 每处行为改动都要有测试：要么 `verify/unit/` 里的断言，要么一个能打印其主张的 `verify/philosophy/*.syn` 用例。
- Bug 修复：补上「原先会失败」的用例。
- 用例数应当**上升**，不应下降。

---

## 6. 代码约定 / Code conventions

- 注释双语（英文在上，中文在下）。
- 用户可见输出与报错：**英文、纯 ASCII**；错误框用 `+` `-` `|`，禁用制表符与 `⚠`。
- 保留 `build.sh` 中的 `-Wl,--stack,33554432`（递归护栏需要 32MB 栈）。
- `src/` 与 `lib/cpp/` 下新文件带三行许可证头：
  ```cpp
  // Copyright (C) 2026 VP_xudon
  // SPDX-License-Identifier: GPL-3.0-or-later
  // See LICENSE in the project root for the full license text.
  ```
- **文档随代码走**：改了行为就在同一 PR 更新 README 与/或语言规范。

---

## 7. DCO：为提交签名 / Sign off your commits

本项目用 **DCO（开发者原创证书）** 而非 CLA——每个提交加一行即可：
This project uses the **Developer Certificate of Origin** instead of a CLA — one line per commit:

```bash
git config user.name  "Your Name"
git config user.email "you@example.com"
git commit -s                       # 签署本次提交
git rebase --signoff origin/main   # 补签整条分支
```

```
Signed-off-by: Your Name <you@example.com>
```

- 提交未签名的 PR **不会被合并**。用真实姓名与真实邮箱（签名是法律意义上的声明）。
- 你用 Synth-OOP **编写**的程序（`.syn`/`.synl`/`.syni`）是**独立作品**，不是本解释器的演绎作品。

---

## 8. 速查 / Quick reference

```
scopes : lexer parser ast runtime builtin interpreter errors cli
         libs syntax verify examples docs build editor
types  : fix add remove refactor perf docs revert
title  : [scope][type] short imperative summary
commit : git commit -s        # 每提交须 Signed-off-by (DCO)
rules  : (1) 1.0.0 后语法冻结   (2) 别新开库名
```

有问题尽管在 issue 里问「我该不该做这件事」——那本身也是一种贡献。
Questions welcome — an issue asking "should I do this?" is a contribution too.

👉 想自己打一个分发包：[打包与发布 / Packaging](Packaging)
