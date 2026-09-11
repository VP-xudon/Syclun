# Standard Library Objects
## 标准库对象使用

> 这是每个标准库模块的「能跑的示例 + 方法清单」。每个示例都在仓库 `verify/philosophy/*.syn` 里实跑过、输出已核对。复制即可运行（记得在文件顶部 `&module;`）。
> Runnable examples + a method cheat-sheet for every module. Every snippet is taken from a `verify/philosophy/*.syn` demo and its output is verified. Copy-paste to run (add `&module;` at the top).

约定 / Conventions:
- 多数示例借用 `io::OStream out;` 然后 `out << "标签 "; out << 值;` 这种「先打标签、再打结果」的写法——它是 `out << value` 流风格（不换行），多个 `<<` 串起来就成一行。
- Most examples declare `-(io::OStream out);` then do `out << "label "; out << value;` — the `out << value` flow style (no newline); chained `<<` stays on one line.

---

## io — 标准输入输出流 / Standard streams

`&io;` 随带两个**常数**对象：`io::out`（写）、`io::in`（读）。它们「生来就在」，不需要你声明。
`&io;` ships two **const** objects: `io::out` (write), `io::in` (read). They "just exist" — you never declare them.

```text
&io;
$Program {
    @::[() -> () {
        -(io::OStream out);

        // 流风格（不换行）/ flow style, no newline
        out << "flow: ";
        out << "Hello";

        // 方法风格（不换行）/ method style, no newline
        out.push(" | push: ");
        out.push("World");

        // 方法风格 + 换行 / method style WITH newline
        out.push_line(" | push_line: done");
    }];
}
```
**输出 / Output:** `flow: Hello | push: World | push_line: done`

- `OStream`：`out << value`（= `out.push(value)`，不换行）、`out.push_line(value)`（自带换行）。
- `IStream`：`x << io::in`（= `x =: io::in` = `x << io::in.get()`，读一个词）、`io::in.get_line()`（读一整行）。读到 EOF 抛 `RuntimeException: input stream ended`。

```text
&io;
$Program {
    @::[() -> () {
        -(io::OStream out);
        -(std::String name) << io::in.get();        // 读一个词 / one word
        out << "你输入了: ";
        out << name;
    }];
}
```
> 输入示例：在终端运行时，键入 `Ada` 回车，`name` 得到 `"Ada"`。
> Input example: when run in a terminal, type `Ada` and press enter; `name` becomes `"Ada"`.

---

## maths — 标量数学 / Scalar math

两种用法都行：①用预置对象 `maths::math`（无状态，推荐）；②自己 `-(maths::Maths m)` 实例化。
Both styles work: ① use the preset `maths::math` (stateless, recommended); ② instantiate `-(maths::Maths m)`.

```text
&io; &maths;
$Program {
    @::[() -> () {
        -(io::OStream out);
        -(std::Number r) << maths::math.sqrt(16);
        out << "sqrt(16) = "; out << r;            // 4
        out << " pi = "; out << maths::math.pi();   // 3.14159...
        out << " cos(0) = "; out << maths::math.cos(0);  // 1
        out << " floor(3.9) = "; out << maths::math.floor(3.9);  // 3
        out << " ceil(3.1) = "; out << maths::math.ceil(3.1);   // 4
        out << " min(7,3) = "; out << maths::math.min(7, 3);    // 3
        out << " e = "; out << maths::math.e();      // 2.71828...
    }];
}
```

| 方法 / Method | 含义 / Meaning |
|---|---|
| `abs(x)` `sqrt(x)` `pow(b,e)` `exp(x)` | 绝对值 / 开方 / 幂 / eˣ |
| `floor(x)` `ceil(x)` `round(x)` | 向下 / 向上 / 四舍五入取整 |
| `sin(x)` `cos(x)` `tan(x)` | 三角函数（弧度） |
| `log(x)` `log10(x)` | 自然对数 / 常用对数 |
| `mod(a,b)` `min(a,b)` `max(a,b)` | 取模 / 最小 / 最大 |
| `pi()` `e()` | 常数 π / e |
| `random()` | `[0,1)` 随机数 |

所有参数为 `std::Number`，返回 `std::Number`。三角函数用**弧度**。
All arguments and returns are `std::Number`. Trig functions take **radians**.

---

## file — 文件读写 / Files

`File` 不是预置对象，要先 `-(file::File f)` 实例化。`write`/`append` 用**二进制模式**打开，`\n` 在各平台原样保留（Windows 下不转 CRLF）。
`File` is not a preset — instantiate it first with `-(file::File f)`. `write`/`append` open in **binary mode**, so `\n` is preserved verbatim on every platform (no CRLF translation on Windows).

