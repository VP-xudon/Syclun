# Design Philosophy
## 设计理念

> 多数语言要把你变得「正常」。Synth-OOP 拥抱自己的怪异——下面每一条「怪」都是一条**哲学立场**，并且都由解释器**强制执行**。
> Most languages normalize you. Synth-OOP leans into its strangeness — every oddity below is a *philosophical position*, enforced by the interpreter.

---

## 1. 没有运算符 / No operators

`io::out << "hi"` 不是什么语法糖——它就是值移动的**唯一**方式：对象之间显式的「流」。连加法都是方法调用：`a.+(b)`。
`io::out << "hi"` isn't sugar for anything — it *is* the only way values move: explicit flows between objects. Even `+` is a method: `a.+(b)`.

为什么？因为「数据在对象之间流动」是这门语言的第一性原理。当你写 `a << b`，你是在说「把 b 流进 a」，而不是「让 a 等于 b」。
Why? "data flows between objects" is the first principle. `a << b` means "flow b into a", not "a equals b".

## 2. 变量不是盒子，是接口 / Variables are interfaces, not boxes

`a << 5` 不是「把 a 指向 5」，而是「把 5 流进 a 这个对象」。名字只是一个**访问入口**，对象才是实体。
`a << 5` doesn't repoint `a` at `5` — it *flows* `5` into the object `a`. A name is just an access point; the object is the real thing.

语言也没有「类型」：声明处的类名（`std::Number`）是你借以**塑造**对象的模板，而不是对象终身佩戴的标签。
There are no "types" either: the class name at declaration (`std::Number`) is the template you build *from*, not a label the object carries forever.

## 3. 对象是活的 / Objects are alive

构造之后对象仍是活的，你可以：
Objects stay alive after construction. You can:

- 运行时**注入**一个方法：`box:@hi << [{ … }];`
  inject a method at runtime: `box:@hi << [{ … }];`
- 给它一个**私有属性**，只有它自己的方法能碰：`box:-(std::Number secret) << 42;`
  give it a *private* attribute only its own methods can touch: `box:-(std::Number secret) << 42;`
- 用 `box.#()` 把它**永久冻结**——单向、只可设置的开关。任何东西都无法移除，冻结的对象永不可解冻。
  `box.#()` to **freeze** it forever — a one-way, set-only switch. Nothing can be removed; a frozen object never thaws.

## 4. 代码即代码块，无需仪式 / Code is a block, not a ceremony

`[{ … }]` 就是空签名行为——条件分支与回调的自然形态。
`[{ … }]` is an empty-signature behavior — the natural shape for branches and callbacks.

## 5. 库交付的是「能用的对象」，不是类 / Libraries ship working objects, not classes

导入 `&io;`，`io::out` / `io::in` 就**存在**，生而为常数。没有工厂、没有仪式。
Import `&io;` and `io::out` / `io::in` *exist*, const from birth. No factory, no ceremony.

而你的 `.syn` 程序**被禁止**声明全局对象——预置对象属于库。
And your `.syn` program is *forbidden* from declaring global objects — presets belong to libraries.

## 6. 错误读起来像编译器 / Errors read like a compiler's

故障在**源头**抛出（绝不把错误值静默地「毒化」数据流），并以类 g++ 的格式报告：带 `^~~~` 插入符的出错行与完整**执行栈**。致命错误退出码为 `1`。
Faults surface at the **source** (never silently poisoning the data flow) and are reported g++-style: a `^~~~` caret and the full **execution stack**. Fatal errors exit `1`.

算术遵循 IEEE 754：`1/0` 得到 `Infinity` 或 `NaN`，不会中断。
Arithmetic honours IEEE 754: `1/0` yields `Infinity`/`NaN`, never a crash.

---

## 为什么这样设计？/ Why design it this way?

Synth-OOP 想回答一个问题：**如果一门语言把「计算 = 有状态对象之间经显式行为与流的演化」当成唯一模型，会发生什么？**
Synth-OOP asks: *what if a language takes "computation = the evolution of stateful objects through explicit behaviors and flows" as its only model?*

上面六条怪，都是这个模型的自然推论——它们不是为怪而怪，而是为了让「流动」「活对象」「可追溯的错误」成为语言的底色。
The six oddities above are natural corollaries of that model — not weirdness for its own sake, but the bedrock that makes "flow", "live objects", and "traceable errors" the default.

> 想看这些理念被「跑出来」印证？`verify/philosophy/*.syn` 每个文件都在实际运行中验证一条设计承诺。
> Want to see these ideas *run*? Every file in `verify/philosophy/*.syn` asserts one design commitment by actually executing it.

👉 下一篇：[基本语法 / Basic Syntax](Basic-Syntax)
