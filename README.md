# Syclun — Synth-OOP Interpreter & Philosophy Verification

# Syclun —— Synth-OOP 解释器与语言哲学验证

**A from-scratch, cross-platform tree-walking interpreter for the Synth-OOP
language** — built to prove the language's design philosophy (the zero-value
law, flow-based value passing, instantiation-as-expression, duck-typed
constructors, immediate and traceable errors, …) while being a genuinely
runnable, tested toolchain.

**一个从零实现的、跨平台的 Synth-OOP 语言树遍历解释器**——既用于验证该语言的
设计哲学（零值法则、基于流的值传递、实例化即表达式、鸭子式构造函数、即时可追踪
错误等），也是一套真正可运行、经过测试的工具链。

> **Errors are immediate and traceable.** A fault is raised at its source —
> never propagated as a silently-poisoned value — and reported g++-style:
> `file:line:col: error: Type: message`, the offending source line with a
> `^~~~` caret, and the full **execution stack**. Arithmetic follows IEEE 754
> (dividing by zero yields `Infinity` / `NaN`). Set `NO_COLOR=1` to disable
> colour. Details: Appendix E of the language documentation.
> **错误即时且可追踪。** 故障在源头抛出——绝不以静默的「毒水」值传播——并以类 g++
> 的格式报告：`文件:行:列: error: 类型: 消息`、带 `^~~~` 插入符的出错源码行，以及
> 完整**执行栈**。算术遵循 IEEE 754（除以零得 `Infinity` / `NaN`）。设
> `NO_COLOR=1` 可关闭配色。详见语言文档附录 E。

> **License.** GPL-3.0-or-later — see [`LICENSE`](./LICENSE). Programs you
> *write* in Synth-OOP are **not** covered by it (see below).
> **许可证**：GPL-3.0-or-later，详见 [`LICENSE`](./LICENSE)。你用 Synth-OOP
> **编写**的程序**不受**该许可证约束（见文末说明）。

---

## 30-second tour / 30 秒体验

### 0. Get the interpreter / 获取解释器

**Fastest path — download a prebuilt release; nothing to compile.** Every
release ships five ready-to-run packages, one per platform. Unzip and go.
**最省事的办法——直接下载预编译发行包，无需编译。** 每个 Release 都提供五个平台
的开箱即用包，解压即用。

| Platform / 平台 | Package / 包 |
|-----------------|--------------|
| Windows x64 | `synth-windows-x64.zip` |
| Windows ARM64 | `synth-windows-arm64.zip` |
| macOS x64 (Intel) | `synth-macos-x64.zip` |
| macOS ARM64 (Apple Silicon) | `synth-macos-arm64.zip` |
| Linux x64 | `synth-linux-x64.zip` |

```bash
# after unzipping / 解压之后
bin/synth examples/hello.syn        # Linux / macOS
bin\synth.exe examples\hello.syn    # Windows (cmd)
```

Each package is self-contained: `bin/synth(.exe)` is the interpreter and
`libs/` holds the standard-library interfaces, which the interpreter resolves
relative to the executable — so it works from any working directory.
每个包都是自包含的：`bin/synth(.exe)` 是解释器，`libs/` 存放标准库接口，解释器依
可执行文件位置解析它们，因此在任意工作目录下都能运行。

> Want to build it yourself or cut a release? That is a separate, **optional**
> step — see the **Packaging tutorial / 打包教程** section further down.
> 想自己构建或制作发布包？那是独立的**可选**步骤，见后文**「打包教程」**章节。

> **Indexing is 0-based** and the build is CMake-driven. `Array`/`Tuple`/`String`
> indices start at `0` (`arr.get(0)` is the first element). The `sugar` standard
> library adds `Infix`, an arithmetic-expression evaluator
> (`-(sugar::Infix("1+(2-3)*(3+5)") e); e.parse()` → `-7`).
> **下标从 0 开始**，且构建由 CMake 驱动。`Array`/`Tuple`/`String` 的下标从 `0`
> 起（`arr.get(0)` 是首个元素）。`sugar` 标准库新增 `Infix` 算术表达式求值器
> （`-(sugar::Infix("1+(2-3)*(3+5)") e); e.parse()` → `-7`）。

### 1. Hello, world / 你好，世界

[`examples/hello.syn`](./examples/hello.syn) — the smallest complete program:
最小的完整程序：

```
&io;
$Program {
    @:: << [() -> () {
        -(io::OStream out);
        out.push_line("Hello, Synth-OOP!");
    }];
}
```

```bash
$ ./build/synth examples/hello.syn      # Linux / macOS
$ build\synth.exe examples\hello.syn    # Windows (cmd)
Hello, Synth-OOP!
```

Three things to notice / 这里有三件事值得注意：

- A program **is** the construction of its `$Program` class — the entry point is
  the `@::` (construct) behavior, and there is **no global scope**.
  程序**就是** `$Program` 类的构造过程——入口是 `@::`（构造行为），**没有全局作用域**。
- `&io;` imports the standard I/O library; `-(io::OStream out)` declares a
  variable of type `io::OStream`. `&io;` 导入标准 IO 库；
  `-(io::OStream out)` 声明一个 `io::OStream` 类型的变量。
- `out.push_line(v)` writes `v` **plus a newline**; `out.push(v)` — or the flow
  form `out << v` — writes it **without** one.
  `out.push_line(v)` 写出 `v` **并换行**；`out.push(v)`（或流写法 `out << v`）**不换行**。

### 2. Values travel through flows / 值在「流」中流动

[`examples/flow.syn`](./examples/flow.syn) — there are **no infix operators**;
`a.+(b)` is an ordinary method call:
没有**中缀运算符**，`a.+(b)` 就是普通的方法调用：

```
&io;
&maths;
$Program {
    @:: << [() -> () {
        -(io::OStream out);

        -(std::Number radius) << 3;              // `<<` binds a value into the variable
        -(maths::Maths m);
        -(std::Number area) << m.pi().*(radius).*(radius);
        out.push_line(area);

        -(std::Number zero);                     // zero-value law / 零值法则
        out.push_line(zero);

        -(std::Number one) << 1;                 // IEEE 754 / IEEE 754 浮点语义
        out.push_line(one./(zero));
    }];
}
```