```text
&io; &file;
$Program {
    @::[() -> () {
        -(io::OStream out);
        -(file::File f);
        f.open("build/_demo_out.txt");
        f.write("Hello from file lib!");
        out << f.read();                 // Hello from file lib!
        out << " size="; out << f.size();
    }];
}
```
**输出 / Output:** `Hello from file lib! size=21`

| 方法 / Method | 含义 / Meaning |
|---|---|
| `open(path, mode)` | 打开文件（mode 形如 `"r"`/`"w"`/`"a"`；二进制由方法决定） |
| `read()` → `String` | 读全部内容 |
| `readlines()` → `Array` | 按行读成数组 |
| `write(text)` / `append(text)` | 覆盖写 / 追加（二进制，保 `\n`） |
| `write_lines(lines)` | 数组元素以 `\n` 连接后一次性写入 |
| `exists()` → `Boolean` | 文件是否存在 |
| `remove()` → `Boolean` | 删除，成功返回 true |
| `size()` → `Number` | 字节数 |

> 路径相对**当前工作目录**。写示例时用 `build/...` 之类已知存在的目录，避免写到意料之外的地方。
> Paths are relative to the **current working directory**. In examples write under a known dir like `build/...`.

---

## system — 进程、环境与时钟 / Process, env, clock

`&system;` 提供进程执行、环境变量、当前目录，以及一组**纯 C++、跨平台可靠**的时钟函数。
`&system;` gives process execution, env vars, cwd, and a set of **pure-C++, cross-platform-reliable** clock functions.

```text
&io; &system;
$Program {
    @::[() -> () {
        -(io::OStream out);
        -(system::System s);
        out << "now(ms) = "; out << s.now();       // 毫秒时间戳 / epoch ms
        out << " time = "; out << s.time();         // "HH:MM:SS"
        out << " date = "; out << s.date();         // "YYYY-MM-DD"
        out << " datetime = "; out << s.datetime();  // "YYYY-MM-DD HH:MM:SS"
        out << "before wait";
        s.wait(50);                                  // 阻塞 50ms / sleep 50ms
        out << " after wait(50ms)";
    }];
}
```

| 方法 / Method | 返回 / Returns | 含义 / Meaning |
|---|---|---|
| `now()` | `Number` | 毫秒时间戳 |
| `time()` `date()` `datetime()` | `String` | 当前时间（本地时区） |
| `wait(ms)` | — | 阻塞当前线程 `ms` 毫秒 |
| `cwd()` | `String` | 当前工作目录 |
| `getenv(name)` | `String` | 取环境变量值 |
| `run(cmd, timeout_ms)` | `Tuple` | **`(status, stdout, stderr)`** |
| `run_lines(cmd)` | `Array` | stdout 按行拆成数组 |
| `exec(argv, timeout_ms, cwd, env)` | `Tuple` | **安全、无 shell** 版本：传 argv 数组，元字符不被解释 |

