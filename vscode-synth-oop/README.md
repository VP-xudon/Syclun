# Synth-OOP — VSCode 语法高亮插件

为 Synth-OOP 语言提供语法高亮的 VSCode 扩展，覆盖文档
`doc/Synth-OOP-Language-Documentation-修正版.md`（v1.29）中描述的所有语法特性。

支持的文件扩展名：

- `.syn` — 程序源码（含 `$Program` 入口）
- `.synl` — 标准库接口文件（方法仅签名，行为由 C++ 底层提供）
- `.syni` — Synth-OOP 接口 / 声明文件（与以上共享同一套语法）

三个扩展名共用同一套 TextMate 语法（`source.synth-oop`），并在资源管理器中显示统一文件图标。

## 已高亮的语法元素

| 类别 | 示例 | 标准作用域（实际颜色由你的 VSCode 主题决定） |
| ---- | ---- | ---- |
| 关键字标记 | `@` `$` `#` `!` 及组合 `@!` `@#` `@!#` | `keyword` |
| 模块导入 | `&io;` `&maths;` | `keyword` |
| 控制方法 | `if_` `while_` `repeat_` | `keyword.control` |
| 箭头 / 流 / 赋值运算符 | `=> ~> -> << >> =: :=` 及 `+ - * / % < >` | `keyword.operator` |
| 布尔 / 非有限字面量 | `true` `false` `inf` `NaN` `Infinity` | `constant.language.boolean` |
| **类名 / 类型**（含 `$` 后、命名空间、库限定） | `$Program` `#Comparable` `std::Number` `maths::Maths` `io::OStream` `std::Number!` | `entity.name.type` / `support.type` |
| **方法名** | `get` `inc` `+(` `>(` `::` `=:` | `entity.name.function`（跟随主题的函数色） |
| **变量 / 其他对象**（可含数字） | `out` `age` `n` `sha2a3` `hashValue42` → 整体一个 token | `variable` |
| 占位符 | `-(std::Number q, _)` 中的 `_` | `variable` |
| 行/块注释 | `// ...`、`/* ... */` | `comment` |
| 字符串 | `"Hello"`（含 `\"` 等转义） | `string` |
| 数字 | `10`、`3.9` | `constant.numeric` |

> **配色说明**
> - 本插件**不写死任何颜色**，全部采用 VSCode 标准 TextMate 作用域
>   （`keyword` / `entity.name.type` / `support.type` / `entity.name.function` / `variable` /
>   `constant` / `string` / `comment` / `constant.numeric` / `keyword.operator`）。
>   每个元素最终显示的色相来自你当前激活的**主题**（如 Light+/Dark+），即 VSCode 的
>   「关键字色 / 类型色 / 函数色 / 变量色 / 常量色」那几类标准色——**所有语法元素都跟随主题样式**。
> - 这些标准作用域在所有内置及主流主题中都有对应配色；切换主题即可整体换色。
> - `void` 不再标红，作为普通 `variable` 着色。
> - **标识符可包含数字**：`sha2a3`、`hashValue42` 等整体作为一个 token 高亮，不再断开。
> - 若想微调某类颜色，直接在 `settings.json` 的 `editor.tokenColorCustomizations.textMateRules`
>   里针对上表作用域覆盖即可（无需改动本插件）。

## 打包与安装

打包产物统一输出到项目根目录的 **`build/`**（`synth-oop-<版本>.vsix`），详见 `打包教程.md`。

### 步骤一：打包（离线，无需联网）
```bash
python "V:/SynthOOP/Syclun/vscode-synth-oop/build_vsix.py"
# 产物：V:/SynthOOP/Syclun/build/synth-oop-1.3.5.vsix
```
脚本按 VSIX v3 规范打包，文件名随 `package.json` 的 `version` 自动变化。

### 步骤二：安装
- **从 VSIX**：扩展面板 `···` → *Install from VSIX…* → 选择
  `build/synth-oop-1.3.5.vsix` → 重新加载窗口。