```bash
$ ./build/synth examples/flow.syn
28.274333882308138
0
Infinity
```

### 3. Classes hold state / 类持有状态

[`examples/counter.syn`](./examples/counter.syn) — instance methods are called
through `self.NAME(...)`; a bare `NAME(...)` means data:
实例方法经 `self.NAME(...)` 调用；裸写 `NAME(...)` 表示数据：

```
&io;
$Program {
    @:: << [() -> () {
        -(Counter c);
        c.inc();
        c.inc();
        c.inc();

        -(io::OStream out);
        out.push_line(c.get());
        out.push_line(c.describe());
    }];
}

$Counter {
    -(std::Number value);

    @get << [() ~> (result) {
        result << value;
    }];

    @inc << [() -> () {
        value << self.next_value();
    }];

    @next_value << [() ~> (result[std::Number]) {
        result << value.+(1);
    }];

    @describe << [() ~> (text[std::String]) {
        text << "Counter(value=".+(value.to_string()).+(")");
    }];
}
```

```bash
$ ./build/synth examples/counter.syn
3
Counter(value=3)
```

Note: `<<` **rebinds** a variable, it does **not** append — string building uses
`+` (with `to_string` for numbers).
注意：`<<` 是**重新绑定**变量而非追加——拼字符串要用 `+`（数字先 `to_string`）。

### 4. Where to go next / 接下来

```bash
# every example / 全部示例
for f in examples/*.syn; do echo "=== $f ==="; ./build/synth "$f"; done

# the language-philosophy cases / 语言哲学验证用例
for f in verify/philosophy/*.syn; do echo "=== $f ==="; ./build/synth "$f"; done

# the regression suites / 回归测试套件
./build/assert_lexer && ./build/assert_parser && ./build/assert_runtimes
```

- Language specification / 语言规范：[`doc/Synth-OOP语言文档-修正版.md`](./doc/Synth-OOP语言文档-修正版.md)
- Standard-library reference / 标准库参考：[`doc/Syclun标准库参考.md`](./doc/Syclun标准库参考.md)

---

## About the name / 关于 Syclun 这个名字

**Syclun** is short for **Sy**nth-OOP **I**nterpreter **M**ade **o**f **C**PP
**L**ang**u**age — an interpreter for the Synth-OOP language, written in C++.
It is pronounced /ˈsɪklən/ (roughly "SIK-lun", 「西克伦」).

**Syclun** 是 **Sy**nth-OOP **I**nterpreter **M**ade **o**f **C**PP
**L**ang**u**age 的简称——「用 C++ 写成的 Synth-OOP 解释器」，读作 /ˈsɪklən/
（近似「西克伦」）。

The letters are drawn from that phrase / 字母取自该短语：

| Letter | From / 来源 |
|--------|-------------|
| **Sy** | **Sy**nth-OOP — the language being implemented / 被实现的语言 |
| **c**  | **C**PP — i.e. C++, the implementation language / 即 C++，实现语言 |
| **l**  | **L**anguage / 语言 |
| **u**  | lang**u**age |
| **n**  | I**n**terpreter / 解释器 |

### Clarifications / 几点澄清

1. **Syclun is the interpreter, not the language.** The language is called
   **Synth-OOP** (also written *Synth OOP*); Syclun is this repository — one
   particular implementation of it. The language is defined by the specification
   in [`doc/`](./doc), independently of any implementation.
   **Syclun 是解释器，不是语言。** 语言叫 **Synth-OOP**（也写作 *Synth OOP*）；
   Syclun 是本仓库——它的一个具体实现。语言由 [`doc/`](./doc) 中的规范定义，与
   任何实现无关。

2. **CPP here means C++, not the C preprocessor.** The abbreviation *CPP* is
   more commonly used for the *C PreProcessor* (and for the `.cpp` file
   extension). In this project's name it stands for **C++** — the whole
   interpreter is C++23 and depends on nothing but the standard library.
   **此处的 CPP 指 C++，不是 C 预处理器。** 缩写 *CPP* 更常用于指 *C PreProcessor*
   （以及 `.cpp` 扩展名）。在本项目名中它代表 **C++**——整个解释器是 C++23，
   除标准库外无任何依赖。

3. **It is a blend, not a strict acronym.** Syclun is coined for
   pronounceability, so the letters trace back to the phrase but are not a
   literal one-letter-per-word initialism. Do not try to expand it backwards.
   **这是合词简称，不是严格首字母缩写。** Syclun 为便于发音而造，字母可追溯到该
   短语，但并非逐词取首字母，不必反向严格还原。

4. **`synth` ≠ Synth-OOP.** `synth` is the name of the **executable** (on
   Windows: `synth.exe`); **Synth-OOP** is the **language**. Synth-OOP source
   files use the extensions `.syn` (program), `.synl` (library interface),
   `.syni`.
   **`synth` 不等于 Synth-OOP。** `synth` 是**可执行文件**名（Windows 下为
   `synth.exe`）；**Synth-OOP** 是**语言**名。Synth-OOP 源文件扩展名为 `.syn`
   （程序）、`.synl`（库接口）、`.syni`。

5. **Syclun is a philosophy-verification vehicle.** It exists as much to test
   whether the language's design commitments actually hold up when run as it
   does to be a usable interpreter — hence the `verify/philosophy/` cases.
   **Syclun 同时是哲学验证载体。** 它的存在既是为了可用，也是为了检验语言的设计
   承诺在实际运行时是否真的成立——这就是 `verify/philosophy/` 用例的由来。

---

## At a glance / 项目亮点

- **Cross-platform by design.** One CMake build runs on Windows (MinGW-w64 /
  MSVC), Linux, macOS, and other Unix systems with GCC, Clang, or MSVC — no
  per-OS fork, no platform-specific Makefiles.
  **天生跨平台。** 一份 CMake 构建即可在 Windows（MinGW-w64 / MSVC）、Linux、
  macOS 及其它 Unix 上跑，使用 GCC、Clang 或 MSVC——无按平台分叉，无平台专属
  Makefile。

