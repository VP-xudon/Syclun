# Standard Library Objects
## 标准库对象使用

> 每个模块的「能跑的示例」。复制即可运行（记得在顶部 `&模块;`）。
> Runnable examples for every module. Copy-paste to run (add `&module;` at the top).

---

## io — 标准流 / Standard streams

`&io;` 随带常数对象 `io::out` / `io::in`。
`&io;` ships const objects `io::out` / `io::in`.

```text
&io;
$Program {
    @:: << [{
        io::out << "不换行";              # out << v 或 out.push(v)，不换行
        io::out.push_line("换行");        # 写出并换行 / write + newline
        -(std::String name);
        name << io::in.get();            # 读一个词 / one word
        io::out.push_line(name);
        -(std::String line) << io::in.get_line();  # 读一整行 / whole line
        io::out.push_line(line);
    }];
}
```

- `OStream`：`out << v`（= `out.push(v)`，不换行）、`out.push_line(v)`（换行）。
- `IStream`：`x << in`（= `x =: in` = `x << in.get()`，读一词）、`x << in.get_line()`（读一行）。EOF 抛 `RuntimeException: input stream ended`。

---

## maths — 标量数学 / Scalar math

```text
&io; &maths;
$Program {
    @:: << [{
        -(std::Number x) << maths::math.sqrt(2);
        io::out.push_line(x);                 # ≈ 1.4142
        io::out.push_line(maths::math.pow(2, 10));   # 1024
        io::out.push_line(maths::math.pi());         # 3.14159…
        io::out.push_line(maths::math.random());     # 随机数 / random
    }];
}
```

方法：`abs` `sqrt` `pow` `floor` `ceil` `round` `sin` `cos` `tan` `log` `log10` `exp` `mod` `min` `max`；常数 `pi()` `e()` `random()`。参数为 `std::Number`，返回 `std::Number`。

---

## file — 文件 / Files

```text
&io; &file;
$Program {
    @:: << [{
        -(file::File f);
        f.open("note.txt");
        f.write("hello file\n");
        f.append("more\n");
        io::out.push_line(f.read());          # 读全部 / read all
        io::out.push_line(f.size());
    }];
}
```

`File`：`open` / `read` / `readlines` / `write` / `append` / `exists` / `remove` / `size`；属性 `path` / `mode`。

---

## system — 进程、环境与时钟 / Process, env, clock

```text
&io; &system;
$Program {
    @:: << [{
        -(system::System s);
        io::out.push_line(s.now());            # 毫秒时间戳 / ms epoch
        io::out.push_line(s.datetime());       # "YYYY-MM-DD HH:MM:SS"
        s.wait(500);                          # 休眠 500ms / sleep
        -(std::Array lines) << s.run_lines("echo hi");  # 跑 shell / run shell
        io::out.push_line(lines);
    }];
}
```

- 时间：`now()`→`Number`、`time()`→`"HH:MM:SS"`、`date()`→`"YYYY-MM-DD"`、`datetime()`→全格式（本地时区）。
- `wait(ms)` 阻塞当前线程 `ms` 毫秒。
- `run` / `run_lines` 经 `popen` 执行命令；`cwd` / `getenv` 取工作目录与环境变量。

---

## structs — 数据结构 / Data structures

C++ 底层；每个实例的 C++ 状态存于按实例 id 索引的注册表，故对象可经流存活。
C++-backed; per-instance C++ state lives in a registry keyed by a stable instance id, so objects survive flows.

```text
&io; &structs;
$Program {
    @:: << [{
        -(structs::Queue q);
        q.push(1); q.push(2); q.push(3);
        io::out.push_line(q.pop());           # 1 (FIFO)
        io::out.push_line(q.size());

        -(structs::Stack st);
        st.push("a"); st.push("b");
        io::out.push_line(st.top());          # "b" (LIFO)

        -(structs::Map m);
        m.put("k", 10);
        io::out.push_line(m.get("k"));        # 10
        io::out.push_line(m.has("k"));        # true

        -(structs::Graph g);
        g.add_node(1); g.add_node(4); g.add_edge(1, 2, 1); g.add_edge(2, 4, 1);
        io::out.push_line(g.shortest_path(1, 4));  # [1,2,4]
    }];
}
```

- `Queue` `push`/`pop`(FIFO)/`peek`/`size`/`empty`/`clear`/`to_array`
- `Stack` `push`/`pop`(LIFO)/`top`/…
- `Tree` 二叉搜索（`std::Number`）：`insert`/`contains`/`min`/`max`/`remove`/`inorder`/`preorder`/`postorder`
- `Map` `put`/`get`/`has`/`remove`/`keys`/`values`/`size`（键可为 `Number`/`String`/对象）
- `Graph` 无向带权图：`add_node`/`add_edge`/`neighbors`/`bfs`/`shortest_path`

---

## re — 正则表达式 / Regular expressions

`Re` 是静态便捷类（边编译边跑）；`Pattern` 是编译后的正则。
`Re` is the static convenience class (compiles on the fly); `Pattern` is a compiled regex.