- **命令行**：`code --install-extension "V:/SynthOOP/Syclun/build/synth-oop-1.3.5.vsix"`
- **扩展开发宿主（调试）**：用 VSCode 打开本目录，按 `F5`（Run Extension），
  打开任意 `.syn` / `.synl` / `.syni` 文件即见高亮。
- **手动**：把本目录整体复制到 `~/.vscode/extensions/synth-oop`
  （Windows：`%USERPROFILE%\.vscode\extensions\synth-oop`）并重启 VSCode。

## 测试样例
见 `examples/demo.syn`，它集中展示了上述各类语法，可直接打开验证配色。

## 目录结构
```
vscode-synth-oop/
├── package.json
├── language-configuration.json
├── syntaxes/
│   └── synth-oop.tmLanguage.json   # TextMate 语法
├── examples/
│   └── demo.syn
├── .vscode/
│   └── launch.json                 # F5 调试配置
├── icons/
│   └── synth-icon.png                # 文件图标 + 插件市场图标（.syn/.synl/.syni 共用）
├── LICENSE                          # MIT 许可证
├── 打包教程.md                       # 打包教程（中文）
└── README.md
```

> 打包产物（`.vsix`）统一输出到项目根目录的 **`build/`**，扩展目录内不再保留 `.vsix`。
> 打包与安装步骤见 `打包教程.md`，无需改动本目录即可重新生成。

---

## English Overview

**Synth-OOP** is a Visual Studio Code extension that provides syntax highlighting
for the Synth-OOP language across three file types:

- `.syn` — program source (with the `$Program` entry point)
- `.synl` — standard-library interface files (method signatures only; behavior is supplied by the C++ layer)
- `.syni` — Synth-OOP interface / declaration files (shares the same grammar)

All three extensions use one TextMate grammar (`source.synth-oop`) and a shared
file icon in the Explorer.

### Highlighted elements

| Category | Examples | Standard TextMate scope (color comes from your theme) |
| -------- | -------- | ----------------------------------------------------- |
| Keyword markers | `@` `$` `#` `!` and combos `@!` `@#` `@!#` | `keyword` |
| Module import | `&io;` `&maths;` | `keyword` |
| Control methods | `if_` `while_` `repeat_` | `keyword.control` |
| Arrow / stream / assignment operators | `=> ~> -> << >> =: :=` and `+ - * / % < >` | `keyword.operator` |
| Boolean / non-finite literals | `true` `false` `inf` `NaN` `Infinity` | `constant.language.boolean` |
| Class / type names (incl. `$`-prefixed, namespaces, libs) | `$Program` `#Comparable` `std::Number` `maths::Maths` `io::OStream` `std::Number!` | `entity.name.type` / `support.type` |
| Method names | `get` `inc` `+(` `>(` `::` `=:` | `entity.name.function` (theme function color) |
| Variables / other names (digits allowed) | `out` `age` `n` `sha2a3` `hashValue42` (one token) | `variable` |
| Placeholder | `_` in `-(std::Number q, _)` | `variable` |
| Line / block comments | `// ...`, `/* ... */` | `comment` |
| Strings | `"Hello"` (with `\"` escapes) | `string` |
| Numbers | `10`, `3.9` | `constant.numeric` |

> **Coloring note:** this extension does **not** hard-code any colors. It uses
> only standard VSCode TextMate scopes, so every element is colored by your active
> theme (Light+, Dark+, etc.). Switch themes to recolor everything at once.

### Install

Packaging produces a `.vsix` under the project root **`build/`** directory
(`build/synth-oop-<version>.vsix`). To install:

- **From VSIX:** Extensions panel `···` → *Install from VSIX…* → select
  `build/synth-oop-1.3.5.vsix` → reload the window.
- **Debug host:** open this folder in VSCode, press `F5` (Run Extension), then open
  any `.syn` / `.synl` / `.syni` file.
- **Manual:** copy this folder to `~/.vscode/extensions/synth-oop` (or
  `%USERPROFILE%\.vscode\extensions\synth-oop` on Windows) and restart VSCode.

Publisher: **VP_xudon** · License: MIT. See `打包教程.md` (Chinese) for the full
guide, including how to publish to the VS Code Marketplace.
