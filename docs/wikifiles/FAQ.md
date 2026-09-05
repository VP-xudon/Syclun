# FAQ
## 常见问题

> 新手最常问的「为什么」「怎么装」「怎么贡献」。没找到答案？开个 issue。
> The most-asked "why / how do I install / how do I contribute". Not covered? Open an issue.

---

### Q1. 这语言和解释器分别叫什么？/ What are the names?
- **Syclun** = **Sy**nth-OOP **I**nterpreter **M**ade **o**f **C**PP **L**ang**u**age，读作 /ˈsɪklən/（「西克伦」）。它是**解释器**，不是语言。
- **Synth-OOP** 是**语言**名。源码扩展名：`.syn`（程序）、`.synl`（库接口）、`.syni`（接口/声明）。
- `synth` 是**可执行文件名**（Windows 下 `synth.exe`）；别和语言名混淆。

### Q2. 为什么没有 `+` `-` `*` 这些运算符？/ Why no operators?
因为「数据在对象之间流动」是第一性原理。连加法都是方法：`a.+(b)`；赋值是流 `a << b`。这让「流动」「活对象」「可追溯错误」成为语言底色。详见 [设计理念 / Design Philosophy](Design-Philosophy)。

### Q3. 怎么最快跑起来？/ Fastest way to run it?
下载 [Release](https://github.com/VP-xudon/Syclun/releases) 里的预编译包解压即用；或一行 `bash build.sh` 后 `./build/synth examples/hello.syn`。见 [快速上手 / Quick Start](Quick-Start)。

### Q4. 为什么 `a = 5` 报错？/ Why does `a = 5` fail?
`=` **不是**运算符，中缀 `a = b` 非法。用 `-(std::Number a) << 5;` 或 `a << 5;`。也可显式 `a.=(5)`，与 `a << 5` 等价。

### Q5. 变量是「盒子」吗？/ Is a variable a box?
不是。名字只是对象的**访问接口**；`a << 5` 是把 5 流进 a 这个对象本身，而非让 a 改指向别处。语言也没有「类型」——声明处的类名是塑造对象的模板，不是对象终身标签。

### Q6. 对象和类是活的吗？/ Are objects alive?
构造之后仍是活的：可运行期注入方法（`obj:@m << [{…}]`）、加私有属性（`obj:-(T v) << init`）、用 `obj.#()` 永久冻结。冻结后永不可解冻。见 [基本语法 §9 / Basic Syntax §9](Basic-Syntax)。

### Q7. 怎么用标准库？/ How do I use a library?
在文件顶部 `&模块;` 导入，例如 `&io;` `&maths;` `&re;`。导入后类（如 `maths::Maths`）即可用；`io` 还会随带常数对象 `io::out`/`io::in`。见 [标准库总览 / Standard Libraries](Standard-Libraries) 与 [对象使用 / Using StdLib Objects](Standard-Library-Objects)。

### Q8. 我写的 `.syn` 程序受 GPL 约束吗？/ Are my .syn programs under GPL?
**不受**。你用 Synth-OOP 编写的程序（`.syn`/`.synl`/`.syni` 及其输出）是**独立作品**，不是本解释器的演绎作品，可自行选择任何许可证（含专有）。仅本仓库源码为 GPL-3.0-or-later；VS Code 插件为 MIT。
**No.** Your Synth-OOP programs are *independent works*, not derivatives of the interpreter. Only this repo's source is GPL-3.0-or-later; the VS Code extension is MIT.

### Q9. 怎么贡献？语法能改吗？/ How do I contribute? Can I change the syntax?
欢迎！但记住两条硬规则：① **1.0.0 之后语法基本冻结**（目前无 1.0.0 标签，窗口仍开，但越触及语言越应先开 issue）；② **别轻易新开库名**。详见 [贡献指南 / Contributing](Contributing)。

### Q10. 下标从几开始？`void` 还能用吗？/ Indexing? Is `void` allowed?
- 下标从 **0** 开始（`arr.get(0)` 是首个元素）。
- `void` **已退役**：用空括号 `()` 表示「无值」，写 `void` 是语法错误。
- `Array` ↔ `Tuple` 在运行期互通（都是通用容器胶囊）。

### Q11. 错误为什么会像编译器？/ Why do errors look like a compiler's?
故障在**源头**抛出，以 `文件:行:列: error: 类型: 消息` + `^~~~` 插入符 + 完整执行栈报告；致命错误退出码 `1`，所以 `synth bad.syn && cmd` 不会执行 `cmd`。算术按 IEEE 754（`1/0`→`Infinity`，不中断）。

### Q12. 怎么自己打一个分发包？/ How do I package a dist myself?
`bash package.sh` 即可在本机产出当前平台的自包含包（此前没有这种一键能力）。全五平台交给 CI 矩阵。见 [打包与发布 / Packaging](Packaging)。

---

👉 还卡住？回到 [首页 / Home](Home) 选一页继续读。
