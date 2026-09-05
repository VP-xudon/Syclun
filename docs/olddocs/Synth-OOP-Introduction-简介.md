# Synth-OOP 正式介绍 / A Formal Introduction to Synth-OOP

> 本文件是 README 的「正式介绍」入口。想先感受这门语言有多野，先看
> [`README.md`](../README.md) 顶部的「这门语言有多野？」；想读权威规范，去
> [`Synth-OOP语言文档-修正版.md`](./Synth-OOP语言文档-修正版.md)。
> This file is the "formal introduction" entry point linked from the README.
> For a punchy taste first, see "How wild is this language?" at the top of
> [`README.md`](../README.md); for the authoritative spec, see
> [`Synth-OOP语言文档-修正版.md`](./Synth-OOP语言文档-修正版.md).

---

## 这是什么 / What this is

**Synth-OOP** 是一门 **结构化对象计算（Structured Object Computation, SOC）** 语言；
**Syclun** 是它的一个从零实现、跨平台、树遍历的 C++23 解释器——用「真的跑起来」而非
「说说而已」来证明这个范式。

**Synth-OOP** is a **Structured Object Computation (SOC)** language, and **Syclun** is
one from-scratch, cross-platform, tree-walking C++23 interpreter for it — built to
*prove* the paradigm by running it, not just describing it.

> **Structured Object Computation (SOC)** is a programming paradigm in which
> computation is modeled as the interaction and evolution of stateful Objects
> through explicit Behaviors and Flows, with Classes, Constraints, and Effects
> providing structure and guarantees over that computation.
>
> **结构化对象计算（SOC）**是一种编程范式：计算被建模为**有状态对象**经由显式
> **行为**与**流**的交互与演化，由**类**、**约束**与**效应**为该计算提供结构与保证。

---

## 设计理念：刻意的怪异 / Design philosophy: weird on purpose

多数语言要把你变得正常。Synth-OOP 拥抱自己的怪异——下面每一条「怪」都是一条
**哲学立场**，且都由解释器强制执行：
Most languages normalize you. Synth-OOP leans into its strangeness — every
oddity below is a *philosophical position*, and each one is enforced by the
interpreter:

- **没有运算符。** `io::out << "hi"` 不是什么语法糖——它就是值移动的**唯一**方式：
  对象之间显式的流。连算术都是方法：`a.+(b)`。
  **There are no operators.** `io::out << "hi"` is not syntax sugar for anything —
  it is *the* way values move: explicit flows between objects. Even arithmetic
  is a method: `a.+(b)`.

- **变量不是盒子。** 变量只是对象的**访问接口**。`.=` 与 `<<` 不会把名字改指向
  别处——它们重塑对象自身。语言也没有类型：声明处的类名是你借以塑造对象的模板，
  而非对象终身佩戴的标签。
  **Variables are not boxes.** A variable is just an *access interface* onto an
  object. `.=` and `<<` do not swap what a name points to — they reshape the
  object itself. And the language has no types: a class name at declaration is
  the template you build from, not a label the object carries forever.

- **对象是活的。** 运行期给活对象注入方法（`box:@hi << [{ … }];`），给它一个
  只有它自己的方法能碰的私有属性（`box:-(std::Number secret) << 42;`），或用
  `box.#()` 把它永久冻结——单向、只可设置的开关。任何东西都无法移除，冻结的对象
  永不可解冻。
  **Objects are alive.** Add a *method* to a live object at runtime, give it a
  *private* attribute only its own methods can touch, or freeze it forever with
  `box.#()` — a one-way, set-only switch. Nothing can ever be removed, and a
  frozen object can never thaw.

- **代码即代码块，无需仪式。** `[{ … }]` 就是空签名行为——条件分支与回调的自然形态。
  **Code is a block, not a ceremony.** `[{ … }]` is a behavior with an empty
  signature — the natural shape for condition branches and callbacks.

- **库交付的是能用的对象，而不只是类。** 导入 `&io;`，`io::out` / `io::in` 就**存在**，
  生而为常数。`&maths;` 带 `maths::math`。没有工厂、没有仪式——而你的 `.syn` 程序
  **被禁止**声明全局对象：预置对象属于库。
  **The library ships working objects, not just classes.** Import `&io;` and
  `io::out` / `io::in` simply *exist*, const from birth. `&maths;` brings
  `maths::math`. No factories, no ceremony — and your `.syn` program is
  *forbidden* from declaring global objects: presets belong to libraries.

- **错误读起来像编译器。** 故障在源头抛出——绝不以静默的「毒水」值传播——并以类 g++
  的格式报告：带 `^~~~` 插入符的出错行与完整**执行栈**。致命错误退出码为 `1`，
  算术遵循 IEEE 754。
  **Errors read like a compiler's.** A fault is raised at its source — never
  propagated as a silently-poisoned value — and reported g++-style with a
  `^~~~` caret and the full **execution stack**. Fatal errors exit `1`;
  arithmetic honours IEEE 754.

