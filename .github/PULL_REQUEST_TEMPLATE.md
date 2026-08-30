<!--
Title format / 标题格式:  [scope][type] short imperative summary
  scopes: lexer parser ast runtime builtin interpreter errors cli
          libs syntax verify examples docs build editor
  types : fix add remove refactor perf docs revert
  e.g.   [interpreter][fix] keep constructor arguments in derived ClsProto

Before opening / 提交前请读: CONTRIBUTING.md
Two rules / 两条硬规则:
  (1) after 1.0.0 the syntax is effectively frozen — a syntax change needs a
      necessity argument, not a preference.
      1.0.0 之后语法基本冻结——语法改动需要「必要性」论证，而非偏好。
  (2) do not invent a new library name if the functionality fits an existing
      module; such a PR is sent back.
      若功能可归入现有库，不要新开库名；此类 PR 会被打回。
-->

## Problem / 问题

<!-- What is wrong or missing? Link the issue. For a bug: how to reproduce it on
     a clean checkout? / 哪里错了或缺了什么？关联 issue。若是缺陷：如何在全新
     检出上复现？ -->

## Done List / 改动清单

1.
2.

## Test Result and Ways / 测试结果与测试方式

| Suite | Before | After |
|-------|--------|-------|
| assert_lexer | 83/83 | |
| assert_parser | 64/64 | |
| assert_runtimes | 143/143 | |
| `examples/` + `verify/philosophy/` | all pass | |

<!-- Commands you ran / 你执行的命令：

```bash
bash build.sh
./build/assert_lexer.exe && ./build/assert_parser.exe && ./build/assert_runtimes.exe
for f in examples/*.syn; do ./build/synth.exe "$f"; done
```
-->

## Compatibility / 兼容性影响

- [ ] none / 无
- [ ] breaks existing `.syn` programs — described below / 破坏既有 `.syn` 程序，见下
- [ ] changes user-visible messages — described below / 改变用户可见输出，见下
- [ ] needs a documentation update — which file? / 需同步文档，哪个文件？

## Self-check / 提交自检

- [ ] `bash build.sh` passes / 构建通过
- [ ] all three suites pass; any count change is explained above / 三个套件通过，用例数变化已在上面说明
- [ ] new behaviour is covered by a test or a `verify/philosophy/*.syn` case / 新行为已有测试或哲学用例覆盖
- [ ] every user-visible string is English and **pure ASCII** / 用户可见字符串均为英文且**纯 ASCII**
- [ ] new source files carry the 3-line licence header / 新源文件带三行许可证头
- [ ] docs updated where relevant / 相关文档已更新
- [ ] one PR = one topic / 一个 PR 一个主题
- [ ] every commit is signed off — `git commit -s` (DCO) / 每个提交都已签名（DCO）
- [ ] I license this contribution under GPL-3.0-or-later / 我以 GPL-3.0-or-later 授权本贡献

Date: YYYY.MM.DD
Developer:
