# Reddit post — r/programming (and friends)

Promotional post for **Syclun / Synth-OOP**. Everything below is **verified
against the current tree** (2026-08-30): the code samples were run, the numbers
were taken from a real `build.sh --test` run. Do not edit the numbers without
re-running things.

宣传贴草稿。下面所有内容都已针对当前仓库核验（2026-08-30）：代码样例实跑过，数字
来自真实的 `build.sh --test`。改数字前请重跑。

---

## 0. Before you post / 发之前先看这里

| Check | Status |
|-------|--------|
| Repo URL | **MISSING** — `<<REPO_URL>>` below must be filled in. There is no git remote and no public repo yet, so the post is not postable until you publish one. |
| Release assets | Five packages are produced by `.github/workflows/release.yml` **on tag push**. Locally only `synth-windows-x64` was built (the other four need their own host). Push a tag before claiming "download a release". |
| Numbers in the post | 83 lexer + 64 parser + 143 runtime = **290** assertions; **~11,000** lines of C++23 (10,178 non-blank across `src/` + `lib/cpp/`). Both verified today by a fresh `bash build.sh --test`. |
| Samples | `hello.syn`, `flow.syn`, `counter.syn`, the `sugar::Infix` line, the `if_`-with-two-behaviors snippet, `a.=(1)`, and the error block were **all executed today**. The error block is real output, copied verbatim. |
| Exit codes | Verified fixed: `0` on success, `1` on syntax errors, `1` on runtime errors, `1` on a missing file. The post may claim this. / 已复验修复：成功 0、语法错误 1、运行期错误 1、缺文件 1。贴子里可以这么写。 |

**One honest warning about the subreddit.** r/programming removes a lot of
self-promotion, and "I made a language" posts land badly there unless the
technical substance carries the post. Recommended order:

1. **r/ProgrammingLanguages** — the right room for this. Post it there first.
2. **r/Compilers** — if you frame it around the tree-walking interpreter.
3. **r/programming** — only if the first two go well, and only with title
   option C (technical hook first, age second).

See §4 for the full playbook.

---

## 1. Title options / 标题选项

**A — closest to what you asked for / 最贴合你的原话**
> I'm 14. I got tired of endless syntactic sugar and half-baked OOP, so I wrote my own programming language.

**B — recommended / 推荐（年龄钩子 + 技术钩子各占一半）**
> I'm 14, so I did the reasonable thing: wrote a language with no operators, no global scope, and no assignment.

**C — for r/programming specifically / 投 r/programming 用这个**
> A language where `a + b` doesn't exist: a 14-year-old's C++23 tree-walking interpreter, and why deleting operators fixed more than it broke.

Use **B** for r/ProgrammingLanguages, **C** for r/programming. A reads as
clickbait on a technical subreddit and invites "ok, and?" — B and C put a
concrete technical claim in the title so people know what they're clicking.

---

## 2. The post / 正文

Copy from here down. Reddit markdown; triple-backtick blocks render fine in the
new editor (or paste into the "Code Block" widget in the fancy editor).

---

I'm 14. Over the last while I built **Syclun**, a tree-walking interpreter for a
language I designed called **Synth-OOP**, and I want to show you the three
decisions that made it feel different from anything else I've used.

It's roughly 11,000 lines of C++23 with zero dependencies outside the standard
library, it ships prebuilt binaries for five platforms, and 290 assertions pass.
It is, genuinely, a toy. But it runs real programs, and I think a few of its
ideas are worth stealing.

### The bet: what if you *remove* the sugar instead of adding it?

Every language I touched kept growing operators and special cases. `??`, `?.`,
`=>`, `::`, `...`. Each one is a tiny rule you have to memorise, and half of them
only work on the built-in types.

So I deleted them. **Synth-OOP has no infix operators at all.** `+` is not
syntax. It's a method name:

```
-(std::Number x) << 2;
-(std::Number y) << 3;
out.push_line(x.+(y));        // -> 5
```

The payoff nobody expects: `+` is **yours**. Any class can define it, and it
behaves exactly like any other method. There's no "why can't I overload `+` for
my type" problem, because there was never a privileged `+` to begin with.

Same trick for the rest of the "special" parts of a language:

- **No conditional keyword.** `if_` is a method on `std::Boolean` that takes two behaviors.
- **No loop keyword.** `while_` is on `std::Boolean` too; `repeat_` is on `std::Number` (the number *is* the count).
- **No constructor keyword.** `@::` is just a method with a reserved name.

Branches are ordinary behaviors, so you can store one in a variable and pass it
around. Once you notice that everything is a method call, the whole language
fits in your head. That was the goal.

### Decision 2: no global scope. A program *is* an object.

There is no top-level code. The entry point is the constructor of `$Program`,
and instantiating it **is** the program's lifetime:

