# Packaging tutorial / 打包教程

> **Everything in this document is optional.** To simply *run* Synth-OOP,
> download a prebuilt package from the Releases page as described in the
> README's 30-second tour. Come here only if you want to build the
> interpreter from source or cut release packages yourself.
>
> **本文全部为可选内容。** 若只是想**运行** Synth-OOP，按 README「30 秒体验」从
> Releases 页下载预编译包即可。只有当你想从源码构建解释器、或自行制作发布包时，
> 才需要读本文件。


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