- **Zero third-party dependencies.** Pure C++23, header-only engine; the only
  requirement is the standard library shipped with your compiler. The hashing,
  regex, and data-structure code under `lib/cpp/` is original to this project.
  **零第三方依赖。** 纯 C++23、header-only 引擎；唯一要求是编译器自带的标准库。
  `lib/cpp/` 下的哈希、正则与数据结构实现均为本项目原创。

- **290 assertion tests, all green.** Three regression suites — lexer (83),
  parser (64), and runtime/object (143) — run through `ctest` and pass on every
  build. The `verify/philosophy/*.syn` cases additionally exercise each design
  commitment end to end.
  **290 条断言测试，全绿。** 三套回归套件——词法（83）、语法（64）、运行时/对象
  （143）——经 `ctest` 运行且每次构建全过。`verify/philosophy/*.syn` 用例还逐一
  端到端印证每条设计承诺。

- **Immediate, traceable errors.** Faults surface at their source with a g++-
  style diagnostic (`file:line:col`, source caret, full execution stack) — no
  silently-poisoned values, no opaque crashes. A failing run **exits non-zero**,
  so `synth bad.syn && cmd` never runs `cmd` and CI notices the failure.
  Arithmetic honours IEEE 754 (`Infinity` / `NaN`).
  **即时、可追踪的错误。** 故障在源头以类 g++ 诊断（文件:行:列、源码插入符、完整
  执行栈）浮现——无静默毒水值，无黑盒崩溃。失败的**以非 0 退出码结束**，故
  `synth bad.syn && cmd` 不会执行 `cmd`，CI 也能察觉失败。算术遵循 IEEE 754
  （`Infinity` / `NaN`）。

- **A real standard library.** Ten modules cover I/O, files, system, scalar
  math, hashing, data structures, regular expressions, an async runtime, a
  runtime checker, and a syntactic-sugar bag (`Infix`).
  **成体系的标准库。** 十个模块涵盖 I/O、文件、系统、标量数学、哈希、数据结构、
  正则、异步运行时、运行期校验，以及语法糖包（`Infix`）。

- **Open, governed, and yours.** GPL-3.0-or-later with a DCO sign-off process
  and a documented contribution workflow. Programs you *write* in Synth-OOP are
  independent works, not derivative of the interpreter.
  **开放、有治理、且属于你。** GPL-3.0-or-later，配 DCO 签署流程与成文贡献规范。
  你用 Synth-OOP **编写**的程序是独立作品，并非本解释器的演绎作品。

---

## Project layout / 目录结构