---

## 设计要点 / Design notes

- **无全局作用域。** 程序入口是 `$Program` 类的 `@::`（构造行为），实例化 `$Program`
  即程序的整个生命周期。
  **No global scope.** The program entry is the `$Program` class's `@::`
  (construct) behavior; instantiation of `$Program` *is* the program lifecycle.

- **一切皆对象，无中缀运算符。** `a.+(b)` 是方法调用。值通过**流**（`<<`/`>>`）传递，
  由 `=:` 公布、`:=` 接收。
  **Everything is an object; no infix operators.** `a.+(b)` is a method call.
  Values travel through **flows** (`<<` / `>>`), published via `=:` and received
  via `:=`.

- **零值法则。** 新造实例自带缺省 `#value` 胶囊（`Number`→`0`），故
  `io::out << (make Number)` 输出 `0`。
  **Zero-value law.** A freshly created instance carries a default `#value`
  capsule (e.g. `Number` → `0`), so `io::out << (make Number)` prints `0`.

- **行为即闭包。** 内联行为字面量捕获调用方作用域，分支可读到调用方变量（规范 C.5）。
  **Behaviors are closures.** Inline behavior literals capture the caller's
  scope, so branches read caller variables (spec C.5).

- **运行期注入（v1.31）。** 对象在构造后仍是活的：`obj:@m << [{ … }];` 新增方法，
  `obj:-(T v) << init;` 新增私有属性（仅可初始化、只有对象自己的方法能访问），
  `obj.#()` 永久冻结。向已有名字注入属重复声明错误；重绑常数方法属更改常量错误。
  **Runtime injection (v1.31).** Objects stay alive after construction:
  `obj:@m << [{ … }];` adds a method, `obj:-(T v) << init;` adds a private
  attribute (init-only, reachable only from the object's own methods), and
  `obj.#()` freezes the object permanently.

- **库预置对象（v1.31）。** 库形态（.synl）可创建顶层常数实例（`-(io::OStream! out);`）
  随导入到来；`.syn` 程序自身被禁止声明全局对象。
  **Library preset objects (v1.31).** Library faces (`.synl`) may create top-level
  const instances (`-(io::OStream! out);`) that arrive with the import; `.syn`
  programs are forbidden from declaring global objects themselves.

---

## 标准库是怎么组织的 / How the standard libraries are structured

标准库位于 **`lib/`** 目录，通过导入语句 **`&module;`** 载入。一个标准库的两部分
**刻意分置于两处**：
Standard libraries live in **`lib/`** and are loaded with **`&module;`**. A
library's two halves are kept in **two different places** on purpose:

- **`lib/<name>.synl`** — 面向人类的接口（仅签名、无函数体）。解释器*解析但不编译*它，
  仅用于记录类的公开形态。
  **`lib/<name>.synl`** — the human-readable interface (signatures only, no body).
  The interpreter *parses but does not compile* it; it documents the public shape.

- **`lib/cpp/<name>.hpp`** — 提供真实行为的 C++ 底层，经
  `rt_builtin::register_native_lib(name, init_fn)` 自注册，引擎无需显式指名。所有
  C++ 底层汇总于 **`lib/cpp/std_libs.hpp`**，由解释器仅包含一次。
  **`lib/cpp/<name>.hpp`** — the C++ backend supplying real behavior, self-registering
  via `rt_builtin::register_native_lib(name, init_fn)`. All backends are collected
  in **`lib/cpp/std_libs.hpp`**, included exactly once by the interpreter.

`&` 语句是稳健的：导入不存在的模块（无 `.synl` 且无登记的 C++ 底层）为静默空操作。
库模块一览与详细方法签名见 [`Syclun标准库参考.md`](./Syclun标准库参考.md)。
The `&` statement is robust: importing a non-existent module is a silent no-op.
The module list and detailed signatures live in
[`Syclun标准库参考.md`](./Syclun标准库参考.md).

---

## 接下来 / Where to go next

- 完整语言规范 / Full specification:
  [`Synth-OOP语言文档-修正版.md`](./Synth-OOP语言文档-修正版.md)
  （英文版 / English:
  [`Synth-OOP-Language-Documentation-修正版.md`](./Synth-OOP-Language-Documentation-修正版.md)）
- 标准库参考 / Standard-library reference:
  [`Syclun标准库参考.md`](./Syclun标准库参考.md)
- 中英双语打包教程 / Bilingual packaging tutorial:
  [`Packaging-Tutorial-打包教程.md`](./Packaging-Tutorial-打包教程.md)
- 回到名片 / Back to the card: [`../README.md`](../README.md)
