# Packaging
## 打包与发布 / Packaging & Release

> 本文**全部可选**。只想运行 Synth-OOP？去 [Quick Start](Quick-Start) 下载预编译包即可。只有想从源码构建、自行打包、或上架插件时才需要读。
> Everything here is **optional**. Just want to run it? Grab a prebuilt package in [Quick Start](Quick-Start). Read this only to build from source, cut packages, or ship the extension.

---

## 1. 从源码构建 / Build from source

**要求 / requirements**：CMake ≥ 3.16；支持 C++23 的编译器（GCC / Clang / MSVC 2022）；Ninja 可选（推荐）。

```bash
bash build.sh                 # 配置（如需）+ 构建 / configure + build
bash build.sh --test          # 构建并跑全部 ctest 套件
bash build.sh --debug         # 调试构建
bash build.sh --clean         # 清空后重建

# 或直接用 CMake / or drive CMake directly
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

产出 / output：

| 二进制 | 用途 |
|--------|------|
| `build/synth`（`synth.exe`） | 解释器本体 |
| `build/assert_lexer` / `assert_parser` / `assert_runtimes` | 三套断言套件（83 / 71 / 158） |

---

## 2. 一键打包分发包 / Package a distribution

日常编译用 `build.sh`；要产出**可直接发布的解释器包**，用独立的打包脚本：
`build.sh` is for daily compile; for a *ready-to-ship* package, use the separate packager:

```bash
bash package.sh                                # 构建本宿主能构建的全部目标
PACKAGE_TARGETS=windows-x64 bash package.sh    # 只打一个目标 / limit to one target
```

它在 `build/dist/` 下为每个「本宿主能构建」的目标组装一棵自包含目录树：

```
build/dist/
├── PACKAGES.md
├── synth-windows-x64/      # bin/synth.exe (+ MinGW DLLs), libs/, examples/, README, LICENSE
├── synth-windows-arm64/    # 需 aarch64-w64-mingw32 交叉
├── synth-macos-x64/        # 需 macOS 宿主
├── synth-macos-arm64/      # 需 macOS 宿主
└── synth-linux-x64/        # 需 Linux 宿主（或 x86_64-linux-gnu 交叉）
```

- **单一宿主只能构建与其 OS/架构匹配的目标**；其余 `package.sh` 会跳过并说明，命令始终成功。要一次拿全五个包，交给下面 CI 矩阵。
- `libs/` 只含运行时成品（`*.synl` 接口）；C++ 底层已编译进二进制，不随包发布。
- 可用 `SYNTH_LIB_DIR=/opt/synth/libs bin/synth x.syn` 指向其它库目录。

> `windows-arm64` 为**尽力而为的交叉构建**，其 `bin/` 未必带 MinGW 运行时 DLL，暂视为实验性。其余四个均为原生、自包含。

---

## 3. 通过 CI 发布 / Release via CI

`.github/workflows/release.yml` 是「一次性产出全部五个包」的 CI 路径：
- `verify` 作业先跑**全部**断言套件作为发布门禁；`all_passed ≠ 1` 时**记录缺陷并拒绝发布**（`allow_defects=true` 可降级为警告）。
- 矩阵在能构建各目标的 runner 上各跑一次 `package.sh`，随后把五个 zip + `synth-oop-<ver>.vsix` 作为 GitHub Release 附件发布（打 tag 推送时，或手动 `workflow_dispatch`）。
- Release 说明由 `gen_release_notes.sh` **格式化生成**（版本/日期/验证表/产物表/变更日志），不再手写。

另：`ci.yml` 在**每次 push/PR** 构建并跑全部断言，任一失败即阻断。
Also: `ci.yml` builds and runs **all** assertions on **every push/PR**; any failure blocks.

---

## 4. 打包 VS Code 插件 / Package the VS Code extension

[`vscode-synth-oop/`](https://github.com/VP-xudon/Syclun/tree/main/vscode-synth-oop) 是纯 TextMate 语法的语法高亮插件（只高亮，不编译不运行）。**离线**打包（仅用 Python 标准库 `zipfile`，无需 `vsce`、无需联网）：
The syntax-highlighting extension is packaged **offline** (Python stdlib `zipfile` only, no `vsce`, no network):

```bash
python vscode-synth-oop/build_vsix.py     # → build/synth-oop-<version>.vsix
```

`.vsix` 是构建产物，不提交进仓库（`build/` 已 git-ignore），随每个 Release 附上。安装/上架/排错见 [`vscode-synth-oop/打包教程.md`](https://github.com/VP-xudon/Syclun/blob/main/vscode-synth-oop/打包教程.md)。

---

👉 贡献改动请先看：[贡献指南 / Contributing](Contributing)