```
Syclun/
├── README.md                  # this file / 本说明
├── LICENSE                    # GPL-3.0-or-later / 许可证
├── DCO.md                     # Developer Certificate of Origin / 开发者原创证书
├── CONTRIBUTING.md            # PR format, self-check, rejection list / 贡献指南
├── .gitignore                 # ignore build artifacts / 忽略构建产物
├── .gitattributes             # pin LF for *.sh / *.syn / *.synl / 固定 LF 行尾
├── CMakeLists.txt             # the single source of truth for the build / 构建的唯一事实来源
├── .github/
│   ├── PULL_REQUEST_TEMPLATE.md   # PR title + body template / PR 标题与正文模板
│   ├── dco.yml                    # DCO app config (if installed) / DCO 应用配置（若安装）
│   └── workflows/
│       ├── dco.yml                # self-contained DCO check / 自包含 DCO 检查
│       └── release.yml            # builds + publishes the 5 packages / 构建并发布五个包
├── build.sh                   # one-shot build wrapper around CMake / CMake 的一键封装
├── package.sh                 # one-click release packager (5 platform distros) / 一键发布打包器（五平台）
├── cmake/                     # cross-compilation toolchain files / 交叉编译工具链文件
│   ├── toolchain-windows-arm64.cmake  # Windows ARM64 via aarch64-w64-mingw32 / Windows ARM64 交叉
│   └── toolchain-linux-x64.cmake      # Linux x64 via x86_64-linux-gnu / Linux x64 交叉
├── examples/                  # the 30-second tour: runnable .syn programs / 30 秒体验示例
│   ├── hello.syn              # the smallest complete program / 最小的完整程序
│   ├── flow.syn               # flows, zero values, IEEE 754 / 流、零值、浮点语义
│   └── counter.syn            # classes, state, self-calls / 类、状态、自调用
├── doc/                       # language documentation / 语言文档
│   ├── Synth-OOP语言文档-修正版.md            # specification v1.29 (Chinese) / 语言规范（中文）
│   ├── Synth-OOP-Language-Documentation-修正版.md  # same, in English / 同上，英文版
│   └── Syclun标准库参考.md                    # standard-library reference / 标准库参考
├── promo/                     # outreach material (not code) / 宣传物料（非代码）
│   └── reddit-r-programming.md  # r/programming post draft + playbook / Reddit 宣传贴与发帖策略
├── src/                       # interpreter engine (header modules + entry) / 解释器引擎
│   ├── exception_throw.hpp    # Thrower: fatal error → exit / 致命错误即退出
│   ├── lexer.hpp              # tokenizer (dumb by design) / 词法器（越笨越好）
│   ├── parser.hpp             # recursive-descent parser / 递归下降语法分析
│   ├── ast_dump.hpp           # AST pretty-printer (extracted debug util) / AST 打印
│   ├── runtime.hpp            # object model, capsules, Callable / 对象模型与胶囊
│   ├── builtin.hpp            # native objects: Number/Boolean/String/Array/... / 原生对象
│   ├── interpreter.hpp        # tree-walking evaluator / 树遍历求值器
│   └── main.cpp               # entry point: synth <program.syn> / 程序入口
├── lib/                       # standard libraries (*.synl interfaces + C++ backends) / 标准库
│   ├── file.synl              # `file` module → `File` class / file 模块，类为 File
│   ├── system.synl            # `system` module → `System` class (has .wait(ms)) / system 模块，类为 System
│   ├── structs.synl           # `structs` module → `Queue`/`Stack`/`Tree`/`Map`/`Graph` / 算法结构库
│   ├── re.synl                # `re` module → `Re`/`Pattern`/`Match` (regex) / 正则库
│   ├── maths.synl             # `maths` module → `Maths` class / maths 模块，类为 Maths
│   ├── async.synl             # `async` module → `Reactor`/`Task`/`Error` / async 模块
│   ├── hash.synl              # `hash` module → `Hash` class / hash 模块，类为 Hash
│   ├── io.synl                # `io` module → `OStream`/`IStream` / io 模块，类为 OStream/IStream
│   ├── sugar.synl             # `sugar` module → `Infix` class (arithmetic evaluator) / sugar 模块，类为 Infix
│   └── cpp/                   # C++ backends, arranged apart from the .synl files / C++ 底层
│       ├── std_libs.hpp       # aggregator: pulls in every C++-backed lib / 聚合头
│       ├── file.hpp           # C++ backend for the `File` class / File 类 C++ 底层
│       ├── system.hpp         # C++ backend for the `System` class / System 类 C++ 底层
│       ├── maths.hpp          # C++ backend for the `Maths` class / Maths 类 C++ 底层
│       ├── async.hpp          # C++ backend for `Reactor`/`Task`/`Error` / async 类 C++ 底层
│       ├── structs.hpp         # C++ backend for `Queue`/`Stack`/`Tree`/`Map`/`Graph` / 算法结构 C++ 底层
│       ├── re.hpp              # C++ backend for `Re`/`Pattern`/`Match` (regex) / 正则 C++ 底层
│       ├── hash.hpp           # C++ backend for the `Hash` class / Hash 类 C++ 底层
│       ├── io.hpp             # C++ backend for `OStream`/`IStream` / io 类 C++ 底层
│       └── sugar.hpp           # C++ backend for the `Infix` class / Infix 类 C++ 底层
├── vscode-synth-oop/          # VS Code extension: Synth-OOP syntax highlighting / VS Code 语法高亮插件
│   ├── package.json           # extension manifest (id, version, contributes) / 扩展清单
│   ├── language-configuration.json        # brackets, comments, auto-closing / 括号·注释·自动配对
│   ├── syntaxes/synth-oop.tmLanguage.json # TextMate grammar shared by .syn/.synl/.syni / TextMate 语法
│   ├── build_vsix.py          # offline .vsix packager (stdlib only, no vsce) / 离线打包脚本
│   ├── icons/synth-icon.png   # unified file icon / 统一文件图标
│   ├── examples/              # sample .syn files used by the extension / 插件示例文件
│   ├── LICENSE                # MIT — the extension only / MIT（仅限插件部分）
│   ├── README.md              # extension documentation / 插件说明文档
│   └── 打包教程.md             # package / install / publish / FAQ / 打包·安装·上架·常见问题
└── verify/                    # philosophy + unit verification / 哲学与单元测试
    ├── unit/                  # assertion suites (C++ drivers) / 断言套件
    │   ├── assert_lexer.cpp       # 83 lexical tests
    │   ├── assert_parser.cpp      # 64 syntax tests (uses parser_cli subprocess)
    │   ├── assert_runtimes.cpp    # 143 runtime/object tests
    │   └── parser_cli.cpp         # standalone parser CLI (helper for assert_parser)
    └── philosophy/            # language-philosophy examples (*.syn) / 哲学示例
        ├── d1_zero.syn            # zero-value law / 零值法则
        ├── d2_flow.syn            # flow << passing / 流传递
        ├── d3_instexpr.syn        # instantiation-as-expression / 实例化即表达式
        ├── counter.syn            # mutable state via behaviors / 行为维护状态
        ├── divide.syn             # multi-return destructuring / 多返回解构
        ├── if_control.syn        # control-flow builtins / 控制流内建
        ├── file_demo.syn          # `&file;` standard library / file 标准库
        ├── system_demo.syn        # `&system;` standard library / system 标准库
        ├── structs_demo.syn         # `&structs;` algorithm structures / 算法结构库
        ├── maths_demo.syn         # `&maths;` standard library / maths 标准库
        ├── hash_demo.syn          # `&hash;` standard library / hash 标准库
        ├── async_demo.syn         # `&async;` Reactor / Task / Error / async 反应堆
        ├── re_demo.syn            # `re` regular expressions / 正则库
        ├── void_demo.syn          # empty `()` = no value (`void` retired) / 空括号表示无值
        ├── tuple_first_demo.syn   # tuple-first destructuring / 元组首项解构
        ├── tuple_destructure_demo.syn # tuple destructuring as flow receiver / 元组解构作流接收方
        ├── constraint_demo.syn    # #Contract + parameter/variable constraints / 契约与约束
        ├── selfcall_demo.syn      # self-calls + `self` keyword / 类内自调用
        └── sugar_demo.syn         # `&sugar;` Infix arithmetic evaluator / sugar 算术求值器
```

The `verify/philosophy/*.syn` files are the **language-philosophy verification**
cases: each one asserts a design commitment of Synth-OOP by actually running it.
The `verify/unit/*.cpp` suites are the mechanical regression guards.

`verify/philosophy/*.syn` 是**语言哲学验证**用例：每个文件都通过实际运行来印证
Synth-OOP 的某条设计承诺。`verify/unit/*.cpp` 套件则是机械的回归防护。

---

## Packaging tutorial / 打包教程

> **Everything in this section is optional.** To simply *run* Synth-OOP,
> download a prebuilt package from the Releases page as described in the
> 30-second tour above. Come back here only if you want to build the
> interpreter from source or cut release packages yourself.
>
> **本节全部为可选内容。** 若只是想**运行** Synth-OOP，按上文「30 秒体验」从
> Releases 页下载预编译包即可。只有当你想从源码构建解释器、或自行制作发布包时，
> 才需要回到这里。

Syclun is built with **CMake** and is **cross-platform**: the same description
builds on Windows (MinGW-w64 / MSVC), Linux, macOS, and other Unix-like systems,
with incremental compilation handled by the generator (Make / Ninja / MSBuild).

Syclun 用 **CMake** 构建，且**跨平台**：同一份描述即可在 Windows（MinGW-w64 /
MSVC）、Linux、macOS 及其它 Unix 上构建，增量编译由生成器（Make / Ninja /
MSBuild）负责。

### Step 1 / 步骤 1 — Build from source / 从源码构建

**Requirements / 环境要求**

