# Standard Libraries
## 标准库总览

> 标准库怎么导入、有哪些模块、哪些是「随导入就存在的对象」。
> How standard libraries are imported, which modules exist, and which objects "just exist" on import.

---

## 1. 导入：一句话 / Import: one statement

```text
&module;
```

模块名小写；导入后，解释器会解析 `lib/<module>.synl`，定义其中的类，并把已登记的 C++ 底层一并上线。
Lower-case module name; the interpreter resolves `lib/<module>.synl`, defines its classes, and brings its C++ backend online.

```text
&io;
&maths;
&re;
```

`&` 是稳健的：导入一个不存在的模块（无 `.synl` 且无登记的 C++ 底层）是**静默空操作**，不会报错。
`&` is robust: importing a non-existent module is a **silent no-op**.

---

## 2. 模块清单 / Module list

| 导入 / Import | 类 / Classes | 用途 / What it does |
|--------|--------------|---------------------|
| `&io;` | `OStream`, `IStream` | 标准输入输出流 / standard streams |
| `&file;` | `File` | 文件读写追加 / file read·write·append |
| `&system;` | `System` | shell、环境变量、时间、`wait` |
| `&maths;` | `Maths` | 标量数学 / scalar math |
| `&hash;` | `Hash` | `sha256` / `crc32` / `fnv1a` |
| `&structs;` | `Queue`, `Stack`, `Tree`, `Map`, `Graph` | 数据结构 / data structures |
| `&re;` | `Re`, `Pattern`, `Match` | 正则表达式 / regular expressions |
| `&async;` | `Reactor`, `Task`, `Error` | 异步运行时 / async runtime |
| `&assert;` | `Checker` | 运行期合法性校验 / runtime checks |
| `&sugar;` | `Infix` | 算术表达式求值（`1+(2-3)*(3+5)` → `-7`） |

---

## 3. 预置对象 / Preset objects

自 v1.31，库的 `.synl` 可在顶层创建**成品常数对象**，随导入到来、贯穿整个运行期。它们属于库命名空间，你的 `.syn` 程序**不得**自己声明全局对象。
Since v1.31, a library's `.synl` may create **ready-made const objects** at the top level; they arrive with the import and live for the whole run. They belong to the library's namespace; your `.syn` must **not** declare global objects itself.

| 模块 / Module | 预置对象 / Preset | 类型 / Type |
|------|------|------|
| `io` | `io::out` | `io::OStream` |
| `io` | `io::in` | `io::IStream` |
| `maths` | `maths::math` | `maths::Maths` |

```text
&io;
$Program {
    @:: << [{
        io::out.push_line("io::out 生来就在");   # 不用声明 / no need to declare
    }];
}
```

---

## 4. 运行期签名强制 / Runtime signature enforcement

每个原生 / 内置方法都携带精确的调用签名，运行时在**调用边界**先校验实参类型，再进入方法体：
Every native/builtin method carries a precise signature; at the call boundary the runtime checks argument types *before* the body runs:

- 类型不符 → 即时抛 `TypeException: Argument type mismatch for method '…'`。
- 方法缺失 → 即时抛 `Method '…' not found`。
- 流 / 生命周期方法（`::`/`~`/`=:`/`=`/`:=`）、通用 / 行为 / 变参（`std::Object`、`@`、`...`）、异构元素标签 `value` 有意豁免。

这让「错误输入」在源头被拦下，而不是在闭包深处炸开。
Bad input is caught at the source, not deep inside a closure.

---

## 5. 想加一个新库？ / Adding a library

见 [`Syclun标准库参考`](https://github.com/VP-xudon/Syclun/blob/main/doc/Syclun标准库参考.md) 的「如何新增标准库」清单（`.synl` 接口 + `lib/cpp/<name>.hpp` 底层 + 自注册 + 聚合进 `std_libs.hpp` + 示例 + 重建验证）。
See the "how to add a standard library" checklist in the [Syclun Standard-Library Reference](https://github.com/VP-xudon/Syclun/blob/main/doc/Syclun标准库参考.md).

> ⚠️ 贡献硬规则：**别轻易新开库名**。能归入现有模块（`maths`/`structs`/`file`/`system`/`re`/`hash`/`io`/`async`/`assert`/`sugar`）的功能就归入。
> Hard rule: **don't invent a new library name** unless it truly can't fit an existing module.

👉 每个模块怎么用：[标准库对象使用 / Using StdLib Objects](Standard-Library-Objects)