```text
&io; &re;
$Program {
    @:: << [{
        -(re::Re r);
        io::out.push_line(r.findall(-(std::String p) << "\\d+",
                                    -(std::String t) << "a1b22c333"));   # [1, 22, 333]
        io::out.push_line(r.test(-(std::String p) << "\\d",
                                 -(std::String t) << "x9y"));            # true
        io::out.push_line(r.replace(-(std::String p) << "\\d+",
                                   -(std::String t) << "a1b22",
                                   -(std::String repl) << "#"));          # a#b#
        -(re::Match m) << r.search(-(std::String p) << "a+",
                                   -(std::String t) << "baaa");
        io::out.push_line(m.text);        # "aaa"
        io::out.push_line(m.groups);      # 捕获组 / capture groups
    }];
}
```

- `Re`：`compile(pattern)`→`Pattern`；`match`/`search`→`Match`；`findall`/`split`→`Array`；`replace`→`String`；`test`→`Boolean`。
- `Pattern`：`match(text)`/`search(text)`/`findall(text)`/`split(text)`/`replace(text,repl)`/`test(text)`。
- `Match`（字段非方法）：`matched`→`Boolean`、`text`→`String`、`groups`→`Array`、`start`/`end`→`Number`。

---

## hash — 哈希 / Hashing

```text
&io; &hash;
$Program {
    @:: << [{
        -(hash::Hash h);
        io::out.push_line(h.sha256("abc"));   # 64 位小写十六进制 / lowercase hex
        io::out.push_line(h.crc32("abc"));    # Number (< 2^32)
        io::out.push_line(h.fnv1a("abc"));    # 16 位小写十六进制
    }];
}
```

`Hash`：`sha256(text)`→`String`、`crc32(text)`→`Number`、`fnv1a(text)`→`String`（输入均为 `std::String`）。

---

## async — 异步运行时 / Async runtime

基于 `std::async`/`std::future`，覆盖生命周期、并发/背压、异常隔离、动态派发、非阻塞定时器。
Built on `std::async`/`std::future`: lifecycle, concurrency/backpressure, fault isolation, dynamic spawn, non-blocking timers.

```text
&io; &async;
$Program {
    @:: << [{
        -(async::Reactor r);
        r.set_limit(4);                       # 并发上限（0=不限，背压）/ max in-flight
        r.set([
            [{ io::out << "task A"; }],
            [{ io::out << "task B"; }]
        ]);
        -(std::Tuple res) << r.start();       # → [(status,payload), …]
        io::out.push_line(res);
    }];
}
```

- `$Reactor`：`set(tasks)`/`set_limit(max)`/`set_timeout(ms)`/`cancel()`/`start([timeout])`→`(status,payload)` 元组数组/`with_timeout(closure,ms)`/`spawn(closure)`/`submit(closure)`→`Task`/`async_sleep(ms)`→`Task`。`status`∈{`ok`,`error`,`timeout`,`cancelled`}；失败任务转为 `error`，`payload` 为 `Error`（反应堆绝不因单个坏任务崩溃）。
- `$Task`：`await([timeout])`→`(status,payload)`、`result()`、`cancel()`、`is_done()`→`Boolean`。
- `$Error`：`message()`→`String`、`kind()`→`String`（`exception`/`timeout`/`cancelled`/…）。

---

## assert — 运行期校验 / Runtime checks

```text
&io; &assert;
$Program {
    @:: << [{
        -(assert::Checker c);
        -(Counter x);                         # 见下方 Counter
        io::out.push_line(c.has_method(x, "get"));   # true：x 当前有 get 方法
        io::out.push_line(c.has_changed(x));         # false：尚未运行期重绑
    }];
}
```

`assert::Checker` 取代旧的「毒水」诊断——事前显式断言合法性，而非依赖静默传播的受检值。
`assert::Checker` replaces the old poison-water diagnostics: assert legality up front.

- `has_method(target[std::Object], name[std::String])` → `Boolean`
- `has_changed(target[std::Object])` → `Boolean`

---

## sugar — 语法糖：算术表达式求值 / Sugar: expression evaluator

```text
&io; &sugar;
$Program {
    @:: << [{
        -(sugar::Infix e) << "1+(2-3)*(3+5)";
        io::out.push_line(e.parse());         # -7
        -(std::Dict env);
        env.put("a", 2);
        e.env(env);                           # 绑定标识符 / bind identifiers
        -(sugar::Infix e2) << "a+3";
        e2.env(env);
        io::out.push_line(e2.parse());        # 5
    }];
}
```

`$Infix`：`-(sugar::Infix("expr") e)`（或 `e.set("...")`）存表达式；`e.env(dict)` 绑定标识符；`e.parse()` 求值返回 `std::Number`。支持 `+ - * / %`、括号、一元负号。

---

👉 写自己的库：[标准库总览 / Standard Libraries](Standard-Libraries) · 语法基础：[基本语法 / Basic Syntax](Basic-Syntax)