- **CMake ≥ 3.16**
- A **C++23-capable compiler** — any toolchain that accepts `-std=c++23` (or
  `-std=c++2b`): GCC, Clang, or MSVC (Visual Studio 2022) all qualify. The
  project is developed and verified with GCC / MinGW-w64.
  **支持 C++23 的编译器**——任何接受 `-std=c++23`（或 `-std=c++2b`）的工具链：
  GCC、Clang、MSVC（Visual Studio 2022）均可。本项目以 GCC / MinGW-w64 开发与
  验证。
- **Ninja** (optional, recommended — markedly faster for this header-only
  project); Make works too. / **Ninja**（可选，推荐——对本 header-only 项目快得多）；
  Make 同样可用。

**Build / 构建**

```bash
# One-shot convenience wrapper / 一键封装
bash build.sh              # configure (if needed) + build / 配置（如需）+ 构建
bash build.sh --test       # build, then run all ctest suites / 构建并跑全部套件
bash build.sh --debug      # Debug build / 调试构建
bash build.sh --clean      # wipe build/ and rebuild / 清空后重建

# Or drive CMake directly / 或直接用 CMake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure      # run all suites / 跑全部套件
```

This produces / 生成（在 Windows 下二进制带 `.exe` 后缀 / on Windows the
binaries carry a `.exe` suffix）：

| Binary | Purpose |
|--------|---------|
| `build/synth` (`synth.exe` on Windows) | the interpreter — run a Synth-OOP program / 解释器本体 |
| `build/parser_cli` (`parser_cli.exe`) | standalone parser (helper for assert_parser) / 解析子进程 |
| `build/assert_lexer` (`assert_lexer.exe`) | lexical assertion suite (83) / 词法断言 |
| `build/assert_parser` (`assert_parser.exe`) | syntax assertion suite (64) / 语法断言 |
| `build/assert_runtimes` (`assert_runtimes.exe`) | runtime assertion suite (143) / 运行时断言 |

---

### Step 2 / 步骤 2 — Package a distribution / 一键打包分发包

Step 1's `build.sh` is for **day-to-day compile & test**. When you want a
**ready-to-ship interpreter package** instead, use the separate one-click
packager:

步骤 1 的 `build.sh` 用于**日常编译与测试**。若要产出**可直接发布的解释器包**，请改用
相互独立的一键打包脚本：

```bash
bash package.sh                              # build & package every buildable target
PACKAGE_TARGETS=windows-x64 bash package.sh  # limit to a single target / 只打一个目标
```

It configures an optimised **Release** build of `synth` and assembles a
self-contained tree under `build/dist/` for each target it can actually build
on this host:

它会构建优化版 **Release** `synth`，并在 `build/dist/` 下为每个「本宿主能构建」的
目标组装出一棵自包含目录树：

```
build/dist/
├── PACKAGES.md                      # summary table: produced vs skipped
├── synth-windows-x64/               # on a Windows host
│   ├── bin/synth.exe  (+ MinGW DLLs)
│   ├── libs/*.synl                  # runtime library interfaces (NOT source)
│   ├── examples/
│   ├── README.md  LICENSE
├── synth-windows-arm64/  (needs aarch64-w64-mingw32)
├── synth-macos-x64/       (needs a macOS host)
├── synth-macos-arm64/     (needs a macOS host)
└── synth-linux-x64/       (needs a Linux host, or x86_64-linux-gnu cross)
```

**A single host can only build the targets matching its own OS/architecture.**
`package.sh` builds what it can and **skips the rest with a clear note**, so the
command always succeeds. To obtain all five packages from one trigger, let the CI
matrix build them (below).

**单一宿主只能构建与其自身 OS/架构匹配的目标。** `package.sh` 会构建能构建的、
**其余跳过并给出说明**，因此命令始终成功。要一次性拿到全部五个包，交给下面的
CI 矩阵即可。

The `libs/` directory holds only the **runtime artefacts** (`*.synl` interfaces
loaded by `&module;` at runtime); the C++ backends (`lib/cpp/*.hpp`) are already
compiled into the binary and are **not** shipped. The interpreter resolves its
libraries relative to the executable, and you can point it elsewhere with
`SYNTH_LIB_DIR`:

`libs/` 目录只含**运行时成品**（`&module;` 运行期载入的 `*.synl` 接口）；C++ 底层
（`lib/cpp/*.hpp`）已编译进二进制，故**不**随包发布。解释器依可执行文件位置解析
标准库，也可用 `SYNTH_LIB_DIR` 指向其它位置：

```bash
SYNTH_LIB_DIR=/opt/synth/libs bin/synth your-program.syn
```

### Step 3 / 步骤 3 — Release via CI / 通过 CI 发布

`.github/workflows/release.yml` is the CI path that produces **all five
packages** from a single trigger. A matrix runs `package.sh` on the runner that
can build each target (Windows x64 on `windows-latest`, Windows ARM64 via a
Linux cross, both macOS slices on `macos-latest`, Linux x64 on `ubuntu-latest`),
then publishes the five zips as a GitHub release on a tag push (or manually via
`workflow_dispatch`).

`.github/workflows/release.yml` 是「一次性产出全部五个包」的 CI 路径。矩阵把各目标
分散到能构建它的 runner 上各跑一次 `package.sh`（Windows x64 在 `windows-latest`、
Windows ARM64 经 Linux 交叉、两个 macOS 切片在 `macos-latest`、Linux x64 在
`ubuntu-latest`），随后在打 tag 推送时（或手动 `workflow_dispatch`）把五个 zip 作为
GitHub Release 附件发布。

> **Note:** `windows-arm64` is a **best-effort cross build**. Because it is
> produced away from a Windows host, its `bin/` may not carry the MinGW runtime
> DLLs that make a Windows package fully self-contained — treat it as
> experimental until it is built on real Windows/ARM64 hardware. The other four
> targets build natively and ship self-contained.
>
> **注意：** `windows-arm64` 为**尽力而为的交叉构建**。由于不在 Windows 宿主上产出，
> 其 `bin/` 未必带有让 Windows 包完全自包含的 MinGW 运行时 DLL——在真实 Windows
> ARM64 硬件上构建之前，请视其为实验性产物。其余四个目标均为原生构建且自包含。