```
&io;
$Program {
    @:: << [() -> () {
        -(io::OStream out);
        out.push_line("Hello, Synth-OOP!");
    }];
}
```

No globals to mutate, no import-order side effects, no "where does `main()` get
its state from". Teardown is the destructor `~`.

### Decision 3: assignment is doing two jobs, so I split it

`=` means "create" or "overwrite" depending on whether the left side happens to
be new. I separated them:

- `-(std::Number radius) << 3;` — `<<` is a **flow**: the right side *publishes* (`=:`) and the left side *receives* (`:=`). Both are overridable methods, so you can make a value that validates everything written into it.
- **Nothing is ever uninitialised.** The zero-value law says every fresh instance carries a default (`0`, `""`, `false`), so `-(std::Number n); out.push_line(n);` prints `0` instead of garbage.
- A variable can carry a **constraint** (`-(Adder[Addable] a)`) that's checked at runtime the moment a value is bound to it.

### The part I'm actually proudest of: the errors

Because there's no poison value quietly flowing through your program, every
failure is raised where it happens, and it looks like this:

```
Synth-OOP error
  Type     RuntimeException
  Location demo.syn, line 11, column 9
  Message  Method 'crash' not found.

    11 |         n.crash();
       |         ^~~~

Call stack
    [1] in '@::' (constructor of 'Program')  (demo.syn, line 3, column 1)
```

Source line, caret, and the **full call stack**, colourised by default
(`NO_COLOR=1` turns it off). It's deliberately g++-shaped, because that's the
error format I already knew how to read.

And it exits **non-zero** — so `synth bad.syn && echo ok` does what you'd
expect, and CI notices. (That was a real bug I only found because someone asked
"what happens in a pipeline?" — worth checking in your own projects.)

### Yes, I named the sugar library `sugar`. I'm not above self-awareness.

I dislike sugar creep, but I also don't enjoy writing `x.+(y.*(z))` twenty
times. So the sugar is opt-in and lives in a library with an honest name:

```
&sugar;
-(sugar::Infix("1+(2-3)*(3+5)") e);
out.push_line(e.parse());      // -> -7
```

If you want infix expressions, import them. The core language stays clean.

### What's actually in it

Ten standard-library modules, all with runtime-checked signatures: `io`, `file`,
`system`, `maths`, `hash` (sha256 / crc32 / fnv1a), `structs`
(Queue / Stack / Tree / Map / Graph), `re` (regex), `async` (a real Reactor with
concurrency limits, per-task timeouts, cancellation and error isolation),
`assert`, and `sugar`.

### Honest limitations, because you'll hit them in five minutes anyway

- It's a **tree-walking** interpreter. It is slow. Nobody is writing a web server in this.
- Ownership is `shared_ptr` refcounting. There is no cycle collector.
- `std::Object` can only carry **scalars** through a flow, so constraints need concrete types to work end-to-end. This is the limitation I'd fix first.
- Recursion is capped at 1000, with a real guard (and a 32 MB stack so the guard fires instead of the process segfaulting).
- The spec is **bilingual**, but the Chinese version is primary and the English translation is younger than it. That's the single biggest thing standing between you and reading it.
- Syntax freezes at 1.0.0. I'd rather ship a small, frozen grammar than another language that grows a new operator every year.

### Try it

Prebuilt, no compiler needed — Windows x64 / ARM64, macOS Intel / Apple Silicon,
Linux x64:

```
# unzip, then
bin/synth examples/hello.syn          # macOS / Linux
bin\synth.exe examples\hello.syn      # Windows
```

Or build it yourself: CMake + C++23, and `./build.sh --test` runs all three
suites (lexer 83, parser 64, runtime 143).

Repo: <<REPO_URL>>

I'd genuinely like to know which of these decisions you think is a mistake. The
"everything is a method call" one is the one I'd bet money on — and the one I'd
most like someone to talk me out of.

---

## 3. OP's first comment / 建议作为一楼评论发出

Reddit convention: put links and the predictable FAQ in a first comment so the
post itself stays readable. Also means you can edit it later.

---

Links, and answers to the questions I expect:

**Repo:** <<REPO_URL>> — GPL-3.0-or-later. Note: programs *you* write in
Synth-OOP are explicitly **not** covered by that licence; only the interpreter
is. It's written into the LICENSE.

**"Why is `<<` an assignment?"** It isn't. `<<` is a flow — the right side calls
its publish method (`=:`) and the left side calls its receive method (`:=`).
Both are ordinary methods you can override. `=` also exists as a method
(`a.=(b)`) and is the same thing.

**"Did you actually write this at 14?"** Yes. It's ~11k lines of C++23 across
`src/` and `lib/cpp/`. The git history is public if you want to check the pace.

