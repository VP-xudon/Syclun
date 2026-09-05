# Quick Start
## 快速上手

> 五分钟跑通第一个程序，感受「值如何在流里流动」。
> Get your first program running in five minutes and feel how values flow.

---

## 0. 拿到解释器 / Get the interpreter

**最快：下载预编译 Release 包，解压即用，无需编译。**
**Fastest: download a prebuilt release, unzip, run — no compile needed.**

每个 Release 附五个平台的开箱即用包：
Each release ships five ready-to-run packages:

| 平台 / Platform | 包 / Package |
|-----------------|--------------|
| Windows x64 | `synth-windows-x64.zip` |
| Windows ARM64 | `synth-windows-arm64.zip` |
| macOS x64 (Intel) | `synth-macos-x64.zip` |
| macOS ARM64 (Apple Silicon) | `synth-macos-arm64.zip` |
| Linux x64 | `synth-linux-x64.zip` |

```bash
# 解压后 / after unzipping
bin/synth examples/hello.syn        # Linux / macOS
bin\synth.exe examples\hello.syn    # Windows (cmd)
```

**或者，从源码一行构建**（需要 CMake ≥ 3.16 + 支持 C++23 的编译器）：
**Or build from source in one line** (needs CMake ≥ 3.16 + a C++23-capable compiler):

```bash
bash build.sh                       # 生成 ./build/synth（Windows 下为 synth.exe）
./build/synth examples/hello.syn
```

---

## 1. 你好，世界 / Hello, world

[`examples/hello.syn`](https://github.com/VP-xudon/Syclun/blob/main/examples/hello.syn) — 最小的完整程序 / the smallest complete program:

```text
&io;
$Program {
    @:: << [{
        io::out.push_line("Hello, Synth-OOP!");
    }];
}
```

```bash
$ ./build/synth examples/hello.syn
Hello, Synth-OOP!
```

三件值得注意的事 / three things to notice:
- 程序**就是** `$Program` 类的构造过程，入口是 `@::`（构造行为），**没有全局作用域**。
  A program *is* the construction of its `$Program` class; the entry is `@::` — **no global scope**.
- `&io;` 导入库并随之交付成品常数对象 `out`——无需声明、无需工厂。
  `&io;` imports the library *and ships the ready-made const object `out`*.
- `[{ … }]` 是空签名行为的代码块糖。
  `[{ … }]` is the block sugar for an empty-signature behavior.

---

## 2. 值在「流」里流动 / Values travel through flows

没有中缀运算符；`a.+(b)` 就是普通方法调用。
No infix operators; `a.+(b)` is just an ordinary method call.

```text
&io;
&maths;
$Program {
    @:: << [{
        -(std::Number r) << 3;
        -(std::Number area) << maths::math.pi().*(r).*(r);
        io::out.push_line(area);          // → 28.274333882308138
        -(std::Number one) << 1;
        -(std::Number zero);
        io::out.push_line(one./(zero));   // → Infinity  (IEEE 754)
    }];
}
```

> **下标从 0 开始；构建由 CMake 驱动。** `Array`/`Tuple`/`String` 的下标从 `0` 起。
> **Indexing is 0-based; the build is CMake-driven.**

---

## 3. 跑更多示例 / Run more samples

```bash
for f in examples/*.syn;         do ./build/synth "$f"; done
for f in verify/philosophy/*.syn; do ./build/synth "$f"; done
```

- `examples/flow.syn`、`examples/counter.syn` — 流、零值、类与状态
- `verify/philosophy/*.syn` — 每个文件印证一条设计承诺（零值法则、流传递、实例化即表达式……）

---

## 4. 想知道更多？ / Want more?

- 语法细节 👉 [基本语法 / Basic Syntax](Basic-Syntax)
- 标准库怎么用 👉 [标准库对象使用 / Using StdLib Objects](Standard-Library-Objects)
- 想贡献一行代码 👉 [贡献指南 / Contributing](Contributing)