### Step 4 / 步骤 4 — Package the VS Code extension / 打包 VS Code 插件

The syntax-highlighting extension lives in
[`vscode-synth-oop/`](./vscode-synth-oop/). It is packaged **offline** — the
script uses only the Python standard library (`zipfile`), with no `vsce` and no
network — into a standard VSIX v3 package:

语法高亮插件位于 [`vscode-synth-oop/`](./vscode-synth-oop/)。它**离线**打包：脚本只用
Python 标准库 `zipfile`，无需 `vsce`、无需联网，产出标准 VSIX v3 包：

```bash
python vscode-synth-oop/build_vsix.py     # use python3 if that is yours / 如系统是 python3 则改之
# -> build/synth-oop-<version>.vsix       (written to build/; the extension dir stays clean)
#                                          （产物写入 build/，扩展源目录保持干净）
```

The `.vsix` is a build artefact, so it is **not committed** to the repository —
`build/` is git-ignored. It is attached to every GitHub Release alongside the
five interpreter packages. Installing it, publishing to the Marketplace, and
troubleshooting are covered in
[`vscode-synth-oop/打包教程.md`](./vscode-synth-oop/打包教程.md).

`.vsix` 是构建产物，因此**不提交进仓库**（`build/` 已被 .gitignore 排除），而是随每个
GitHub Release 与五个解释器包一并附上。安装、上架 Marketplace 与常见问题见
[`vscode-synth-oop/打包教程.md`](./vscode-synth-oop/打包教程.md)。

---

## Run / 运行

```bash
./build/synth verify/philosophy/divide.syn       # Linux / macOS
build\synth.exe verify\philosophy\divide.syn      # Windows (cmd)
# → 3.3333333333333335
# → 1

./build/assert_lexer && ./build/assert_parser && ./build/assert_runtimes
```

Beyond `examples/`, `verify/philosophy/` holds one runnable program per language
feature (`file_demo.syn`, `structs_demo.syn`, `async_demo.syn`, `re_demo.syn`,
`constraint_demo.syn`, `selfcall_demo.syn`, `sugar_demo.syn`, …).
除 `examples/` 外，`verify/philosophy/` 每个语言特性配一个可运行程序
（`file_demo.syn`、`structs_demo.syn`、`async_demo.syn`、`re_demo.syn`、
`constraint_demo.syn`、`selfcall_demo.syn`、`sugar_demo.syn` 等）。

---

## Standard libraries / 标准库

Standard libraries live in the **`lib/`** directory and are loaded with the
import statement **`&module;`** (e.g. `&file;`, `&maths;`, `&async;`,
`&hash;`, `&structs;`, `&re;`, `&sugar;`). The interpreter resolves the module
name to `lib/<module>.synl`, defines any `$Class` it finds there, and — if a C++
backend is registered — brings the native class online first. A `$Program`
class inside a library is **ignored** (it must not become the program entry
point).

标准库位于 **`lib/`** 目录，通过导入语句 **`&module;`**（如 `&file;`、`&maths;`
`&async;`、`&hash;`、`&structs;`、`&re;`、`&sugar;`）载入。解释器将模块名解析为
`lib/<module>.synl`，定义其中出现的 `$Class`；若登记了 C++ 底层，则先上线原生类。
库中的 `$Program` 类**被忽略**（不得成为程序入口）。

### Layout / 文件布局

A library's two halves are kept in **two different places** on purpose:

- **`lib/<name>.synl`** — the human-readable interface (method signatures
  only, no body). This is ordinary Synth-OOP source the interpreter *parses
  but does not compile*; it documents the public shape of the class.
- **`lib/cpp/<name>.hpp`** — the C++ backend that supplies the real behavior.
  It self-registers via `rt_builtin::register_native_lib(name, init_fn)` so
  the engine never names it explicitly. The C++ backends are collected in
  **`lib/cpp/std_libs.hpp`**, which the interpreter includes exactly once.
  Keeping `.hpp` under `lib/cpp/` (not beside the `.synl`, not folded into
  `builtin.hpp`) is the load-bearing convention of this project.

一个标准库的两部分**刻意分置于两处**：

- **`lib/<name>.synl`** — 面向人类的接口（仅签名、无函数体）。这是解释器
  *解析但不编译*的普通 Synth-OOP 源码，仅用于记录类的公开形态。
- **`lib/cpp/<name>.hpp`** — 提供真实行为的 C++ 底层，经
  `rt_builtin::register_native_lib(name, init_fn)` 自注册，引擎无需显式
  指名。所有 C++ 底层汇总于 **`lib/cpp/std_libs.hpp`**，由解释器仅包含一次。
  把 `.hpp` 放在 `lib/cpp/`（而非与 `.synl` 并列、也非并入 `builtin.hpp`）
  是本项目的关键约定。

Two flavours of library coexist:

标准库有两种形态共存：

1. **C++-backed (`lib/cpp/<name>.hpp` + `lib/<name>.synl`).** Examples:
   `File`, `System`, `Maths`, `Reactor`(+`Task`/`Error`), `Hash`, `Queue`/
   `Stack`/`Tree`/`Map`/`Graph` (the `structs` module), `Re`/`Pattern`/`Match`
   (the `re` module), `Checker` (the `assert` module), `Infix` (the `sugar`
   module) — imported as `&file;`, `&system;`, `&maths;`, `&async;`, `&hash;`,
   `&structs;`, `&re;`, `&assert;`, `&sugar;`.
   **C++ 底层库**：例 `File`、`System`、`Maths`、`Reactor`(+`Task`/`Error`)、
   `Hash`、`structs`(Queue/Stack/Tree/Map/Graph)、`re`(Re/Pattern/Match)、
   `assert`(Checker)、`sugar`(Infix)（分别由 `&file;`/`system;`/`maths;`/`async;`
   /`hash;`/`structs;`/`re;`/`assert;`/`sugar;` 导入）。
2. **Pure-Synth-OOP (`lib/<name>.synl` only).** No such library currently
   ships — every standard library now has a C++ backend. The form remains
   supported if one is ever needed.
   **纯 Synth-OOP 库（仅 `<name>.synl`）**：当前未随附；所有标准库均带 C++
   底层，但该形态仍被支持。