> ⚠️ **`run` / `run_lines` 依赖宿主 shell**：在类 Unix 上用 `sh -c`，在 Windows 上行为取决于 `cmd`。输出捕获在不同平台可能不一致（某些 Windows 环境下 `echo` 的 stdout 可能为空）。**跨平台请用 `exec`**（传 `argv` 数组，例如 `[("myapp", "arg1")]`）以获得可预期行为。
> ⚠️ **`run`/`run_lines` depend on the host shell**: `sh -c` on Unix, `cmd` on Windows. Output capture can differ across platforms (on some Windows setups `echo`'s stdout may be empty). **For portable code use `exec`** (pass an `argv` array) to avoid shell interpretation.

---

## structs — 数据结构 / Data structures

C++ 底层实现；每个实例的 C++ 状态存于按实例 id 索引的注册表，所以对象可经流存活、可运行期注入/冻结。包括 `Queue` `Stack` `Tree`(BST) `Map` `Graph`。
C++-backed; per-instance C++ state lives in a registry keyed by a stable instance id, so objects survive flows and accept runtime injection/freeze. Covers `Queue`, `Stack`, `Tree` (BST), `Map`, `Graph`.

```text
&io; &structs;
$Program {
    @::[() -> () {
        -(io::OStream out);

        // Queue: 先进先出 / FIFO
        -(structs::Queue q);
        q.push(-(std::Number n) << 10);
        q.push(-(std::Number n) << 20);
        out << "queue pop="; out << q.pop();      // 10
        out << " peek="; out << q.peek();         // 20

        // Stack: 后进先出 / LIFO
        -(structs::Stack s);
        s.push(-(std::Number n) << 1);
        s.push(-(std::Number n) << 2);
        out << " stack pop="; out << s.pop();     // 2

        // Map: 键值映射，键带类型标签，不同类型不碰撞
        -(structs::Map m);
        m.put(-(std::String k) << "a", -(std::Number n) << 1);
        out << " map get(a)="; out << m.get(-(std::String k) << "a");   // 1
        out << " keys="; out << m.keys();         // [a]

        // Graph: 最短路径（尊重边权）
        -(structs::Graph g);
        g.add_node(-(std::Number n) << 1);
        g.add_node(-(std::Number n) << 4);
        g.add_edge(-(std::Number n) << 1, -(std::Number n) << 2, -(std::Number w) << 1);
        g.add_edge(-(std::Number n) << 2, -(std::Number n) << 4, -(std::Number w) << 1);
        out << " shortest 1->4="; out << g.shortest_path(-(std::Number n) << 1, -(std::Number n) << 4);  // [1,2,4]
    }];
}
```

**方法速查 / Methods**
- `Queue`: `push` `pop`(FIFO) `peek` `size` `empty` `clear` `to_array` `dispose`
- `Stack`: `push` `pop`(LIFO) `top` `size` `empty` `clear` `to_array` `dispose`
- `Tree` (键为 `std::Number`): `insert` `contains` `min` `max` `remove` `size` `empty` `inorder` `preorder` `postorder` `height` `lower_bound` `upper_bound` `range` `clear` `dispose`
- `Map`: `put(k,v)` `get(k)` `has(k)` `remove(k)` `keys` `values` `size` `empty` `clear` `dispose`
- `Graph`: `add_node` `add_edge(a,b,w)` `has_node` `neighbors` `node_count` `edge_count` `bfs` `shortest_path` `shortest_distance` `dispose`

> `Map` 的键带类型标签序列化：`Number 1` 与 `String "1"`、`1.0` 与 `1`、不同对象实例均**不碰撞**；`keys` 返回**原始**键对象（保持类型）。
> `Map` keys are serialized with a type tag: `Number 1` vs `String "1"`, `1.0` vs `1`, and distinct object instances never collide; `keys` returns the **original** key objects (type-preserving).
> 用完大量对象后调用 `dispose()` 可立即回收其 C++ 状态（否则滞留到对象被回收）。
> After heavy use, call `dispose()` to reclaim the C++ state immediately.

---

## re — 正则表达式 / Regular expressions

`Re` 是静态便捷类（边编译边跑）；`Pattern` 是**预编译**可复用匹配器；`Match` 是结果对象。底层用 `std::regex`（ECMAScript 文法）。
`Re` is the static convenience class (compiles on the fly); `Pattern` is a **precompiled**, reusable matcher; `Match` is a result object. Backed by `std::regex` (ECMAScript grammar).

```text
&io; &re;
$Program {
    @::[() -> () {
        -(io::OStream out);
        -(re::Re r);

        out << "test \\d+: ";
        out << r.test(-(std::String pat) << "\d+", -(std::String txt) << "abc123");   // true

        out << " findall \\d+: ";
        out << r.findall(-(std::String pat) << "\d+", -(std::String txt) << "a1b22c333");  // [1, 22, 333]

        // 预编译后复用 / compile once, reuse
        -(re::Pattern p) << r.compile(-(std::String pat) << "[a-z]+@[a-z]+\.[a-z]+");
        out << " email? ";
        out << p.test(-(std::String txt) << "foo@bar.com");                            // true
        out << " search text: ";
        out << p.search(-(std::String txt) << "contact me at foo@bar.com now").text;   // foo@bar.com

        out << " replace: ";
        out << r.replace(-(std::String pat) << "\d", -(std::String txt) << "a1b2", -(std::String rep) << "#");  // a#b#
    }];
}
```

| 类 / Class | 方法 / Methods | 返回 / Returns |
|---|---|---|
| `Re` | `compile(pattern)` | `Pattern` |
| | `match(p,t)` `search(p,t)` | `Match` |
| | `findall(p,t)` `split(p,t)` | `Array` |
| | `replace(p,t,repl)` | `String` |
| | `test(p,t)` | `Boolean` |
| `Pattern` | `match(t)` `search(t)` `findall(t)` `split(t)` `replace(t,repl)` `test(t)` `dispose()` | 同上 / as above |
| `Match`（字段非方法） | `matched` → `Boolean`；`text` → `String`；`groups` → `String` 数组；`start`/`end` → `Number`（偏移） | |

> `Match` 是**结果对象**，读字段而非调方法：`m.matched`、`m.text`、`m.groups`。
> `Match` is a **result object** — read fields, don't call them: `m.matched`, `m.text`, `m.groups`.

---

## hash — 哈希 / Hashing

`&hash;` 提供三种摘要：`sha256`（64 位小写十六进制）、`crc32`（32 位无符号整数）、`fnv1a`（16 位小写十六进制）。
`&hash;` offers three digests: `sha256` (64-char lowercase hex), `crc32` (32-bit unsigned int), `fnv1a` (16-char lowercase hex).

```text
&io; &hash;
$Program {
    @::[() -> () {
        -(io::OStream out);
        -(hash::Hash h);
        out << "sha256(abc)=";
        out << h.sha256("abc");     // ba7816bf...20015ad
        out << " crc32(abc)=";
        out << h.crc32("abc");      // 891568578
        out << " fnv1a(abc)=";
        out << h.fnv1a("abc");      // a9993e...
    }];
}
```

| 方法 / Method | 返回 / Returns | 用途 / Use |
|---|---|---|
| `sha256(text)` | `String` | 内容指纹、完整性校验 |
| `crc32(text)` | `Number` | 轻量校验和 |
| `fnv1a(text)` | `String` | 哈希表键、快速摘要 |

输入均为 `std::String`。
All inputs are `std::String`.

---

## async — 异步运行时 / Async runtime

基于工作线程 + future 实现，**全部求值在解释器 GIL 下串行化**（工业化审计 D6）：任务求值绝不与主线程竞争，代价是没有真并行加速——与 CPython、Ruby MRI 同一取舍。覆盖生命周期、并发/背压、异常隔离、动态派发、非阻塞定时器。失败的任务不会让反应堆崩溃——它变成 `error` 结果。
Built on worker threads + futures, **serialized under the interpreter GIL** (industrial-audit D6): task evaluation can never race the main thread, at the cost of no true parallel speedup — the same trade-off CPython and Ruby MRI make. Covers lifecycle, concurrency/backpressure, fault isolation, dynamic spawn, non-blocking timers. A failing task never crashes the reactor — it becomes an `error` result.

```text
&io; &async;
$Program {
    @::[() -> () {
        -(io::OStream out);
        -(async::Reactor r);

        r.set_limit(-(std::Number n) << 2);     // 最多 2 个并发（背压）/ max in-flight
        -(std::Array jobs);
        jobs.push_back([() ~> (x) { x << 10; }]);
        jobs.push_back([() ~> (x) { x << 20; }]);
        r.set(jobs);

        -(std::Tuple res) << r.start();
        out << "task count="; out << res.size();                 // 2
        out << " task1 status="; out << res.get(0).get(0);      // "ok"
        out << " task1 value="; out << res.get(0).get(1).get(0); // 10

        // 动态派发 + await / dynamic spawn + await
        -(async::Task t) << r.spawn([() ~> (x) { x << 99; }]);
        -(std::Tuple ta) << t.await();
        out << " spawned value="; out << ta.get(1).get(0);      // 99
    }];
}
```
**输出 / Output:** `task count=2 task1 status=ok task1 value=10 spawned value=99`

| 类 / Class | 方法 / Methods | 含义 / Meaning |
|---|---|---|
| `Reactor` | `set(tasks:Array)` `set_limit(max)` `set_timeout(ms)` `cancel()` | 配置任务集 / 并发上限 / 超时 |
| | `start([timeout])` → `Tuple` | 运行，返回 `(status, payload)` 元组数组 |
| | `with_timeout(closure, ms)` `spawn(closure)` `submit(closure)` → `Task` | 单次带超时 / 动态派发 |
| | `async_sleep(ms)` → `Task` | 非阻塞定时器 |
| `Task` | `await([timeout])` → `Tuple` `result()` `cancel()` `is_done()` `dispose()` | 取结果 / 取消 / 状态 |
| `Error` | `message()` `kind()` | 错误信息 / 类型（`exception`/`timeout`/`cancelled`/…） |

> 任务闭包通过 `x << 值` 公布结果；`start()` 的结果是 `[(status, payload), …]`，`payload` 是该任务公布的数组。`cancel()` 是协作式的：只置标志供任务在步骤间检查，运行中的任务绝不会被强行中断。Task 句柄是普通值，`-(async::Task t) << r.spawn(...)` 复制仍保留身份；注册表有上限（1024 条）且满后按最旧优先清扫，不再需要的未 await 任务请 `dispose()` 提前释放。
> A task closure publishes via `x << value`; `start()` yields `[(status, payload), …]` where `payload` is the array the task published. `cancel()` is cooperative: it only sets a flag a task checks between/before steps — a running task is never forcibly interrupted. Task handles are plain values: a `-(async::Task t) << r.spawn(...)` copy keeps its identity; the registry is bounded (1024 entries) and swept oldest-first once full, so `dispose()` unawaited tasks you no longer need.
> `Error` 的 `kind` 与 `message` 仅以方法存在（`err.kind()` / `err.message()`）——同名对象属性会遮蔽方法。
> `Error`'s `kind` and `message` exist only as methods (`err.kind()` / `err.message()`) — a same-named attribute would shadow them.

---

## assert — 运行期校验 / Runtime checks

`Checker` 在做复杂操作**之前**显式断言一个对象「当前是否合法」，而不是事后靠静默传播的受检值去猜。
`Checker` asserts an object's *current* legality **up front**, instead of inferring it later from silently-propagated checked values.

```text
&io; &assert;
$Counter { -(std::Number value); @get[() ~> (r) { r << value; }]; @inc[() -> () { value << value.+(1); }]; }
$Program {
    @::[() -> () {
        -(io::OStream out);
        -(Counter c);
        -(assert::Checker chk);

        out << "has inc? "; out << chk.has_method(c, "inc");        // true
        out << " changed? "; out << chk.has_changed(c);             // false

        // 运行期重绑 inc，再检查 / rebind inc at runtime, then re-check
        c:@inc[()->() { value << value.+(100); }];
        out << " changed-after-rebind? "; out << chk.has_changed(c); // true
    }];
}
```
**输出 / Output:** `has inc? true changed? false changed-after-rebind? true`

| 方法 / Method | 返回 / Returns | 含义 / Meaning |
|---|---|---|
| `has_method(target, name)` | `Boolean` | 对象当前是否有名为 `name` 的方法 |
| `has_changed(target)` | `Boolean` | 对象的方法集是否在运行期被重绑过 |

> `Checker` 常配合「运行期注入方法」（`obj:@m[{…}]`）使用：注入前 `has_changed` 为 false，注入后为 true。
> `Checker` pairs naturally with runtime method injection (`obj:@m[{…}]`): `has_changed` is false before, true after.

---

## sugar — 语法糖：算术表达式求值 / Sugar: expression evaluator

核心解释器不原生提供算术运算符，`sugar` 补上一个 `Infix` 求值器：把字符串当算术表达式求值，支持 `+ - * / %`、括号、一元负号，还能从 `std::Dict` 绑定变量。
The core interpreter has no arithmetic operators; `sugar` supplies an `Infix` evaluator: parse a string as an arithmetic expression, with `+ - * / %`, parentheses, unary minus, and variable binding from a `std::Dict`.

```text
&io; &sugar;
$Program {
    @::[() -> () {
        -(io::OStream out);

        // 构造时直接给表达式 / pass the expression at construction
        -(sugar::Infix("1+(2-3)*(3+5)") e);
        out << "1+(2-3)*(3+5) = "; out << e.parse();     // -7

        // 之后用 @set 换表达式 / replace later with @set
        e.set("2*3 + 4*5");
        out << " 2*3+4*5 = "; out << e.parse();           // 26

        // 从字典绑定变量 / bind variables from a Dict
        -(std::Dict env);
        env.set("a", 2); env.set("b", 3);
        -(sugar::Infix("a*a + b*b") e2);
        e2.env(env);
        out << " a*a+b*b (a=2,b=3) = "; out << e2.parse(); // 13
    }];
}
```
**输出 / Output:** `1+(2-3)*(3+5) = -7 2*3+4*5 = 26 a*a+b*b (a=2,b=3) = 13`

| 方法 / Method | 含义 / Meaning |
|---|---|
| `-(sugar::Infix("expr") e)` | 构造时传表达式字符串 |
| `set(expr)` | 换表达式文本，复用同一实例 |
| `env(dict)` | 把标识符解析到字典中同名的数值键 |
| `parse()` → `Number` | 求值；环境缺失或标识符未解析时抛运行期错误 |
| `dispose()` | 立即回收表达式状态 |

> 构造用 `-(sugar::Infix("...") e)`（表达式在**括号**里），不要用 `<<` 流喂——构造器的 `expr` 参数不会被 `<<` 自动填。
> Construct with `-(sugar::Infix("...") e)` (expression **in parentheses**); don't try to feed it via `<<` — the constructor's `expr` param isn't auto-filled by a flow.

---

👉 模块清单与导入机制：[标准库总览 / Standard Libraries](Standard-Libraries) · 语法基础：[基本语法 / Basic Syntax](Basic-Syntax)
