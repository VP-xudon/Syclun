# Home
## Synth-OOP Wiki · Synth-OOP 维基

> 这是 **Syclun**（Synth-OOP 语言的解释器）的**完整维基式介绍**：从「为什么这么怪」到「怎么写、怎么用库、怎么贡献、怎么打包」，全部拆成分层、好读的页面。
> This is the **complete, wiki-style introduction** to **Syclun** (the interpreter for the Synth-OOP language): from *why it's so weird* to *how to write it, use the libraries, contribute, and package a release* — all split into layered, easy-to-read pages.

**Syclun** 是 **Synth-OOP** 这门 **结构化对象计算（SOC）** 语言的从零实现、跨平台、树遍历 C++23 解释器。它用「真的跑起来」来证明这个范式，而不是说说而已。
**Syclun** is a from-scratch, cross-platform, tree-walking C++23 interpreter for **Synth-OOP**, a **Structured Object Computation (SOC)** language — built to *prove* the paradigm by running it.

---

## 一分钟上手 / One-minute taste

下载 [Release](https://github.com/VP-xudon/Syclun/releases) 里的预编译包，或一行构建：
Grab a prebuilt package from [Releases](https://github.com/VP-xudon/Syclun/releases), or build in one line:

```bash
bash build.sh
./build/synth examples/hello.syn
```

```text
&io;
$Program {
    @:: << [{
        io::out.push_line("Hello, Synth-OOP!");
    }];
}
```

更多请直接看 👉 [快速上手 / Quick Start](Quick-Start)。
For more, jump straight to [Quick Start](Quick-Start).

---

## 本维基的页面 / Pages in this wiki

| 页面 / Page | 讲什么 / What it covers |
|---|---|
| [设计理念 / Design Philosophy](Design-Philosophy) | 为什么「没有运算符」「对象是活的」——每条怪都是哲学立场 |
| [快速上手 / Quick Start](Quick-Start) | 下载 / 构建 / 跑 hello / 感受「流」 |
| [基本语法 / Basic Syntax](Basic-Syntax) | 程序结构、实例化、流、行为、方法、类、控制流、约束、运行期注入、错误 |
| [标准库总览 / Standard Libraries](Standard-Libraries) | `&module;` 导入机制、模块清单、预置对象 |
| [标准库对象使用 / Using StdLib Objects](Standard-Library-Objects) | 每个模块的用法 + 可运行示例（io / maths / file / system / structs / re / hash / async / assert / sugar） |
| [贡献指南 / Contributing](Contributing) | 两条硬规则、PR 格式、DCO 签署、测试与拒收清单 |
| [打包与发布 / Packaging](Packaging) | 从源码构建、一键打包五平台、CI 发布、VS Code 插件 |
| [常见问题 / FAQ](FAQ) | 新手最常问的「为什么」「怎么装」「怎么贡献」 |

---

## 一句话记住它 / One line to remember

> 没有运算符、变量是接口不是盒子、对象是活的、库交付的是能用的对象、错误读起来像编译器。
> **No operators. Variables are interfaces, not boxes. Objects are alive. Libraries ship working objects. Errors read like a compiler's.**

想先被「震」一下？去 [设计理念 / Design Philosophy](Design-Philosophy)。
Want to be surprised first? Go to [Design Philosophy](Design-Philosophy).
