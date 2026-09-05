# Basic Syntax
## 基本语法

> 一份够用的语法地图：从「程序长什么样」到「流、行为、方法、类、约束」都覆盖，配可运行示例。
> A usable syntax map: from "what a program looks like" to "flows, behaviors, methods, classes, constraints", with runnable examples.

---

## 1. 程序结构 / Program structure

程序**就是** `$Program` 类的构造过程。入口是 `@::`（构造行为），**没有全局作用域**。
A program *is* the construction of its `$Program` class. The entry is `@::` (construct behavior) — **no global scope**.

```text
&io;                       # 导入标准库 / import a stdlib
$Program {                # 程序类 / the program class
    @:: << [{             # 构造行为 = 程序入口 / construct behavior = entry point
        io::out.push_line("hi");
    }];
}
```

- `&io;` 导入库并随带成品对象 `io::out` / `io::in`。
- `[{ … }]` 是空签名行为 `[() -> () { … }]` 的块糖。

---

## 2. 对象实例化与变量 / Instantiation & variables

语法：`- 类型名 变量名`。**圆括号里必须写「类型名 + 变量名」两项**，不能只写变量名。
Syntax: `- type name`. The parentheses **must hold both a type and a name** — never the name alone.

```text
-(std::Number a);              # 仅声明，自动零值 / declare, auto zero-value
-(std::String s) << "Yahoo!";  # 声明并赋值 / declare and bind
-(std::Number! frozen) << 7;   # 常数：类型后加 ! / const: ! after the type
```

- **零值法则**：新对象自带缺省值（`Number`→`0`、`String`→`""`、`Boolean`→`false`）。
  **Zero-value law**: a fresh object carries a default (`Number`→`0`).
- **赋值用 `<<`**：`a << b` 是把 b 流进 a，等价于显式调用 `a.=(b)`。中缀 `a = b` 是**非法的**（没有 `=` 运算符）。
  **Assign with `<<`**: `a << b` flows b into a, equal to `a.=(b)`. Infix `a = b` is **illegal**.
- 实例化语句本身是**表达式**，值就是新对象，可嵌进别的表达式（`out << (-(std::String t) << "x")`）。

---

## 3. 流语句 / Flows

值通过显式的「流」在对象之间移动：
Values move between objects through explicit *flows*:

```text
A >> B;     # A 流向 B / A flows to B
B << A;     # 与上式完全等价 / exactly equal to the above
```

- `<<` 左向流、`>>` 右向流，语义相同。
- 发送方先「公布」(`=:`)，接收方再「接收」(`:=`)；同类型间 `a << b` 就是赋值。
- 向 `io::OStream` 写入（`out << v`）是输出动作，**不修改 `out` 自身**，所以 `out` 可声明为常数。

---

## 4. 行为（闭包）/ Behaviors (closures)

可执行逻辑的基本单元，用方括号书写。**函数、闭包、方法、控制流分支——底层都是行为。**
The basic unit of executable logic, written with brackets. *Functions, closures, methods, branches — all are behaviors underneath.*

```text
[(输入参数) 行为模式 (输出) { 函数体 }]
```

**三级行为模式箭头**（由严到宽）：
**Three behavior-mode arrows** (strict → loose):

| 箭头 | 模式 | 能做什么 |
|------|------|----------|
| `=>` | 零副作用 | 连外部都不能访问，纯计算 |
| `~>` | 常数 | 可**读**外部环境，不可改 |
| `->` | 非常数 | 可读**也可改**外部环境 |

```text
[(a) => (sum) { sum << a.+(1); }]   # 纯计算，绝不碰外界 / pure, touches nothing outside
[(a) ~> (sum) { sum << a.+(1); }]   # 只读外界 / read-only
[() -> ()   { io::out << "x"; }]    # 要写外界，用 -> / needs -> to write out
```

- 行为也是对象：可传递、可延迟执行、可绑定成方法。
- 空输出 `()` 表示「不公布任何值」（取代已退役的 `void`）。

---

## 5. 方法 / Methods

方法用 `@方法名 << 行为` 声明，三形态：
Declare methods with `@name << behavior`; three shapes:

```text
@get  << [() ~> (result) { result << value; }];        # 读 / getter
@inc  << [() -> ()     { value << self.next_value(); }]; # 写，用 self / setter, uses self
@name << [() -> ()     { … }];                          # 普通 / plain
```