**"How is `if_` a method?"**
```
-(std::Boolean ok) << (a.>(b));
ok.if_([() -> () { out.push_line("bigger"); }],
       [() -> () { out.push_line("smaller"); }]);
```
Both branches are behaviors (closures), so they're just values until `if_` calls
one.

**"Why C++ and not Rust/Zig?"** Because I knew C++ and wanted to spend the time
on the language, not on the implementation language. Also `std::regex` and
`std::async` gave me the stdlib modules for free.

**"Is it compiling or interpreting?"** Tree-walking interpreter. A bytecode VM
is the obvious next step and I haven't done it.

**Specs / docs:** `doc/` in the repo — Chinese and English versions of the
language spec (v1.28), plus a separate standard-library reference.

**Contributing:** `CONTRIBUTING.md` has the PR format and the rejection list.
Two rules worth knowing: after 1.0.0 the syntax is frozen, and I'll send back any
PR that opens a needless new standard-library module.

---

## 4. Playbook / 发帖策略

**Where and when**

| Where | When | Notes |
|-------|------|-------|
| r/ProgrammingLanguages | first, Tue–Thu, 8–10am US Eastern | The right audience. They *want* language-design posts. |
| r/Compilers | a day or two later | Frame it around the interpreter, not the age. |
| r/programming | only if the above went well | High removal risk. Use title C. Don't crosspost the same text. |

Do **not** post to more than one subreddit on the same day — it reads as spam
and can get you filtered.

**Honesty guardrails (non-negotiable — Reddit will check)**

- Every number in the post is true as of today. If you change the code, re-run `./build.sh --test` and update 290 / 11,000.
- Do not claim it's fast, production-ready, or that you have users. You don't, yet.
- Don't delete the limitations section to make the post look better. It's the part that earns you the benefit of the doubt. Admitting "`std::Object` can't carry objects through a flow" is exactly what makes a reader trust the rest.
- The spec's English translation being younger than the Chinese is a real weakness — stating it up front costs you nothing and pre-empts the first reply.

**Handling the age question**

Someone will ask "really 14?" Answer once, plainly, point at the git history,
and move on. Don't argue about it. Also expect: "this is impressive *for 14*" is
a compliment you should neither fish for nor reject loudly.

**Expect these replies, and have an answer ready**

1. *"This is just Smalltalk / Io / Self with worse syntax."* — Fair. Say so: the novelty is the package, not any single idea, and the zero-value law plus flow-only binding is the part you haven't seen combined.
2. *"`a.+(b)` is unreadable."* — Agreed, that's why `&sugar;` exists. Show the `Infix` example.
3. *"Why no `return`?"* — Outputs are named parameters in the signature: `[(x) -> (result) { result << x; }]`.
4. *"What about performance?"* — Tree-walking, slow, not the point.

**Things that will get the post removed**

- No repo link, or a link to something that 404s → **fix `<<REPO_URL>>` first**.
- Posting the same text to five subreddits in an hour.
- Editing the title after posting to chase upvotes.
- Arguing in the comments. Answer the technical ones, ignore the rest.

**Already fixed — no blocker before posting / 已修复，发之前没有障碍了**

The exit-code bug that was here earlier (syntax and runtime errors returning 0)
has been fixed and verified: `0` on success, `1` on syntax errors, `1` on
runtime errors, `1` on a missing file. It's also covered by an assertion now —
the parser suite went 63 → 64 — so it can't silently regress. The post mentions
this on purpose; it's the kind of detail that buys credibility.

**Optional, if you want more reach later**

- A 60-second terminal recording (asciinema) of `hello.syn` → the error demo → `sugar::Infix`. Reddit engagement triples with a visual.
- A follow-up post in a month: "what I changed after r/ProgrammingLanguages tore into my language" — that format performs extremely well.

---

## 5. Fact sheet / 数字备查

Verified 2026-08-30, from a fresh `bash build.sh --test` on Windows (MinGW-w64, C++23):

| Fact | Value |
|------|-------|
| Assertions | 83 lexer + 64 parser + 143 runtime = **290** |
| Exit codes | 0 success / 1 syntax error / 1 runtime error / 1 missing file |
| C++ lines | 10,914 total / 10,178 non-blank (`src/` + `lib/cpp/`) |
| Dependencies | C++23 standard library only |
| Build | CMake ≥ 3.16, ctest; `./build.sh [--test] [--clean] [--debug] [--ninja]` |
| Platforms | Windows x64 / ARM64, macOS Intel / Apple Silicon, Linux x64 |
| Stdlib modules | 10: `io` `file` `system` `maths` `hash` `structs` `re` `async` `assert` `sugar` |
| Indexing | 0-based |
| Recursion limit | 1000, with a 32 MB stack so the guard fires cleanly |
| Licence | GPL-3.0-or-later; user-written `.syn` programs are not covered |
| Docs | `doc/` — Chinese + English spec (v1.28), plus `Syclun标准库参考.md` |
