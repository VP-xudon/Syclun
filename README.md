# Syclun — an interpreter for a language that refuses to be normal

# Syclun —— 一个拒绝正常的语言的解释器

[![CI / 持续集成](https://github.com/VP-xudon/Syclun/actions/workflows/ci.yml/badge.svg)](https://github.com/VP-xudon/Syclun/actions/workflows/ci.yml)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/VP-xudon/Syclun/badge)](https://securityscorecards.dev/viewer/?uri=github.com/VP-xudon/Syclun)
[![Codecov](https://codecov.io/gh/VP-xudon/Syclun/graph/badge.svg)](https://codecov.io/gh/VP-xudon/Syclun)

[![License](https://img.shields.io/github/license/VP-xudon/Syclun)](https://github.com/VP-xudon/Syclun/blob/main/LICENSE)
[![Stars](https://img.shields.io/github/stars/VP-xudon/Syclun)](https://github.com/VP-xudon/Syclun/stargazers)
[![Last Commit](https://img.shields.io/github/last-commit/VP-xudon/Syclun)](https://github.com/VP-xudon/Syclun/commits)
[![Issues](https://img.shields.io/github/issues/VP-xudon/Syclun)](https://github.com/VP-xudon/Syclun/issues)
[![Language](https://img.shields.io/badge/language-C%2B%2B-00599C)](https://isocpp.org/)
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-2ea44f)](https://github.com/VP-xudon/Syclun)

> **🎴 这是一张名片，也是一张引流牌。** 看完你大概会想：这语言也太怪了——然后发现它
> 怪得有道理。正式、严肃的那一份在文末「接下来 / 如何继续」。
> **This is a business card and a traffic sign.** By the end you'll think "this
> language is weird" — and then realize the weirdness is load-bearing. The
> serious, formal version waits at "Next steps / 如何继续" below.
>
> **⌨️ 你可以影响这门语言。** Syclun 是开源（GPL-3.0-or-later）、有治理、靠 DCO
> 签署的。在 **1.0.0 之前语法之窗仍开着**；你写的 `.syn` 程序是独立作品，不是演绎作品。
> **You can influence this language.** Syclun is open (GPL-3.0-or-later),
> governed, and DCO-signed. The syntax window is **still open before 1.0.0**;
> the `.syn` programs you write are independent works, not derivatives.

---

## 这门语言有多野？ / How wild is this language?

几条「怪」到让人笑出声、却又自洽的设计。每条都由解释器**强制执行**：
A few design choices that are funny-until-you-realize-they're-consistent. Each
is **enforced by the interpreter**:

- **没有运算符。** `io::out << "hi"` 不是语法糖，它就是值移动的**唯一**方式——对象之间
  显式的「流」。连加法都是方法：`a.+(b)`。
  **No operators.** `io::out << "hi"` isn't sugar for anything — it *is* the only
  way values move: explicit flows between objects. Even `+` is a method: `a.+(b)`.

- **变量不是盒子，是接口。** `a << 5` 不是「把 a 指向 5」，而是「把 5 流进 a 这个对象」。
  名字只是访问入口，对象才是实体。
  **Variables aren't boxes, they're interfaces.** `a << 5` doesn't repoint `a` at
  `5` — it *flows* `5` into the object `a`. A name is just an access point.

- **对象是活的。** 运行时给一个活对象「注入」新方法（`box:@hi << [{ … }];`）、塞一个只有
  它自己能碰的私有属性、再用 `box.#()` 把它**永久冻结**——单向开关，冻结后永不可解冻。
  **Objects are alive.** At runtime you can inject a new method onto a live object,
  give it a private attribute only its own methods can touch, then `box.#()` to
  freeze it **forever** — a one-way switch, never thawable.

- **库交付的是「能用的对象」，不是类。** `&io;` 一导入，`io::out` 就**存在**、生而为常数。
  没有工厂、没有仪式。而且你的 `.syn` 程序**禁止**声明全局对象——预置对象属于库。
  **Libraries ship working objects, not classes.** Import `&io;` and `io::out`
  *exists*, const from birth. No factory, no ceremony. And your `.syn` program is
  *forbidden* from declaring global objects — presets belong to libraries.

- **错误读起来像编译器。** 故障在源头抛出，带 `^~~~` 插入符和完整执行栈；致命错误退出码
  `1`，所以 `synth bad.syn && cmd` 永远不会执行 `cmd`。
  **Errors read like a compiler's.** Faults surface at the source with a `^~~~`
  caret and the full execution stack; fatal errors exit `1`, so
  `synth bad.syn && cmd` never runs `cmd`.

```text
// 一个会「长大」的对象 / an object that "grows up" at runtime
&io;
$Program {
    @:: << [{
        -(std::String s);
        s:@shout << [() ~> (out[std::String]) { out << s.+( "!!"); }];
        s << "hello";
        io::out.push_line(s.shout());      // → hello!!
        s.#();                             // 冻结，此后不可再注入
    }];
}
```

想看更多「哲学验证」用例？`verify/philosophy/*.syn` 每个文件都在实际运行里印证一条设计承诺。
Want more philosophy-in-action? Every file in `verify/philosophy/*.syn` asserts one
design commitment by actually running it.

---

## 30 秒上手 / 30-second tour

### 0. 拿到解释器 / Get the interpreter

**最快：下载预编译 Release 包，解压即用，无需编译。** 每个 Release 附五个平台的开箱即用包。
**Fastest: download a prebuilt release, unzip, run. No compile needed.** Each
release ships five ready-to-run packages.

| Platform / 平台 | Package / 包 |
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

**或者，从源码一行构建（需要 CMake + C++23 编译器）：**
**Or build from source in one line (needs CMake + a C++23 compiler):**

```bash
bash build.sh                       # 生成 ./build/synth（Windows 下为 synth.exe）
./build/synth examples/hello.syn
```

### 1. 你好，世界 / Hello, world

[`examples/hello.syn`](./examples/hello.syn) — 最小的完整程序 / the smallest complete program:

```
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
  A program *is* the construction of its `$Program` class; the entry is the `@::`
  (construct) behavior — **no global scope**.
- `&io;` 导入库并随之交付成品常数对象 `out`——无需声明、无需工厂。
  `&io;` imports the library *and ships the ready-made const object `out`*.
- `[{ … }]` 是空签名行为的代码块糖。
  `[{ … }]` is the block sugar for an empty-signature behavior.

### 2. 值在「流」里流动 / Values travel through flows

没有中缀运算符；`a.+(b)` 就是普通方法调用。
No infix operators; `a.+(b)` is an ordinary method call.

```
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

更多可跑示例：`examples/flow.syn`、`examples/counter.syn`，以及 `verify/philosophy/*.syn`。
More runnable samples: `examples/flow.syn`, `examples/counter.syn`, and
`verify/philosophy/*.syn`.

---

## 梯度贡献表：从 5 分钟到影响语言走向 / Gradient contribution table

按**时间**与**难度**排好序——挑一个今天就动手。每条都指向真实、可验证的成果。
Ordered by **time** and **difficulty** — pick one and ship something today. Each
points at a real, verifiable outcome.

| 投入 / Effort | 难度 | 你能做成的事 / What you can do | 怎么开始 / How to start |
|---|---|---|---|
| **5 分钟** | 🟢 零门槛 | 跑通 `hello.syn`，亲手感受「流」怎么动 | 下 Release 包，或 `bash build.sh && ./build/synth examples/hello.syn` |
| **15 分钟** | 🟢 零门槛 | 写你的第一个 `.syn` 小程序（斐波那契 / 猜数字） | 抄 `examples/hello.syn`，改 `io::out.push_line(...)` |
| **30 分钟** | 🟡 易 | 给回归套件补几个**边缘用例**（空串、负数、溢出、超大数） | 在 `verify/unit/assert_runtimes.cpp` 加 `EXPECT(...)`，跑 `./build/assert_runtimes` |
| **1 小时** | 🟡 易 | 在**自家电脑上产出一份基于本系统的分发包**（`package.sh`，此前没有这种一键能力） | `bash package.sh`，看 `build/dist/` 下的平台包 |
| **2 小时** | 🟠 中 | 给某个标准库加一个**小方法**（如给 `maths` 加 `hypot`） | 改 `lib/cpp/maths.hpp` + `lib/maths.synl`，跑三套回归 |
| **半天** | 🟠 中 | 新增一条**哲学验证用例**（`verify/philosophy/*.syn`）印证某条设计承诺 | 仿 `d1_zero.syn`，在文件头写清要印证的承诺 |
| **1 天** | 🔴 难 | 修一个解释器 bug，或实现一处规范细则 | 看 `src/*.hpp`，先用断言复现，再提 PR（带 DCO 签署） |
| **持续** | 🔴 难 | **影响语言走向**：在 1.0.0 前提案语法 / 语义改动 | 开 issue / 讨论，需「必要性」论证（见 `CONTRIBUTING.md`） |

> 门槛越低越欢迎：把「跑通 hello」截图发出来、把发现的反直觉行为写进 issue，都是贡献。
> Lower the bar, the better: a "I got hello running" note or an issue about a
> surprising behavior counts as contribution.

---

## 接下来 / 如何继续 / Next steps

完整、分层、通俗易懂的「维基式」介绍都在 **GitHub Wiki**（README 只负责勾引 👀）：
The full, layered, plain-spoken **wiki** holds the complete introduction (this README only hooks you):

- 🏠 **Wiki 首页 / Home** — <https://github.com/VP-xudon/Syclun/wiki/Home>
- 📘 **设计理念 / Design Philosophy** — <https://github.com/VP-xudon/Syclun/wiki/Design-Philosophy>
- 🚀 **快速上手 / Quick Start** — <https://github.com/VP-xudon/Syclun/wiki/Quick-Start>
- 🧱 **基本语法 / Basic Syntax** — <https://github.com/VP-xudon/Syclun/wiki/Basic-Syntax>
- 📚 **标准库总览 / Standard Libraries** — <https://github.com/VP-xudon/Syclun/wiki/Standard-Libraries>
- 🧰 **标准库对象使用 / Using StdLib Objects** — <https://github.com/VP-xudon/Syclun/wiki/Standard-Library-Objects>
- 🤝 **贡献指南 / Contributing** — <https://github.com/VP-xudon/Syclun/wiki/Contributing>
- 📦 **打包与发布 / Packaging** — <https://github.com/VP-xudon/Syclun/wiki/Packaging>
- ❓ **常见问题 / FAQ** — <https://github.com/VP-xudon/Syclun/wiki/FAQ>

> 权威语言规范（v1.31）仍以 `doc/Synth-OOP语言文档-修正版.md` 为准；本 Wiki 是其通俗、分层的导读。
> The authoritative spec (v1.31) remains `doc/Synth-OOP语言文档-修正版.md`; the Wiki is its plain-spoken, layered companion.

---

## 项目亮点 / At a glance

- **跨平台、零第三方依赖。** 纯 C++23、header-only 引擎，除编译器自带标准库外无任何依赖。
  `lib/cpp/` 下的哈希、正则、数据结构实现均为本项目原创。
  **Cross-platform, zero third-party deps.** Pure C++23 header-only engine; the
  only requirement is your compiler's standard library.
- **312 条断言测试，全绿——且由 CI 强制保障。** 三套回归套件（词法 83 / 语法 71 /
  运行时·对象 158）在**每次 push/PR** 经 `ci.yml` 运行、并在 `release.yml` 的发布门禁中
  复跑；任一断言失败即阻断构建/发布。
  **312 assertion tests, all green — enforced by CI.** Three suites (lexer 83 /
  parser 71 / runtime·object 158) run on every push/PR via `ci.yml` and again as
  the release gate; one failing assertion blocks the build/release.
- **即时、可追踪的错误 + IEEE 754 语义。** 故障在源头以类 g++ 诊断浮现；失败以非 0 退出码
  结束，CI 必察觉。
  **Immediate, traceable errors + IEEE 754.** Faults surface at source g++-style;
  non-zero exit makes CI notice.
- **成体系的标准库（10 个模块）** 与 **VS Code 语法高亮插件**。
  **A real standard library (10 modules)** and a **VS Code highlighting extension**.
- **开放、有治理、且属于你。** GPL-3.0-or-later + DCO 签署；你写的 `.syn` 程序是独立作品。
  **Open, governed, and yours.** GPL-3.0-or-later with DCO; your `.syn` programs
  are independent works.

---

## 标准库一览 / Standard libraries

用 **`&module;`** 导入（模块名小写）；类名为模块名首字母大写，并以 `module::Class` 形式引用
（如 `&maths;` → `maths::Maths`）。
Import with **`&module;`** (lower-case); the class is the capitalized module name,
referenced as `module::Class` (e.g. `&maths;` → `maths::Maths`).

| Import | Classes / 类 | 用途 / What it does |
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

详细的**方法签名**与「如何新增标准库」清单，见 Wiki 的
[标准库总览 / Standard Libraries](https://github.com/VP-xudon/Syclun/wiki/Standard-Libraries)
与
[标准库对象使用 / Using StdLib Objects](https://github.com/VP-xudon/Syclun/wiki/Standard-Library-Objects)。

---

## 编辑器支持 / Editor support (VS Code)

[`vscode-synth-oop/`](./vscode-synth-oop/) 是纯 TextMate 语法的 **VS Code 语法高亮插件**，
只高亮、不编译不运行，因此绝不与解释器冲突。识别 `.syn` / `.synl` / `.syni` 三种文件，
高亮 `@ $ # !` 标记、模块导入（`&io;`）、控制方法（`if_`/`while_`/`repeat_`）、流运算符
（`<< >> =: :=`）等。每个 Release 附 `synth-oop-<version>.vsix`；插件采用 **MIT** 许可。
[`vscode-synth-oop/`](./vscode-synth-oop/) is a pure-TextMate **VS Code syntax
extension** — it highlights only, never compiles or runs, so it never fights the
interpreter. It recognizes `.syn`/`.synl`/`.syni`, highlights `@ $ # !` markers,
imports (`&io;`), control methods, and flow operators. Every release attaches
`synth-oop-<version>.vsix`; the extension is **MIT**-licensed.

---

## 目录结构 / Project layout

```
Syclun/
├── README.md                  # 本名片 / this card
├── CONTRIBUTING.md            # PR 格式、自检、拒收清单 / PR format & rejection list
├── DCO.md                     # 开发者原创证书 / Developer Certificate of Origin
├── CMakeLists.txt             # 构建唯一事实来源 / single source of build truth
├── build.sh  package.sh       # 一键构建 / 一键打包（5 平台）
├── .github/
│   ├── workflows/
│   │   ├── ci.yml             # push/PR 门禁：构建 + 跑全部断言
│   │   ├── release.yml        # 验证全部断言 → 构建并发布五个包
│   │   └── scripts/           # run_asserts.sh / gen_release_notes.sh
│   └── dco.yml                # 自包含 DCO 检查
├── doc/                       # 正式文档（见「接下来」）/ formal docs
├── examples/                  # hello.syn / flow.syn / counter.syn
├── lib/  lib/cpp/             # 标准库（.synl 接口 + C++ 底层）
├── src/                       # 解释器引擎（header 模块 + main.cpp）
├── verify/                    # 哲学验证用例 + 三套断言套件
└── vscode-synth-oop/          # VS Code 语法高亮插件（MIT）
```

---

## 贡献 / Contributing

详见 **Wiki 贡献指南** [Contributing](https://github.com/VP-xudon/Syclun/wiki/Contributing)
（仓库内的 [`CONTRIBUTING.md`](./CONTRIBUTING.md) 为完整版：PR 标题/正文格式、自检清单、拒收清单）。
动手前两条铁律：
See the **Wiki Contributing guide** [Contributing](https://github.com/VP-xudon/Syclun/wiki/Contributing)
(the in-repo [`CONTRIBUTING.md`](./CONTRIBUTING.md) is the full version). Two rules before
you write a line:

1. **1.0.0 之后语法基本冻结**——语法改动需「必要性」论证，而非偏好。
   **After 1.0.0 the syntax is effectively frozen** — a syntax change needs a
   necessity argument, not a preference. (No `1.0.0` tag yet, so the window is open.)
2. **别轻易新开标准库名**——能归入 `maths`/`structs`/`file`/`system`/`re`/`hash`/`io`/
   `async`/`assert`/`sugar` 的功能就归入；此类 PR 会被打回。
   **Don't invent a new standard-library name** if it fits an existing module.

贡献以 **DCO（开发者原创证书）** 接受，而非 CLA：每个提交须带 `Signed-off-by` 尾注，
`git commit -s` 会自动加。
Contributions are accepted under the **Developer Certificate of Origin** — every
commit must carry a `Signed-off-by` trailer, which `git commit -s` adds for you.

> 门槛低也欢迎：跑通示例、补边缘用例、提 issue 描述反直觉行为，都是贡献。
> Low-bar contributions count too: running the samples, adding edge-case tests, or
> filing an issue about surprising behavior.

---

## 许可证 / License

```
Syclun — Synth-OOP Interpreter & Philosophy Verification
Copyright (C) 2026 VP_xudon
SPDX-License-Identifier: GPL-3.0-or-later
```

本项目为自由软件，采用 **GNU GPL 第 3 版或任何后续版本**。你用 Synth-OOP **编写**的程序
（`.syn`/`.synl`/`.syni` 及其输出）是**独立作品**，不是本解释器的演绎作品——正如用 GCC
编译出的 C 程序并非 GCC 的演绎作品。VS Code 插件（`vscode-synth-oop/`）单独以 **MIT** 许可。
This project is free software under the **GNU GPL v3 or later**. The Synth-OOP
programs *you* write (`.syn`/`.synl`/`.syni` and their output) are **independent
works**, not derivatives — just as a C program compiled with GCC is not a derivative
of GCC. The VS Code extension (`vscode-synth-oop/`) is separately **MIT**-licensed.
