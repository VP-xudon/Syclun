# Syclun Standard-Library Reference
# Syclun 标准库参考

> Moved out of `README.md` so the README stays an introduction.
> This file is the **reference**: exact method signatures and the checklist for
> adding a new library.
> 从 `README.md` 迁出，使 README 回归「介绍」职责。本文件是**参考手册**：
> 精确的方法签名与新增标准库的清单。
>
> See also / 另见：
> - Language specification / 语言规范：[`Synth-OOP语言文档-修正版.md`](./Synth-OOP语言文档-修正版.md)（English / 英文版：[`Synth-OOP-Language-Documentation-修正版.md`](./Synth-OOP-Language-Documentation-修正版.md)）
> - Project introduction / 项目介绍：[`../README.md`](../README.md)

## Contents / 目录

- [Runtime signature enforcement / 运行期签名强制](#runtime-signature-enforcement--运行期签名强制)
- [Preset objects / 预置对象](#preset-objects--预置对象)
- [Method reference / 方法参考](#method-reference--方法参考)
  - `file` · `system` · `structs` · `re` · `maths` · `async` · `hash` · `io` · `assert`
- [Adding a standard library / 如何新增标准库](#adding-a-standard-library--如何新增标准库)

---

## Preset objects / 预置对象

Since then a library face (`.synl`) may create ready-made object instances at
the top level (e.g. `-(io::OStream! io::out);`). The object's name is its full
qualified name — the leading module is how a program finds the library's
runtime space, and the object lives in it exactly like the library's classes
do. Presets arrive with the import, live for the whole run, and are **const
bindings** — the name always refers to the same object (assignment via `.=` is
rejected with a `ConstException`). A user program (`.syn`) must NOT declare
global objects directly; presets belong to libraries.
当前，库形态（`.synl`）可在顶层创建成品对象实例（如
`-(io::OStream! io::out);`）。对象的名字就是它的全限定名——前导的模块名正是
程序找到该库运行空间的路径，对象与库的类一样居于其中。预置对象随导入到来、
贯穿整个运行期，且是**常数绑定**——名字永远指向同一对象（经 `.=` 赋值会以
`ConstException` 被拒）。用户程序（`.syn`）不得直接声明全局对象；预置对象
属于库。

| Module / 库 | Preset / 预置对象 | Type / 类型 | Replaces / 取代 |
|---|---|---|---|
| `io` | `io::out` | `io::OStream` | `-(io::OStream o);` in every program / 每个程序里的手写声明 |
| `io` | `io::in` | `io::IStream` | `-(io::IStream i);` ditto / 同上 |
| `maths` | `maths::math` | `maths::Maths` | instantiating the stateless `$Maths` class / 实例化无状态的 Maths 类 |
| `http` | `http::client` | `http::Client` | stateless HTTP/1.1 request client / 无状态 HTTP/1.1 请求客户端 |
| `http` | `http::crawler` | `http::Crawler` | BFS web crawler / 广度优先网络爬虫 |
| `internet` | `internet::net` | `internet::Network` | network info + connectivity probe / 网络信息与连通性探测 |
| `bt` | `bt::radio` | `bt::Bluetooth` | Bluetooth discovery / connect / radio state / 蓝牙发现 / 连接 / 无线电状态 |

---

## Runtime signature enforcement / 运行期签名强制

**Method signatures are runtime-enforced.** Every native / built-in method
carries a precise `CallableSign` (e.g. `re`'s `compile(pattern[std::String]) ~> (pat[re::Pattern])`, `async`'s
`spawn(task[@]) → async::Task`, `io`'s `OStream.:=(value[std::Object])`). At the
call boundary the runtime rejects argument-type
mismatches *before* the body runs, so malformed input (e.g. a `Number` fed to
`String.+`) is caught with a clear `Argument type mismatch for method '…'`
rather than blowing up deep inside a closure. Flow / lifecycle methods
(`::`/`~`/`=:`/`=`/`:=`), universal / behavior / variadic parameters
(`std::Object`, `@`, `...`), and the heterogeneous element tag `value` (used by
`Array`/`Dict`/`Tuple`) are intentionally exempt; a missing or type-mismatched
argument raises an immediate `TypeException: Argument type mismatch for method
'…': expected '…', got '…'` at the call boundary rather than degrading silently.
A missing method (`Method '…' not found`) is reported immediately as well.
**方法签名在运行时被强制约束。** 每个原生 / 内置方法都携带精确的
`CallableSign`（如 `re` 的 `compile(pattern[std::String]) ~> (pat[re::Pattern])`、
`async` 的 `spawn(task[@]) → async::Task`、`io` 的 `OStream.:=(value
[std::Object])`）。运行时在调用边界**先于方法体**拒收类型不符的实参，使错误输入
（例如把 `Number` 喂给 `String.+`）得到清晰的 `Argument type mismatch for
method '…'`，而非在闭包深处炸开。流 / 生命周期方法（`::`/`~`/`=:`/`=`/`:=`）、
通用 / 行为 / 变参（`std::Object`、`@`、`...`）以及异构元素标签 `value`
（Array/Dict/Tuple 使用）有意豁免；缺失或类型不符的实参会**即时抛出错误**
（`TypeException: Argument type mismatch for method '…': expected '…', got '…'`），
不再静默降级为毒水。方法缺失（`Method '…' not found`）同样即时报错。

**Scope of enforcement (now complete).** All three layers are implemented and
runtime-enforced:
  1. **Native / built-in methods** — the call-boundary type check above.
  2. **`#Contract` definitions** — `#Name { @m[(in) -> (out)]; … }` declares
     a contract; `ClassContract::validate` checks that a class implements every
     required signature, compared **by type** (not by parameter name) so a real
     implementation satisfies it.
  3. **Parameter & variable constraints** — a parameter `x[Contract]` or a
     variable `-(T[Contract] v)` is checked with `check_constraint` *whenever the
     bound value is known*: a parameter at behavior entry, a variable at
     declaration (when it has no initializer) and again after a flow `<<` binds
     it. The bound value must actually implement the contract's methods, or a
     `ConstraintException` is raised (`value does not satisfy constraint '…'`).
**强制范围（现已完整）**：三层均已实现并在运行期强制：
(1) 原生 / 内置方法的调用边界类型检查；(2) `#契约` 定义——`#名 { @m[(入) -> (出)]; … }`
声明契约，`ClassContract::validate` 校验某类是否实现全部所需签名（**按类型**而非
参数名比较，故真实实现即可满足）；(3) 参数与变量约束——参数 `x[契约]` 或变量
`-(T[契约] v)` 在绑定值已知时经 `check_constraint` 核查：参数于行为入口核查，变量于
声明时（无初始化器）及流 `<<` 绑定后各核查一次。被绑定值须真正实现契约方法，否则
抛出 `ConstraintException`（`value does not satisfy constraint '…'`）。
**Architectural caveat (known limitation).** The publish / receive capsule
mechanism only carries *scalar* capsules (number / string / boolean); a value
bound through a `std::Object` flow assignment therefore cannot hold a real
object. For end-to-end enforced constraints, bind the value to a **concrete**
type and let it travel as a direct pointer (method argument or `self`) — e.g.
`-(Adder[Addable] a)` plus `self.use(a)` — rather than
`-(std::Object[Addable] a) << …`. See `verify/philosophy/constraint_demo.syn`.
**架构性注意（已知限制）**：公布 / 接收胶囊机制仅携带*标量*胶囊（数 / 串 / 布尔），
故经 `std::Object` 流赋值绑定的值无法持有真实对象。要让约束端到端生效，请把约束值
绑定到**具体**类型并作为直接指针传递（方法实参或 `self`），例如
`-(Adder[Addable] a)` 搭配 `self.use(a)`，而非 `-(std::Object[Addable] a) << …`。
见 `verify/philosophy/constraint_demo.syn`。

---

## Method reference / 方法参考

- **`file` → `File`** — `open/read/readlines/write/append/exists/remove/size`,
  attributes `path`/`mode`. / 文件读写。
- **`system` → `System`** — process/shell (`run`/`run_lines` via `popen`,
  `cwd`, `getenv`) and **time inspection**: `now()` → `Number` (ms since the
  Unix epoch), `time()` → `String` `"HH:MM:SS"`, `date()` → `String`
  `"YYYY-MM-DD"`, `datetime()` → `String` `"YYYY-MM-DD HH:MM:SS"` (all local
  time). `wait(ms)` blocks the current thread for `ms` integer milliseconds
  (`std::Number`). / 进程/Shell（popen 的 `run`/`run_lines`、`cwd`、`getenv`）
  与**时间查看**：`now()`→`Number`（Unix 毫秒戳）、`time()`→`String` `"HH:MM:SS"`、
  `date()`→`String` `"YYYY-MM-DD"`、`datetime()`→`String` `"YYYY-MM-DD HH:MM:SS"`
  （均为本地时间）。`wait(ms)` 使当前线程休眠 ms 个整数毫秒。
- **`structs` → algorithm structures** (C++-backed; per-instance C++ state
  lives in a process-wide registry keyed by a stable instance id, so the
  objects survive publish/receive). / 算法结构（C++ 底层；每个实例的 C++ 状态
  存于按稳定实例 id 索引的进程级注册表，故对象可经公布/接收存活）。
  - **`Queue`** — `push(x)`/`pop()`(FIFO)/`peek()`/`size()`/`empty()`/`clear()`/`to_array()`.
  - **`Stack`** — `push(x)`/`pop()`(LIFO)/`top()`/`size()`/`empty()`/`clear()`/`to_array()`.
  - **`Tree`** — binary search tree over `std::Number`: `insert`/`contains`/`min`/
    `max`/`remove`/`size`/`empty`/`inorder()`/`preorder()`/`postorder()`.
  - **`Map`** — associative map (key = `std::Number`/`std::String`/object):
    `put`/`get`/`has`/`remove`/`keys()`/`values()`/`size()`/`empty()`/`clear()`.
  - **`Graph`** — undirected weighted graph: `add_node`/`add_edge`/`has_node`/
    `neighbors`/`node_count`/`edge_count`/`bfs`/`shortest_path` (BFS, returns the
    node-id path, e.g. `1→4` gives `[1,2,3,4]`). / 无向带权图：增节点/边、邻接、
    计数、BFS、最短路径。

- **`re` → regular expressions** (C++-backed; per-instance C++ state lives in a
  process-wide registry keyed by a stable instance id, so `Pattern`/`Match`
  objects survive publish/receive — same idiom as the `structs` module).
  / 正则表达式（C++ 底层；按稳定实例 id 索引的进程级注册表，使 `Pattern`/`Match`
  可经公布/接收存活，与 structs 同型）。
  - **`Re`** (static convenience, compiles on the fly) — `compile(pattern)` →
    `Pattern`; `match(pattern, text)`/`search(pattern, text)` → `Match`;
    `findall(pattern, text)`/`split(pattern, text)` → `Array` of `String`;
    `replace(pattern, text, repl)` → `String`; `test(pattern, text)` →
    `Boolean`.
  - **`Pattern`** — a compiled regex (the C++ `std::regex` lives in the
    registry): `match(text)`/`search(text)` → `Match`; `findall(text)`/`split(text)`
    → `Array` of `String`; `replace(text, repl)` → `String`; `test(text)` →
    `Boolean`.
  - **`Match`** — result object (fields, not methods): `matched` → `Boolean`,
    `text` → `String` (the matched substring), `groups` → `Array` of `String`
    (capture groups), `start`/`end` → `Number` (byte offsets).
    / 匹配结果（均为字段而非方法）：matched→Boolean、text→String（命中子串）、
    groups→String 数组（捕获组）、start/end→Number（偏移）。

- **`sugar` → `Infix`** (C++-backed; per-instance C++ state lives in a
  process-wide registry keyed by a stable instance id, same idiom as the
  `structs` module). / 语法糖（C++ 底层；按稳定实例 id 索引的进程级注册表，
  与 structs 同型）。
  - **`$Infix`** — an arithmetic-expression evaluator. `-(sugar::Infix("1+(2-3)*(3+5)") e)`
    (or `e.set("...")`) stores the expression; `e.env(dict)` binds identifiers
    to `std::Dict` values; `e.parse()` evaluates and returns a `std::Number`.
    Supports `+ - * / %`, parentheses, and unary minus. `1+(2-3)*(3+5)` → `-7`;
    with `a = 2` in the env, `a+3` → `5`. An unresolved variable (no env set, or
    the key absent) raises a runtime error attributed to the `parse` method.
    / 算术表达式求值器。`-(sugar::Infix("1+(2-3)*(3+5)") e)`（或 `e.set("...")`）
    保存表达式；`e.env(dict)` 把标识符绑定到 `std::Dict` 值；`e.parse()` 求值并返回
    `std::Number`。支持 `+ - * / %`、括号与一元负号。`1+(2-3)*(3+5)` → `-7`；环境
    中 `a = 2` 时 `a+3` → `5`。未解析的变量（未设环境或缺键）抛出归属 `parse` 方法的
    运行时错误。

- **`maths` → `Maths`** — scalar math on `std::Number`. Every op takes
  `std::Number` and returns `std::Number` `r` (integral flag where noted):
  / 标量数学，输入 `std::Number`、返回 `std::Number` `r`：
  `abs(x)`, `sqrt(x)`, `pow(base, exp)`, `floor(x)`, `ceil(x)`, `round(x)`,
  `sin(x)`, `cos(x)`, `tan(x)`, `log(x)`, `log10(x)`, `exp(x)`,
  `mod(a, b)`, `min(a, b)`, `max(a, b)`; constants `pi()` `e()` `random()`.

- **`async` → `Reactor`/`Task`/`Error`** — a production async runtime built on
  worker threads + futures, **serialized under the interpreter GIL**
  (industrial-audit D6): worker tasks can never race the main thread's
  evaluation, at the cost of no true parallel CPU speedup — the same trade-off
  CPython and Ruby MRI make. Covers the five dimensions of a real runtime:
  lifecycle control, concurrency/backpressure, error isolation, dynamic
  spawning, and non-blocking timers. / 基于工作线程 + future 的生产级异步运行时，
  **全部求值在解释器 GIL 下串行化**（工业化审计 D6）：任务求值绝不与主线程竞争，
  代价是没有真并行加速——与 CPython、Ruby MRI 同一取舍。覆盖生命周期控制、
  并发/背压、异常隔离、动态派发、非阻塞定时器五个维度。
  - **`$Reactor`** — `set(tasks)` (store an `Array` of closures),
    `set_limit(max)` (max in-flight tasks; `0` = unlimited → backpressure),
    `set_timeout(ms)` (per-task default timeout), `cancel()` (best-effort
    cancel flag), `start([timeout])` → `Tuple` of **`(status, payload)`**
    result tuples, `with_timeout(closure, ms)` → `(status, payload)`,
    `spawn(closure)`/`submit(closure)` → `Task` (dynamic submission),
    `async_sleep(ms)` → `Task` (non-blocking timer). Each `status` is one of
    `ok` / `error` / `timeout` / `cancelled`; a failing closure becomes an
    `error` whose `payload` is an `Error` object (fault tolerance — the
    Reactor never crashes on a bad task). Hard interpreter errors (missing
    method, type violation) are NOT isolated: by design they take the
    immediate fatal-diagnostic path, wherever they occur.
    / `set(tasks)` 存闭包数组；`set_limit(max)` 并发上限（0 不限，背压）；
    `set_timeout(ms)` 逐任务默认超时；`cancel()` 尽力取消；`start([timeout])`
    返回 `(status,payload)` 元组数组；`with_timeout(闭包,ms)` 单任务硬超时；
    `spawn`/`submit` 动态派发返回 `Task`；`async_sleep(ms)` 非阻塞定时器。
    `status`∈{ok,error,timeout,cancelled}；失败的闭包转为 `error` 且 `payload`
    为 `Error`（容错，反应堆绝不因单个坏任务崩溃）。
  - **`$Task`** — a future-like handle: `await([timeout])` → `(status, payload)`
    (blocks until done or timeout), `result()` → `(status, payload)` (no wait),
    `cancel() → ()`, `is_done()` → `Boolean`, `dispose()` → `()`
    (release the underlying registry entry early). `cancel` is **cooperative**:
    it only sets a flag a task checks between/before steps — a running task is
    never forcibly interrupted. Task handles are plain values: a
    `-(async::Task t) << r.spawn(...)` copy keeps the identity and may be
    awaited like the original; the registry is bounded (1024 entries) and
    swept oldest-first once full, so `dispose()` unawaited tasks you no longer
    need.
    / 类 future 句柄：`await([timeout])`→`(status,payload)`、`result()`→同、
    `cancel()`、`is_done()`→Boolean、`dispose()`（提前释放底层注册表条目）。
    `cancel` 是**协作式**的：只置标志供任务在步骤间检查——运行中的任务绝不会被
    强行中断。Task 句柄是普通值：`-(async::Task t) << r.spawn(...)` 复制仍保留
    身份、可如原件一样 await；注册表有上限（1024 条）且满后按最旧优先清扫，
    故不再需要的未 await 任务请 `dispose()`。
  - **`$Error`** — `message()` → `String`, `kind()` → `String`
    (`kind` is e.g. `exception`/`timeout`/`cancelled`/`unknown`). `kind` and
    `message` exist ONLY as methods — a same-named object attribute would
    shadow them (member lookup is attributes-first).
    / `message()`→String、`kind()`→String（如 exception/timeout/cancelled）。
    `kind` 与 `message` **仅以方法存在**——同名对象属性会遮蔽方法
    （成员查找属性优先）。

- **`hash` → `Hash`** — hashing on `std::String`.
  / 对 `std::String` 的哈希：
  - `sha256(text)` → `String`: FIPS 180-4, lowercase hex, 64 chars.
  - `crc32(text)`  → `Number`: IEEE 802.3 polynomial, exact `< 2^32`.
  - `fnv1a(text)`  → `String`: FNV-1a 64-bit, lowercase hex, 16 chars.

- **`io` → `OStream` / `IStream`** — standard streams (C++-backed, the same
  shape as the other native libraries; not special-cased into `builtin.hpp`).
  / 标准流（C++ 底层，与其它原生库同形，不再特例塞进 builtin.hpp）。
  - **`io::OStream`** — output stream. Two calling styles for the same effect:
    / 输出流。两种等价写法：
    - `out << value` (flow → `:=` receive behavior) **or** `out.push(value)`
      — write `value` with **no** trailing newline; `value` is a universal
      sink (`std::Object`), so any object may be printed.
      / `out << value`（流 → `:=` 接收行为）**或** `out.push(value)`——写出值，
      **不**换行；`value` 为通用汇聚点（std::Object），任意对象皆可打印。
    - `out.push_line(value)` — write `value` **plus a newline** (each call on
      its own line). / `out.push_line(value)`——写出值**并换行**（每次调用各占一行）。
    Signs: `:(value [std::Object]) ~> ()`, `push(value [std::Object]) ~>
    ()`, `push_line(value [std::Object]) ~> ()`.
    / 签名：`:(value [std::Object]) ~> ()`、`push(...) ~> ()`、
    `push_line(...) ~> ()`。
  - **`io::IStream`** — input stream. Two calling styles for the same effect:
    / 输入流。两种等价写法：
    - `x << in` (i.e. `x =: in`, flow → `=:` publish behavior) **or**
      `x << in.get()` — read **one whitespace-delimited word** from standard
      input and bind it to `x` (typed `std::String`).
      / `x << in`（即 `x =: in`，流 → `=:` 公布行为）**或** `x << in.get()`——
      从标准输入读**一个以空白分隔的词**并绑定到 `x`（类型 std::String）。
    - `x << in.get_line()` — read **one whole line** (newline stripped). A
      dangling newline left by a preceding `get()` is consumed first so the
      call does not return an empty line. / `x << in.get_line()`——读**一整行**
      （去换行）；会先吃掉 `get()` 遗留的悬空换行，避免立刻返回空行。
    On EOF any read raises an immediate `RuntimeException: input stream ended`
    (the retired poison-value fallback is gone). / 遇 EOF 任意读取都会即时抛出
    `RuntimeException: input stream ended`（原有的毒水兜底已移除）。
    Signs: `=:() ~> (result [std::String])`, `get() ~> (result [std::String])`,
    `get_line() ~> (result [std::String])`.
    / 签名：`=:() ~> (result [std::String])`、`get() ~> (result [std::String])`、
    `get_line() ~> (result [std::String])`.

- **`assert` → `assert::Checker`** — runtime verification helpers for checking
  operation legality (import with `&assert;`). The `Checker` object exposes two
  methods:
  / 运行期验证助手（用 `&assert;` 导入），用于检查操作合法性。`Checker` 对象提供两个方法：
  - `has_method(target[std::Object], name[std::String]) → result[std::Boolean]`
    — `true` iff `target` currently has a method named `name`. Works on any
    object because `std::Object` is the universal receiver type (so you can pass
    e.g. a `Counter` instance).
    / `true` 当且仅当 `target` 当前拥有名为 `name` 的方法。因 `std::Object` 是通用
    接收类型，任意对象皆可传入（如传入 `Counter` 实例）。
  - `has_changed(target[std::Object]) → result[std::Boolean]` — `true` iff any
    of `target`'s methods have been dynamically rebound at runtime (i.e. the
    object's `methods_dirty` flag is set by a runtime `c.method.=(beh)` /
    `c.method << beh` rebind).
    / `true` 当且仅当 `target` 的方法在运行期被动态重绑（即对象经运行期
    `c.method.=(beh)` / `c.method << beh` 重绑后，`methods_dirty` 标志被置位）。
  `assert::Checker` replaces the old `_case`/"poison-water" diagnostics: instead
  of a silently-propagating checked value, you assert legality up front.
  / `assert::Checker` 取代了旧的 `_case`（毒水）诊断：不再依赖静默传播的受检值，
  而是事前显式断言合法性。示例见 `verify/philosophy/checker_demo.syn`。

---

## Advanced standard libraries / 高级标准库

五个与「平台能力」强相关的模块——网络与图形界面——单独列为**高级标准库**层。它们由平台
原生能力直接支撑：网络走原生 socket / `iphlpapi` / `getifaddrs` / 蓝牙原生工具；GUI 与画布走
一套**平台中立 C 桥**（`gui_native.h` / `windows_native.h`），含 Win32 / X11 / Cocoa **三套真实
实现**，按平台只编译对应一套并链接进同一二进制——因此**真正跨平台**：Windows / Linux /
macOS 上均获得完整能力，CI 在三大平台构建并验证。所有方法沿用运行期签名强制（见上文），
错误即时抛出。
Five platform-capability modules — networking, Bluetooth and GUI — are grouped as the
**advanced standard-library tier**. They are backed directly by each platform's native
capabilities: networking uses raw sockets / `iphlpapi` / `getifaddrs` / the OS Bluetooth
tooling; GUI and canvas sit on a **platform-neutral C bridge** (`gui_native.h` /
`windows_native.h`) with **three real implementations** (Win32 / X11 / Cocoa), only the
matching one compiled and linked per platform — so they are **genuinely cross-platform**:
Windows / Linux / macOS all get full functionality, and CI builds and verifies on all three.
All methods keep runtime signature enforcement (above) and raise immediate errors.

- **`http` → `Client` / `Response` / `Crawler`** — HTTP/1.1 客户端（基于原生 socket，仅明文
  HTTP，不含 TLS）。`Client` 与 `Crawler` 均为随导入到来的常数预置对象（`http::client`、
  `http::crawler`）；`Response` 经流赋值 `<<` 携带其 `#status`/`#reason`/`#headers`/`#body`
  字段（已加 `=:`/`=` 公布/接收方法，不会在 `<<` 后变空）。 / HTTP/1.1 client over raw sockets
  (cleartext HTTP only, no TLS). Both `Client` and `Crawler` arrive as const presets
  (`http::client`, `http::crawler`); `Response` carries its `#status`/`#reason`/`#headers`/
  `#body` across a `<<` flow (publish/receive methods added so it never arrives hollow).
  - `http::client.get(url)` / `.post(url, body)` / `.put(url, body)` / `.delete(url)`
    / `.head(url)` → `http::Response`; `http::client.download(url, path)` → `Boolean`
    （下载到本地文件）；`http::client.request(method, url, headers, body)` → `http::Response`。
  - `Response.status()` → `Number`, `Response.reason()` → `String`,
    `Response.headers()` → `Dict`, `Response.body()` / `.text()` → `String`,
    `Response.ok()` → `Boolean` (status ∈ [200,300)).
  - `http::crawler.crawl(start, maxDepth, maxPages)` → `Array` of `Dict`
    `{ url, title, depth, links: Array<String> }`：从 `start` 起广度优先抓取、提取 `<title>` 与
    全部 `<a href>`、按 `maxDepth`/`maxPages` 跟进链接。/ BFS web crawler: from `start` it fetches
    each page, extracts `<title>` and every `<a href>`, follows links up to `maxDepth` /
    `maxPages`; returns an `Array` of `Dict` `{ url, title, depth, links }`.
- **`internet` → `Network`** — 广义网络服务：**接口 / 地址信息**、**连通性探测**、**Wi‑Fi 列表**
  与**端口扫描**。`Network` 为随导入到来的常数预置对象 `internet::net`。Wi-Fi / 蜂窝的*主动*
  连接/断开受各操作系统管控，超出 L3 范围（本库提供可见网络*列表*，不提供连接开关）；HTTP 请求
  请走 `http`。 / Generalized networking: interface & address **info**, **connectivity probes**,
  **Wi‑Fi listing** and **port scanning**. `Network` arrives as the const preset `internet::net`.
  Active Wi-Fi / cellular *connect/disconnect* is OS-controlled and out of L3 scope (the library
  lists visible networks but does not toggle them); use `http` for requests.
  - `internet::net.interfaces()` → `Array` of `Dict` (name / family / address / netmask / loopback).
  - `internet::net.resolve(host)` → `Array` of IP `String`.
  - `internet::net.reachable(host, port)` → `Boolean`; `internet::net.latency(host, port)`
    → `Number` (ms, -1 on failure).
  - `internet::net.local_addresses()` → `Array` of non-loopback IP `String`.
  - `internet::net.is_online()` → `Boolean` (probes 8.8.8.8:53 / 1.1.1.1:53 over TCP).
  - `internet::net.wifi_networks()` → `Array` of `Dict` `{ ssid, signal, channel, security }`
    （可见 Wi‑Fi 列表；Windows 解析 `netsh wlan show networks`、Linux 用 `nmcli`、macOS 用
    `airport`）。 / visible Wi‑Fi networks (Windows `netsh` / Linux `nmcli` / macOS `airport`).
  - `internet::net.scan_ports(host, from, to)` → `Array` of open port `Number`（TCP 端口扫描，
    区间 `[from, to]`）。 / TCP port scan over `[from, to]`.
- **`gui` → `Window`** — 类 tkinter 的迷你系统窗口工具包（顶层 `Window` + `label` / `button` /
  `entry` / `checkbox` / `slider` / `listbox` / `textarea` / `message`，按钮等动作接回 Synth-OOP
  闭包）。由平台中立 C 桥（`gui_native.h`）支撑，含 **Win32 User32 / X11 保留式控件 / Cocoa
  AppKit** 三套真实实现，按平台只编译对应一套——故**真正跨平台**（需桌面环境）。`w.mainloop()`
  泵消息循环，按钮点击与控件回调在其中派发。 / A small tkinter-like system-window toolkit
  (top-level `Window` + `label` / `button` / `entry` / `checkbox` / `slider` / `listbox` /
  `textarea` / `message`, actions wired to Synth-OOP closures). Backed by a platform-neutral C
  bridge (`gui_native.h`) with **three real implementations** (Win32 User32 / X11 retained
  widgets / Cocoa AppKit) — genuinely cross-platform (needs a desktop session). `w.mainloop()`
  pumps the message loop and dispatches button clicks / control callbacks.
  - `w.open(title, width, height)` — 创建并显示窗口。
  - `w.label(text, x, y)`，`w.button(text, x, y, w, h, action[@])`，`w.set_title(title)`。
  - `w.entry(x, y, w, h)` → `Number`（索引），`w.entry_text(index)` → `String`；
    `w.checkbox(text, x, y)` → 索引，`w.checkbox_checked(index)` → `Boolean`；
    `w.slider(x, y, w, min, max, value)` → 索引，`w.slider_value(index)` → `Number`；
    `w.listbox(items[Array], x, y, w, h)` → 索引，`w.listbox_selection(index)` → `Number`（-1 无）；
    `w.textarea(text, x, y, w, h)` → 索引，`w.textarea_text(index)` → `String`。
  - `w.message(title, text, kind)` — 消息框（kind：0 信息 / 1 警告 / 2 错误）。
  - `w.on_close(action[@])` — 窗口关闭时调用；`w.mainloop()` 泵循环至关闭；`w.close()` 销毁。
- **`windows` → `Screen`** — 类 pyglet 的 2-D 画布窗口。由平台中立 C 桥（`windows_native.h`）
  支撑，含 **Win32 GDI / X11 GC / Cocoa CGContext** 三套真实实现，按平台只编译对应一套——故
  **真正跨平台**（需桌面环境）。注册 `on_draw` 回调（在其中调用 `clear` / `draw_*` 基元）、可选
  `on_key` / `on_close` 回调，再 `mainloop()` 泵事件；绘制既可在 `on_draw` 内即时生效，也可作为
  缓冲场景被自动重绘。 / A pyglet-like 2-D canvas window. Backed by a platform-neutral C bridge
  (`windows_native.h`) with **three real implementations** (Win32 GDI / X11 GC / Cocoa
  CGContext) — genuinely cross-platform (needs a desktop session). Register `on_draw` (where you
  call `clear` / `draw_*` primitives), optionally `on_key` / `on_close`, then `mainloop()` to pump
  events; drawing works both live inside `on_draw` and as a buffered scene that repaints.
  - `s.open(title, width, height)` / `s.set_title(title)` — 创建 / 重命名画布窗口。
  - `s.on_draw(action[@])`，`s.on_key(action[(key[std::String]) -> ()])`，`s.on_close(action[@])`。
  - `s.clear(r, g, b)`（0–255；置背景色并请求重绘）。
  - `s.draw_line(x1, y1, x2, y2, color)`；`s.draw_rect(x, y, w, h, color)` / `s.draw_outline_rect(...)`
    （实心 / 描边）；`s.draw_circle(cx, cy, r, color)` / `s.draw_outline_circle(...)`；
    `s.draw_ellipse(x, y, w, h, color)` / `s.draw_outline_ellipse(...)`；`s.draw_polygon(points[Array], color)`
    / `s.draw_outline_polygon(...)`（points 为扁平 `[x0,y0,x1,y1,…]`）；`s.draw_text(x, y, text, color)`。
    color 为 `0xRRGGBB` 的十进制 `Number`（Synth-OOP 无十六进制字面量，请传十进制值）。
  - `s.mainloop()` — 泵消息至窗口关闭；`s.close()` — 销毁。

- **`bt` → `Bluetooth`** — 蓝牙设备发现、枚举、连接 / 断开与无线电状态。蓝牙无统一 C API，故
  底层调用各平台原生工具（Windows 经蓝牙设置 / Linux 经 `bluetoothctl` / macOS 经 `blueutil`），
  上是统一的 Synth-OOP 界面。RFCOMM SPP 字节流 I/O 在 L3 之外。随导入到来的常数预置对象
  `bt::radio`。 / Bluetooth device discovery, enumeration, connect / disconnect and radio state.
  No single portable C API for Bluetooth exists, so each platform's native tooling is used under
  the hood (Windows Bluetooth settings / Linux `bluetoothctl` / macOS `blueutil`), surfaced through
  one uniform Synth-OOP interface. RFCOMM SPP byte-stream I/O is out of L3 scope. Arrives as the
  const preset `bt::radio`.
  - `bt::radio.devices()` → `Array` of `Dict` `{ address, name, status }`（已知 / 已配对设备）。
  - `bt::radio.scan(timeout)` → `Array` of `Dict` `{ address, name, status }`（扫描附近设备 `timeout` 秒）。
  - `bt::radio.connect(address)` / `bt::radio.disconnect(address)` → `Boolean`（Linux / bluez）。
  - `bt::radio.enabled()` → `Boolean`（无线电是否开启）。

> **L3 范围声明 / L3 scope note**: `http` 仅明文 HTTP/1.1（不含 TLS）；`internet` 提供信息 +
> 探测 + Wi‑Fi 列表 + 端口扫描，不含 Wi‑Fi / 蜂窝的*主动连接/断开*；`bt` 提供发现 / 枚举 /
> 连接 / 断开 + 无线电状态，RFCOMM 字节流在 L3 外。`gui` / `windows` 现已**真正跨平台**
> （Win32 / X11 / Cocoa 三套原生实现，按平台只编译对应一套），不再降级为「不支持」错误。
> 四库均满足 L3「正确、可测、文档齐、有安全边界」的门槛。 / `http` is cleartext HTTP/1.1 only
> (no TLS); `internet` provides info + probes + Wi‑Fi listing + port scan, not *active*
> Wi‑Fi/cellular connect/disconnect; `bt` provides discovery / enumeration / connect / disconnect
> + radio state, RFCOMM byte-stream out of L3. `gui`/`windows` are now **genuinely cross-platform**
> (Win32 / X11 / Cocoa, one per platform) and no longer degrade to a "not supported" error. All four
> meet L3's "correct, tested, documented, with a safety boundary" bar.

---

### Adding a standard library / 如何新增标准库

Follow this exact checklist so the new library slots in the same way as the
existing ones (`File`/`System`/`Maths`/`Reactor`/`Hash`/`Structs`/`Re`):
请严格按此清单新增标准库，使其与既有库（File/System/Maths/Reactor/Hash/Structs/Re）一致：

1. **Backend** `lib/cpp/<name>.hpp` — mirror `file.hpp`/`maths.hpp`:
   / 后端 `lib/cpp/<name>.hpp`（参照 file.hpp/maths.hpp）：
   - `#include "../../src/builtin.hpp"` (relative to `lib/cpp/`).
   - Write each method as `rb::native_method([](env, paras){ … },
     rb::make_sign("m", {{in,type}}, {{out,type}}))`; read inputs with
     `rb::para_at(paras, i)` / `rb::number_of` / `rb::string_of`; return with
     `rb::list_of({…})` or `rb::empty_result()`; on bad input call
     `rb::native_error(…)`, which raises an immediate
     `RuntimeException` at the source (the value no longer propagates as poison).
     `env` *is* the object's attributes. **Declare `type` as a
     precise, namespace-qualified tag** — built-in scalars are `std::Number` /
     `std::String` / `std::Boolean`, containers `std::Array` / `std::Dict` /
     `std::Tuple`, and library classes use their module prefix (`re::Pattern`,
     `re::Match`, `async::Task`, `async::Reactor`, `async::Error`, `io::OStream`,
     …). These signatures are now **runtime-enforced** (see the Library
     reference note), so wrong-typed arguments are rejected at the call boundary.
     用 `rb::native_method` 写方法，借 `rb::make_sign` 声明签名；输入经
     `rb::para_at`/`rb::number_of`/`rb::string_of` 读取，返回用
     `rb::list_of`/`rb::empty_result`；非法输入调 `rb::native_error`，现已在源头
     即时抛出 `RuntimeException`（不再以毒水传播）。
     `env` 即对象属性表。**`type` 须声明为精确的、带命名空间的类型标签**——
     内置标量用 `std::Number`/`std::String`/`std::Boolean`，容器用
     `std::Array`/`std::Dict`/`std::Tuple`，库类用其模块前缀（`re::Pattern`、
     `re::Match`、`async::Task`、`async::Reactor`、`async::Error`、`io::OStream`
     等）。这些签名现已**在运行时强制约束**（见“标准库参考”说明），类型不符的
     实参会在调用边界被拒收。
   - `init_<name>_stdlib()`: build a `ClsProto` on `::stdRT.getcls("Object")`,
     `set_method(...)`, `regcls("<name>", proto)`, `::stdRT.add_protos(p)`.
   - End the file with the self-registration line:
     末尾自注册：
     `inline bool _registered = (rt_builtin::register_native_lib("<name>", &init_<name>_stdlib), true);`
2. **Interface** `lib/<name>.synl` — signatures only, mirroring the `.hpp`
   shapes; the class type is `$<CapitalizedName>` (the `$`-suffix is used
   **verbatim and capitalized**, e.g. `$Maths`, `$System`, `$Reactor`,
   `$Hash`, `$File`, `$Structs`, `$Re`). A `$Program` here would be ignored.
   Since then this face may also create top-level **preset object instances**
   (`-(<module>::<Type>! <module>::<name>);`) — const globals that arrive with
   the import (io's `io::out`/`io::in`, maths' `maths::math`). The name MUST
   carry the module prefix: the object belongs to the library's namespace,
   not to the program's bare scope. Add presets here when a stateless class
   would otherwise force every user to instantiate it.
   / 接口 `lib/<name>.synl`：仅签名，与 .hpp 形态对应；类名为 `$<首字母大写名>`
   （`$` 后后缀**原样且首字母大写**，如 `$Maths`/`$System`/`$Reactor`/`$Hash`
   /`$File`/`$Structs`/`$Re`）。库内的 `$Program` 会被忽略。
   当前，此文件还可创建顶层**预置对象实例**
   （`-(<模块>::<类型>! <模块>::<名字>);`）——随导入到来的常数全局对象
   （io 的 io::out/io::in、maths 的 maths::math）。名字**必须**带模块前缀：
   对象属于库的命名空间，而非程序的裸作用域。若一个无状态类只会迫使用户
   人手实例化，就应在这里加预置对象。
3. **Aggregate** — add `#include "<name>.hpp"` to `lib/cpp/std_libs.hpp`.
   / 聚合：在 `lib/cpp/std_libs.hpp` 加 `#include "<name>.hpp"`。
4. **Demo** `verify/philosophy/<name>_demo.syn` — a runnable program that
   exercises the library; verify its printed output by hand.
   / 示例：写 `verify/philosophy/<name>_demo.syn` 并人工核对其输出。
5. **Rebuild + verify** — run `bash build.sh`, then the unit suites
   (`assert_lexer`/`assert_parser`/`assert_runtimes`) and the new demo.
   / 重建并验证：`bash build.sh`，再跑三个单元测试套件与新示例。