- 实例方法经 `self.NAME(...)` 调用（`self` 指当前对象）。
- 四个保留方法名：`@::`（构造）、`@~`（析构）、`@=:`（公布）、`@:=`（接收）。
- 约束检查见第 8 节。

---

## 6. 类 / Classes

```text
$Counter {
    -(std::Number value);
    @get << [() ~> (result) { result << value; }];
    @inc << [() -> () { value << self.next_value(); }];
}
```

- 类名以 `$` 开头；实例用 `-(Counter c)` 创建。
- 父类可以是类，也可以是**实例**（从实例派生类）。
- 类内自调用必须经 `self`。

---

## 7. 控制流 / Control flow

`if_` / `while_` / `repeat_` 是布尔值 / 数字的方法，接收「条件检查行为」和「循环体行为」：
`if_`/`while_`/`repeat_` are methods on Booleans/Numbers, taking a *condition* behavior and a *body* behavior:

```text
&io;
$Program {
    @:: << [{
        -(std::Boolean ok) << true;
        ok.if_([{ io::out << "yes"; }], [{ io::out << "no"; }]);   # if_ 条件, 真分支, 假分支
        -(std::Number i) << 0;
        i.while_([{ i.<(3); }], [{ io::out << i; i << i.+(1); }]); # while_ 条件, 体
        (5).repeat_([{ io::out << "x"; }]);                         # repeat_ 次数
    }];
}
```

> 行为参数会被**参数严格性**检查：条件/循环体行为的签名必须匹配。
> Behavior arguments are checked for **parameter strictness**: their signatures must match.

---

## 8. 约束（接口）/ Constraints (interfaces)

约束 = 一张「必须有哪些方法」的清单（鸭子类型的强类型盔甲）。用 `#` 前缀声明：
A constraint is a checklist of "which methods an object must have". Declared with `#`:

```text
#Addable {
    @+ << [(other) -> (result) {}];     # 只写签名，函数体留空 / signature only
}

[(a[Addable]) -> (r) { r << a.+(1); }]  # 参数必须满足 Addable / param must satisfy Addable
```

- `#Name [父约束]` 支持约束继承（`#Comparable [Addable] { … }`）。
- 直接填类名当约束：`[(s[std::String]) -> () {}]` 要求方法签名与 `std::String` 一致。
- `@` 表示「必须是行为」：`[(h[@]) -> () {}]`；`@` 后可跟行为签名做精确匹配。
- 约束在**运行期**核对（按方法签名集合比对，不看参数名）。不满足立即抛 `ConstraintException`。

---

## 9. 运行期注入（v1.31）/ Runtime injection

对象构造后仍是活的：
Objects stay alive after construction:

```text
obj:@m   << [{ … }];    # 注入新方法 / inject a new method
obj:-(T v) << init;     # 加私有属性（仅可初始化，只有自身方法能访问）/ private attr
obj.#();                # 永久冻结 / freeze forever
```

- 向已有名字注入 = 重复声明错误；重绑常数方法 = 更改常量错误。
- 库预置对象：库的 `.synl` 可在顶层创建常数实例（`-(io::OStream! io::out);`），随导入到来。

---

## 10. 错误 / Errors

- 故障在**源头**抛出，以类 g++ 格式报告：`文件:行:列: error: 类型: 消息` + `^~~~` 插入符 + 完整执行栈。
- 致命错误退出码为 `1`，所以 `synth bad.syn && cmd` 绝不会执行 `cmd`。
- 算术遵循 IEEE 754：`1/0` → `Infinity` / `NaN`，不中断。

---

## 附录：语法符号速查 / Cheat sheet

| 符号 | 含义 |
|------|------|
| `-(T v)` | 实例化对象 / instantiate |
| `<<` `>>` | 左/右向流（含赋值、方法绑定） |
| `=>` `~>` `->` | 行为模式：零副作用 / 常数 / 非常数 |
| `[ … ]` | 行为界定 / behavior |
| `@名` | 方法声明 / method decl |
| `!`（类型后） | 常数变量 / const var |
| `#` | 约束前缀 / constraint |
| `&模块;` | 导入库 / import |
| `::` | 构造/命名空间 / construct / namespace |
| `self` | 当前对象 / current object |
| `=:` `:=` | 公布 / 接收函数 |

👉 标准库怎么用：[标准库对象使用 / Using StdLib Objects](Standard-Library-Objects)