The `&` statement is robust: importing a non-existent module (no `.synl` and
no registered C++ backend) is a silent no-op. Every shipped standard library
(`file`, `system`, `maths`, `async`, `hash`, `structs`, `re`, **`io`**,
**`assert`**, **`sugar`**) is a real C++-backed module with both a `.synl`
interface and a `.hpp` backend.

`&` 语句是稳健的：导入不存在的模块（无 `.synl` 且无登记的 C++ 底层）为静默空操作。
所有随附标准库（`file`/`system`/`maths`/`async`/`hash`/`structs`/`re`/**`io`**/**
`assert`**/**`sugar`**）均为带 `.synl` 接口与 `.hpp` 底层的真实 C++ 库。`&io;`
**不再特例塞进 `builtin.hpp`**，而是与余者一致地位于 `lib/cpp/io.hpp` +
`lib/io.synl`。`&sugar;` 同样位于 `lib/cpp/sugar.hpp` + `lib/sugar.synl`。

### Library modules / 库模块一览

Every library is imported with `&module;` (lower-case module name). The class
type is the module name **capitalized** and is referenced with the
library-qualified form `module::Class` (e.g. `&maths;` gives `Maths`, so
`-(maths::Maths m)`).

每个库用 `&module;`（模块名小写）导入；类名为模块名**首字母大写**，并采用库限定
形式 `module::Class`（如 `&maths;` 得到 `Maths` 类，故 `-(maths::Maths m)`）。

| Import | Classes / 类 | What it does / 用途 |
|--------|--------------|---------------------|
| `&io;` | `OStream`, `IStream` | standard streams / 标准输入输出流 |
| `&file;` | `File` | file read / write / append / 文件读写 |
| `&system;` | `System` | shell, env, time, `wait` / Shell、环境变量、时间 |
| `&maths;` | `Maths` | scalar math / 标量数学 |
| `&hash;` | `Hash` | `sha256` / `crc32` / `fnv1a` |
| `&structs;` | `Queue`, `Stack`, `Tree`, `Map`, `Graph` | data structures / 数据结构 |
| `&re;` | `Re`, `Pattern`, `Match` | regular expressions / 正则表达式 |
| `&async;` | `Reactor`, `Task`, `Error` | async runtime / 异步运行时 |
| `&assert;` | `Checker` | runtime legality checks / 运行期合法性校验 |
| `&sugar;` | `Infix` | arithmetic-expression evaluator (`1+(2-3)*(3+5)` → `-7`) / 算术表达式求值 |

**Detailed method signatures, the "how to add a standard library" checklist,
and the language-level semantics that these libraries rely on live in the
language documentation**, not here:

详细的**方法签名**、「如何新增标准库」清单，以及这些库所依赖的**语言层语义细则**
都在语言文档中，不在本文件：

- Standard-library reference / 标准库参考：[`doc/Syclun标准库参考.md`](./doc/Syclun标准库参考.md)
- Language semantics / 语言语义细则：`doc/Synth-OOP语言文档-修正版.md` 附录 E
- Full specification / 完整语言规范：[`doc/Synth-OOP语言文档-修正版.md`](./doc/Synth-OOP语言文档-修正版.md)（英文版 [`Synth-OOP-Language-Documentation-修正版.md`](./doc/Synth-OOP-Language-Documentation-修正版.md)）

---

## Editor support / 编辑器支持（VS Code 插件）

[`vscode-synth-oop/`](./vscode-synth-oop/) holds a **VS Code extension that
adds syntax highlighting for Synth-OOP**. It is pure TextMate grammar — it
highlights, but it neither compiles nor runs anything, so it never fights the
interpreter.

[`vscode-synth-oop/`](./vscode-synth-oop/) 是一个**为 Synth-OOP 提供语法高亮的
VS Code 插件**。它是纯 TextMate 语法——只做高亮，不编译也不运行，因此绝不会与解释器
冲突。

It recognises all three Synth-OOP file kinds / 它识别全部三种 Synth-OOP 文件：

| Extension | Purpose / 用途 |
|-----------|----------------|
| `.syn`  | program source, with the `$Program` entry / 程序源码，含 `$Program` 入口 |
| `.synl` | standard-library interface (signatures only) / 标准库接口（仅签名） |
| `.syni` | Synth-OOP interface / declaration files / 接口与声明文件 |

Highlighted tokens include / 高亮覆盖：the `@ $ # !` marker keywords and their
combinations, module imports (`&io;`), control methods (`if_`, `while_`,
`repeat_`), arrows and flow operators (`=> ~> -> << >> =: :=`), boolean and
non-finite literals (`true` / `false` / `inf` / `NaN` / `Infinity`), class and
type names (`$Program`, `std::Number`, `maths::Maths`, `io::OStream`), method
names, and variables.

高亮包括：`@ $ # !` 标记关键字及其组合、模块导入（`&io;`）、控制方法（`if_`、
`while_`、`repeat_`）、箭头与流运算符（`=> ~> -> << >> =: :=`）、布尔与非有限字面量
（`true` / `false` / `inf` / `NaN` / `Infinity`）、类名与类型名（`$Program`、
`std::Number`、`maths::Maths`、`io::OStream`）、方法名与变量名。

**Getting it / 获取方式**

- **From a release (recommended / 推荐):** every GitHub Release attaches
  `synth-oop-<version>.vsix` next to the five interpreter packages. Download it
  and use **Extensions → `···` → Install from VSIX…**.
  每个 GitHub Release 都会在五个解释器包旁附上 `synth-oop-<版本>.vsix`，下载后用
  **「扩展」→ `···` → Install from VSIX…** 安装即可。
- **From source / 从源码：** see Step 4 of the packaging tutorial above, or
  [`vscode-synth-oop/打包教程.md`](./vscode-synth-oop/打包教程.md) for installing,
  publishing to the Marketplace, and troubleshooting.
  见上文打包教程的步骤 4，或查阅
  [`vscode-synth-oop/打包教程.md`](./vscode-synth-oop/打包教程.md)（安装、上架
  Marketplace 与常见问题）。

Colours come entirely from your active VS Code theme: the extension emits only
standard TextMate scopes and hard-codes no colours.
配色完全来自你当前的 VS Code 主题——插件只输出标准 TextMate 作用域，不写死任何颜色。

> The extension is licensed **MIT** — a deliberate exception to the project's
> GPL-3.0 licensing — so you may freely adapt the grammar.
> 该插件采用 **MIT** 许可，是本项目 GPL-3.0 许可的刻意例外，你可自由改写其语法文件。

---

## Design notes / 设计要点

- **No global scope.** The program entry is the `$Program` class's `@::`
  (construct) behavior; instantiation of `$Program` *is* the program lifecycle.
  / 无全局作用域。程序入口是 `$Program` 类的 `@::`（构造行为），实例化
  `$Program` 即程序的整个生命周期。

- **Everything is an object; no infix operators.** `a.+(b)` is a method call.
  Values travel through **flows** (`<<` / `>>`), published via `=:` and received
  via `:=`. / 一切皆对象，无中缀运算符。`a.+(b)` 是方法调用。值通过**流**
  （`<<`/`>>`）传递，由 `=:` 公布、`:=` 接收。

- **Zero-value law.** A freshly created instance carries a default `#value`
  capsule (e.g. `Number` → `0`), so `out << (make Number)` prints `0`.
  / 零值法则：新造实例自带缺省 `#value` 胶囊（`Number`→`0`），故
  `out << (make Number)` 输出 `0`。

- **Behaviors are closures.** Inline behavior literals capture the caller's
  scope, so branches read caller variables (spec C.5).
  / 行为即闭包：内联行为字面量捕获调用方作用域，分支可读到调用方变量（规范 C.5）。

See `doc/Synth-OOP语言文档-修正版.md` for the full specification.
完整规范见 `doc/Synth-OOP语言文档-修正版.md`。

---

## Contributing / 如何贡献

See [`CONTRIBUTING.md`](./CONTRIBUTING.md) for the PR title/body format, the
self-check list, and the full rejection list. Two rules are worth knowing before
you write a line of code:

贡献方式见 [`CONTRIBUTING.md`](./CONTRIBUTING.md)：PR 标题与正文格式、提交自检
清单、完整拒收清单。动手之前有两条规则值得先知道：

1. **After 1.0.0, the syntax is effectively frozen** — a syntax change needs a
   necessity argument, not a preference.
   **1.0.0 之后语法基本冻结**——语法改动需要「必要性」论证，而非偏好。
2. **Do not invent a new standard-library name** if the functionality fits an
   existing module (`maths`, `structs`, `file`, `system`, `re`, `hash`, `io`,
   `async`, `assert`, `sugar`). Such a PR is sent back.
   **别新开标准库名**——若功能可归入现有模块（`maths`、`structs`、`file`、
   `system`、`re`、`hash`、`io`、`async`、`assert`、`sugar`），就该归入。此类
   PR 会被打回。

Contributions are accepted under the **Developer Certificate of Origin**
([`DCO.md`](./DCO.md)) rather than a CLA: every commit must carry a
`Signed-off-by` trailer, which `git commit -s` adds for you.

外部贡献以 **DCO（开发者原创证书）**（[`DCO.md`](./DCO.md)）接受，而非 CLA：每个
提交必须带 `Signed-off-by` 尾注，`git commit -s` 会自动为你加上。

---

## License / 许可证

```
Syclun — Synth-OOP Interpreter & Philosophy Verification
Copyright (C) 2026 VP_xudon

SPDX-License-Identifier: GPL-3.0-or-later
```

This project is free software, licensed under the **GNU General Public License
version 3 or (at your option) any later version**. The full text is in
[`LICENSE`](./LICENSE); it is the verbatim GPLv3 plus a project notice that
spells out the points below.

本项目为自由软件，采用 **GNU 通用公共许可证第 3 版或（依你选择）任何后续版本**
授权。完整文本见 [`LICENSE`](./LICENSE)：该文件为未经改动的 GPLv3 原文，后附本
项目的补充说明，其中包含下列要点。

| Path / 路径 | License / 许可证 |
|-------------|------------------|
| `src/`, `lib/`, `build.sh`, `examples/`, `verify/`, `doc/`, `README.md` | **GPL-3.0-or-later** |
| `vscode-synth-oop/` | **MIT** — separate, independently distributed work, with its own `LICENSE` / 独立分发作品，自带 `LICENSE` |

**Your Synth-OOP programs are yours.** Source files written in Synth-OOP
(`.syn`, `.synl`, `.syni`, …) and any output produced by running them are
**independent works**, not derivative works of this interpreter. Using the
standard libraries in `lib/` through the normal publish/receive flow (`=:`,
`:=`, `<<`) does not change that — the same way a C program compiled with GCC
is not a derivative work of GCC. You may write, run, and distribute your
Synth-OOP programs under any license you like, including proprietary terms.
This is a clarification of GPLv3 section 2, not an extra restriction.

**你写的 Synth-OOP 程序归你自己。** 用 Synth-OOP 编写的源文件（`.syn`、`.synl`、
`.syni` 等）以及运行它们产生的任何输出都是**独立作品**，不是本解释器的演绎作品。
通过常规公布/接收流（`=:`、`:=`、`<<`）使用 `lib/` 下的标准库不会改变这一点——
正如用 GCC 编译出的 C 程序并非 GCC 的演绎作品。你可以以任意许可证（包括专有条款）
编写、运行与分发你的 Synth-OOP 程序。本条系对 GPLv3 第 2 节的澄清，而非附加限制。

**No third-party source code.** The project depends only on the C++23 standard
library shipped with your compiler (GCC, Clang, or MSVC). The hashing, regex,
and data-structure implementations under `lib/cpp/` are original to this
project. / **无第三方源码**：项目仅依赖编译器自带的 C++23 标准库（GCC、Clang 或
MSVC）；`lib/cpp/` 下的哈希、正则与数据结构实现均为本项目原创。

**New source files** under `src/` and `lib/cpp/` should carry this header:
/ 在 `src/` 与 `lib/cpp/` 下**新增源文件**请保留如下声明头：

```cpp
// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.
```
