// ============================================================
// assert_runtimes.cpp
//
// Synth OOP runtime — built-in objects acceptance tests.
// Synth OOP 运行时 · 内置对象验收测试
//
// Acceptance target: the native objects implemented by builtin.hpp
// per the "Synth OOP Language Specification v1.25" (std::Object /
// Number / Boolean / String / Array / Dict / Tuple and io::OStream /
// io::IStream).
// 验收对象：builtin.hpp 按《Synth OOP 语言文档 v1.25》实现的
// 全部原生对象（std::Object / Number / Boolean / String / Array /
// Dict / Tuple 与 io::OStream / io::IStream）。
//
// Test case numbers map to the document appendix D and the chapters:
// 用例编号对应文档附录 D 与正文章节：
//   D.1  zero-value init              D.2  streamed assign & output
//   D.1  零值初始化            D.2  流式赋值与输出
//   D.3  instantiation expr          D.8  graceful degrade on bad negotiation
//   D.3  实例化表达式          D.8  协商失败优雅降级
//   D.16 control flow & env vars     D.18 tuple objectification
//   D.16 控制流与环境变量      D.18 元组对象化
//   2.2 / 6.2  generic make & signature constraints (Runtime.make /
//              CallableSign)
//   7.x  control flow                10.x poison water & _case
//   7.x  控制流                10.x 毒水模型与 _case
//
// Run:  ./assert_runtimes               static acceptance (no stdin)
//        echo "Yeah." | ./assert_runtimes --io
//                                         append live IStream input test
// 运行：  ./assert_runtimes              静态验收（不读标准输入）
//         echo "Yeah." | ./assert_runtimes --io
//                                        追加 IStream 实时输入验收
// ============================================================

#include <limits>
#include <cmath>
#include <string>
#include <vector>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <system_error>

// Platform declarations are pulled in manually (instead of including
// <windows.h> / <mach-o/dyld.h>) to keep the global namespace clean for the
// headers below.
// 手动引入平台声明（而非包含 <windows.h> / <mach-o/dyld.h>），
// 以免污染后续头文件的全局命名空间。
#if defined(_WIN32)
extern "C" __declspec(dllimport) unsigned long __stdcall
    GetModuleFileNameA(void* module, char* buf, unsigned long size);
#elif defined(__APPLE__)
extern "C" int _NSGetExecutablePath(char* buf, std::uint32_t* bufsize);
#elif defined(__linux__) || defined(__unix__) || defined(__FreeBSD__) \
      || defined(__OpenBSD__) || defined(__NetBSD__)
#include <unistd.h>
#endif

#include "../../src/builtin.hpp"
#include "../../lib/cpp/io.hpp"   // self-register the io native library


namespace {

    // --------------------------------------------------------
    // Test skeleton: no classes, no macros — only free predicates and
    // counters.
    // 测试骨架：无类、无宏，只有自由的判定与统计。
    // --------------------------------------------------------

    int passed = 0;
    int failed = 0;

    void check(bool condition, const std::string& what) {
        if (condition) {
            ++passed;
            std::cout << "  [PASS] " << what << "\n";
        } else {
            ++failed;
            std::cout << "  [FAIL] " << what << "\n";
        }
    }

    void section(const std::string& title) {
        std::cout << "\n== " << title << " ==\n";
    }

    // --------------------------------------------------------
    // Minimal interpreter-side simulation
    // 解释器侧的最小仿真
    // --------------------------------------------------------

    using rt_builtin::empty_result;
    using rt_builtin::first_of;
    using rt_builtin::list_of;
    using rt_builtin::make_sign;

    // Unified method-call entry: built-in objects are all RuntimeClass
    // instances; on a bad cast (theoretically impossible) we return an empty
    // list so the predicate naturally fails.
    // 方法调用统一入口：内置对象都是 RuntimeClass 实例，
    // 转型失败（理论不可能）返回空列表，让判定自然为假。
    rt_basic::InstanceListPtr invoke(
        const runtime::RuntimeObjectPtr& object,
        const std::string& name,
        const rt_basic::InstanceListPtr& args
    ) {
        auto cls = std::dynamic_pointer_cast<runtime::RuntimeClass>(object);
        return cls ? cls->call_method(name, args) : empty_result();
    }

    // Flow statement (Spec 3.4.2): first call the sender's "=:" publish,
    // then the receiver's ":=" receive. Both spellings (>> and <<) share
    // the same semantics.
    // 流语句（文档 3.4.2）：先调用发送方 "=:" 公布，
    // 再调用接收方 ":=" 接收。两种写法（>> 与 <<）语义相同。
    rt_basic::InstanceListPtr flow(
        const runtime::RuntimeObjectPtr& from,
        const runtime::RuntimeObjectPtr& to
    ) {
        auto published = invoke(from, "=:", empty_result());
        return invoke(to, ":=", published);
    }

    // Method call (Spec 6.5): a method call is itself an expression.
    // 方法调用（文档 6.5）：方法调用本身就是表达式。
    rt_basic::InstanceListPtr call(
        const runtime::RuntimeObjectPtr& object,
        const std::string& name,
        std::initializer_list<runtime::RuntimeObjectPtr> args = {}
    ) {
        return invoke(
            object,
            name,
            std::make_shared<std::vector<runtime::RuntimeObjectPtr>>(args)
        );
    }

    // --------------------------------------------------------
    // Portable subprocess / path helpers / 可移植子进程与路径助手
    //
    // The interpreter binary sits NEXT TO this test binary in the build
    // directory. Resolving it from this test's OWN location - instead of a
    // hard-coded "build\\synth.exe" - keeps the suite working on Windows,
    // Linux, macOS and other Unix systems, and independent of the current
    // working directory and of the platform's executable suffix.
    // 解释器二进制与本测试二进制同处构建目录。从本测试自身位置解析它
    // （而非硬编码 "build\\synth.exe"），使套件在 Windows / Linux / macOS
    // 及其它 Unix 上均可用，且不依赖当前工作目录与平台可执行文件后缀。
    // --------------------------------------------------------

    namespace fs = std::filesystem;

    // Absolute path of the running executable ("" when it cannot be found).
    // 运行中可执行文件的绝对路径（无法确定时为空串）。
    fs::path current_exe_path() {
#if defined(_WIN32)
        char buf[32768];
        unsigned long n = GetModuleFileNameA(
            nullptr, buf, (unsigned long)sizeof(buf));
        return n ? fs::path(std::string(buf, n)) : fs::path();
#elif defined(__APPLE__)
        char buf[4096];
        std::uint32_t sz = (std::uint32_t)sizeof(buf);
        return _NSGetExecutablePath(buf, &sz) == 0 ? fs::path(buf) : fs::path();
#elif defined(__linux__) || defined(__unix__) || defined(__FreeBSD__) \
      || defined(__OpenBSD__) || defined(__NetBSD__)
        char buf[4096];
        ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0) return fs::path();
        buf[n] = '\0';
        return fs::path(buf);
#else
        return fs::path();   // unknown platform: fall back to the cwd
#endif
    }

    // Directory holding this test binary (and, next to it, the interpreter).
    // 存放本测试二进制（以及其旁的解释器）的目录。
    fs::path test_binary_dir() {
        fs::path p = current_exe_path();
        return p.empty() ? fs::path(".") : p.parent_path();
    }

    // Path of the interpreter executable, with the platform's suffix.
    // 解释器可执行文件的路径（带平台后缀）。
    fs::path synth_exe_path() {
        fs::path p = test_binary_dir() / "synth";
#if defined(_WIN32)
        p += ".exe";
#endif
        return p;
    }

    // Is the interpreter present beside this test binary? Checked once at
    // startup so a missing binary fails loudly instead of every subprocess
    // check quietly "passing" on an empty capture.
    // 解释器是否存在于本测试二进制旁？启动时检查一次，使缺失的二进制
    // 大声失败，而非让每个子进程检查都在空输出上悄然“通过”。
    bool synth_available() {
        std::error_code ec;
        return fs::exists(synth_exe_path(), ec);
    }

    // Quote an argument so paths containing spaces survive the shell.
    // 为实参加引号，使含空格的路径能安全穿过 shell。
    std::string quote_arg(const fs::path& p) {
        return "\"" + p.string() + "\"";
    }

    // Assemble "<exe> <script> 2>&1" for the host shell.
    // 为主机 shell 组装 "<exe> <script> 2>&1"。
    //
    // On Windows popen() hands the line to the command interpreter, which
    // applies legacy quote handling: a command line that BEGINS with a quote
    // is parsed specially and fails with "The filename, directory name, or
    // volume label syntax is incorrect". Wrapping the WHOLE line in one more
    // pair of quotes is the documented workaround. POSIX shells need no such
    // wrapper (there the extra quotes would be read as a single command name).
    // Windows 上 popen() 把命令行交给命令解释器，后者沿用旧式引号处理：以
    // 引号【开头】的命令行会被特殊解析并报“文件名、目录名或卷标语法不正
    // 确”。把整行再包一层引号是文档记载的绕法。POSIX shell 不需要这层包装
    // （多余的引号会被当成单个命令名）。
    std::string build_cmd(const fs::path& exe, const fs::path& script) {
        std::string line = quote_arg(exe) + " " + quote_arg(script) + " 2>&1";
#if defined(_WIN32)
        return "\"" + line + "\"";
#else
        return line;
#endif
    }

    // MSVC spells the POSIX pipe API with a leading underscore; MinGW and
    // Unix-like systems expose the plain names.
    // MSVC 的管道 API 带前导下划线；MinGW 与类 Unix 系统用普通名字。
    FILE* portable_popen(const std::string& cmd) {
#if defined(_MSC_VER)
        return _popen(cmd.c_str(), "r");
#else
        return popen(cmd.c_str(), "r");
#endif
    }
    int portable_pclose(FILE* f) {
#if defined(_MSC_VER)
        return _pclose(f);
#else
        return pclose(f);
#endif
    }

    // Write `src` to a temp script beside the test binary, run the
    // interpreter on it, and return the combined stdout+stderr. The temp
    // script is removed afterwards (best effort).
    // 把 src 写到测试二进制旁的临时脚本，在其上运行解释器，返回合并的
    // stdout+stderr。随后（尽力）删除该临时脚本。
    std::string run_synth(const std::string& src, const char* tag) {
        fs::path script = test_binary_dir()
                        / ("_" + std::string(tag) + "_test.syn");
        {
            std::ofstream outf(script);
            if (!outf) return "";
            outf << src;
        }
        std::string cmd = build_cmd(synth_exe_path(), script);
        FILE* pipe = portable_popen(cmd);
        std::string all;
        if (pipe) {
            char buf[1024];
            while (fgets(buf, sizeof(buf), pipe)) all += buf;
            portable_pclose(pipe);
        }
        std::error_code ec;
        fs::remove(script, ec);
        return all;
    }

    // Run a snippet and assert it reports an error containing `needle`
    // (the retired poison-water model now aborts the run with a traceable
    // diagnostic instead of silently degrading).
    // 运行源码并断言其报错输出包含 `needle`（退役的毒水模型现在直接中断
    // 运行并给出可追踪诊断，而非静默降级）。
    bool expect_runtime_error(
        const std::string& src, const std::string& needle
    ) {
        return run_synth(src, "neg").find(needle) != std::string::npos;
    }

    // Run a snippet and assert it completes WITHOUT a runtime error. The
    // marker is the real diagnostic header ("Synth-OOP error"); an empty
    // capture (interpreter missing) must NOT silently count as a clean run.
    // 运行源码并断言其【未】触发运行时错误。标记为真实的诊断标题
    // （"Synth-OOP error"）；空输出（解释器缺失）不得被当作干净的通过。
    bool expect_clean_run(const std::string& src) {
        // An empty capture is a legitimate clean run (a program whose entry
        // point prints nothing), so only the real diagnostic header counts.
        // 空输出也是合法的干净运行（入口什么都不打印的程序），故只有真实
        // 的诊断标题才算失败。
        std::string all = run_synth(src, "pos");
        return all.find("Synth-OOP error") == std::string::npos;
    }

    // Run a snippet and assert its output contains `needle`. On a clean run
    // the output lands on stdout (captured via 2>&1); on failure it lands on
    // stderr instead, so a missing needle correctly fails the check.
    // 运行源码并断言其输出包含 `needle`。成功时输出落 stdout（经 2>&1
    // 一并捕获）；失败时落 stderr，needle 缺失即判失败。
    bool expect_program_output(
        const std::string& src, const std::string& needle
    ) {
        return run_synth(src, "pos").find(needle) != std::string::npos;
    }

    // Behavior literal (Spec 5.1): [(params) mode (outputs) { body }].
    // Interpreter-side behavior: construct a Callable directly (non-const
    // binding, signature <test>).
    // 行为字面量（文档 5.1）：[(参数) 模式 (输出) { 函数体 }]。
    // 解释器侧行为：直接构造 Callable（非常数绑定，签名 <test>）。
    runtime::RuntimeObjectPtr behavior(rt_basic::NativeClosure body) {
        return rt_builtin::make_behavior(
            rt_basic::Callable(body, make_sign("<test>"), {}, {false, false})
        );
    }

    // Convenience accessor for a single return value (returns a sentinel on
    // failure so the predicate naturally fails).
    // 读取单返回值的便捷通道（失败给哨兵值，判定自然为假）。
    double num(const rt_basic::InstanceListPtr& result) {
        return rt_builtin::number_of(first_of(result)).value_or(
            std::numeric_limits<double>::quiet_NaN()
        );
    }

    std::string text(const rt_basic::InstanceListPtr& result) {
        return rt_builtin::string_of(first_of(result)).value_or("<none>");
    }

    bool flag(const rt_basic::InstanceListPtr& result) {
        return rt_builtin::boolean_of(first_of(result)).value_or(false);
    }

    double num_of(const runtime::RuntimeObjectPtr& object) {
        return rt_builtin::number_of(object).value_or(
            std::numeric_limits<double>::quiet_NaN()
        );
    }

    std::string text_of(const runtime::RuntimeObjectPtr& object) {
        return rt_builtin::string_of(object).value_or("<none>");
    }

    // --------------------------------------------------------
    // 1. Bootstrap and packages (Spec 9.2: std / io are preset namespaces)
    // 1. 引导与集合包（文档 9.2：std / io 是预置命名空间）
    // --------------------------------------------------------

    void test_bootstrap() {
        section("Bootstrap and packages (std / io Prototypes packages)");

        check(stdPT.getcls("Object") != nullptr,
            "std package registers the root Object");
        check(stdPT.getcls("Number") != nullptr
            && stdPT.getcls("Boolean") != nullptr
            && stdPT.getcls("String") != nullptr
            && stdPT.getcls("Array") != nullptr
            && stdPT.getcls("Dict") != nullptr
            && stdPT.getcls("Tuple") != nullptr,
            "std package registers Number / Boolean / String / Array / "
            "Dict / Tuple");
        check(stdRT.getcls("io::OStream") != nullptr
            && stdRT.getcls("io::IStream") != nullptr,
            "io package registers OStream / IStream (independent of std)");
        check(stdPT.getcls("OStream") == nullptr,
            "OStream is not in the std package — io and std are independent");
        check(stdPT.getcls("Nothing") == nullptr,
            "querying an unregistered prototype returns nullptr "
            "(left to the interpreter to throw)");

        // Idempotency: repeated bootstrap does not corrupt the registry.
        // 幂等：重复引导不破坏注册表。
        rt_builtin::init_builtins();
        check(stdPT.getcls("Number") != nullptr,
            "init_builtins() is idempotent");

        // Runtime environment: object registration and lookup.
        // 运行环境（Runtime）：对象登记与查询。
        auto out = rt_builtin::make_ostream();
        stdRT.defobj("out", out);
        check(stdRT.getobj("out") == out,
            "Runtime.defobj / getobj registers an io::OStream instance");
        check(stdRT.getobj("ghost") == nullptr,
            "Runtime.getobj returns nullptr for a non-existent object");
    }

    // --------------------------------------------------------
    // 2. Zero-value law and streamed output (D.1 / D.2 / D.3)
    // 2. 零值法则与流式输出（D.1 / D.2 / D.3）
    // --------------------------------------------------------

    void test_zero_and_flow() {
        section("Zero-value law and streamed output (D.1 / D.2 / D.3)");

        auto out = rt_builtin::make_ostream();

        // D.1: zero-value initialization happens automatically at declaration.
        // D.1：声明瞬间自动零值初始化。
        auto num0 = rt_builtin::make_int(0);
        check(rt_builtin::display(num0) == "0", "Number zero value is 0");
        check(rt_builtin::display(rt_builtin::make_string("")) == "",
            "String zero value is empty");
        check(rt_builtin::display(rt_builtin::make_boolean(false)) == "false",
            "Boolean zero value is false");
        check(num(call(rt_builtin::make_array(), "size")) == 0,
            "Array zero value is an empty array");
        check(num(call(rt_builtin::make_dict(), "size")) == 0,
            "Dict zero value is an empty dict");
        check(num(call(rt_builtin::make_tuple(), "size")) == 0,
            "Tuple zero value is an empty tuple");

        // D.2: streamed assignment and output.
        // D.2：流式赋值与输出。
        auto strlock = rt_builtin::make_string("");
        flow(rt_builtin::make_string("Yeah."), strlock);
        check(text_of(strlock) == "Yeah.", "String streamed assignment");
        flow(strlock, out);
        flow(rt_builtin::make_string("\n"), out);

        // D.3: an instantiation expression is an expression; its value is the
        // object itself.
        // D.3：实例化表达式是表达式，值就是对象本身。
        auto msg = rt_builtin::make_string("");
        flow(rt_builtin::make_string("Hello, World!"), msg);
        flow(msg, out);
        flow(rt_builtin::make_string("\n"), out);
        check(text_of(msg) == "Hello, World!",
            "instantiation expression value participates directly in flow");
    }

    // --------------------------------------------------------
    // 3. std::Number: arithmetic and comparison (Spec 5.5: an operation is
    //    a method call)
    // 3. std::Number：算术与比较（文档 5.5：运算即方法调用）
    // --------------------------------------------------------

    void test_number() {
        section("std::Number — arithmetic and comparison");

        auto a = rt_builtin::make_int(10);
        auto b = rt_builtin::make_int(20);

        check(num(call(a, "+", {b})) == 30, "a.+(b) == 30");
        check(num(call(a, "-", {b})) == -10, "a.-(b) == -10");
        check(num(call(a, "*", {b})) == 200, "a.*(b) == 200");
        check(num(call(a, "/", {b})) == 0.5, "a./(b) == 0.5");
        check(num(call(rt_builtin::make_int(10), "%", {rt_builtin::make_int(3)})) == 1,
            "10.%(3) == 1");

        // Chained operation (Spec 5.5 example): a.+(b).*(3).
        // 链式运算（文档 5.5 例子）：a.+(b).*(3)。
        auto chained = call(
            first_of(call(a, "+", {b})), "*",
            {rt_builtin::make_int(3)}
        );
        check(num(chained) == 90, "a.+(b).*(3) == 90");

        // Float: std::Number unifies integer and float (Spec 5.5).
        // 浮点：std::Number 统一承载整数与浮点（文档 5.5）。
        auto half = rt_builtin::make_float(0.5);
        check(num(call(half, "+", {half})) == 1.0, "0.5.+(0.5) == 1.0");
        check(text(call(half, "to_string")) == "0.5",
            "to_string produces shortest round-trip representation");
        check(text(call(rt_builtin::make_int(42), "to_string")) == "42",
            "integer to_string");

        // The six comparison methods (Spec 2.3.2).
        // 六个比较方法（文档 2.3.2）。
        auto one = rt_builtin::make_int(1);
        auto two = rt_builtin::make_int(2);
        check(flag(call(one, "<", {two})), "1.<(2)");
        check(flag(call(two, ">", {one})), "2.>(1)");
        check(flag(call(one, "<=", {one})), "1.<=(1)");
        check(flag(call(two, ">=", {two})), "2.>=(2)");
        check(flag(call(one, "==", {rt_builtin::make_int(1)})), "1.==(1)");
        check(flag(call(one, "!=", {two})), "1.!=(2)");
        check(!flag(call(one, ">", {two})), "!(1.>(2))");
    }

    // --------------------------------------------------------
    // 4. std::String (Spec 2.3.2)
    // 4. std::String（文档 2.3.2）
    // --------------------------------------------------------

    void test_string() {
        section("std::String — immutable character sequence");

        auto s = rt_builtin::make_string("Synth");

        check(text(call(s, "+", {rt_builtin::make_string(" OOP")})) == "Synth OOP",
            "concatenation returns a new string");
        check(text_of(s) == "Synth", "original unchanged (immutable semantics)");
        check(text(call(s, "upper")) == "SYNTH", "upper capitalizes");
        check(text(call(s, "lower")) == "synth", "lower lowercases");
        check(text(call(s, "reverse")) == "htnyS", "reverse reverses");
        check(num(call(s, "length")) == 5, "length is the character count");
        check(text(call(s, "get", {rt_builtin::make_int(0)})) == "S",
            "get(0) fetches the first character");
        check(flag(call(s, "contains", {rt_builtin::make_string("nth")})),
            "contains substring");
        check(!flag(call(s, "contains", {rt_builtin::make_string("xyz")})),
            "contains does not contain substring");
        check(text(call(s, "slice", {rt_builtin::make_int(1),
                                     rt_builtin::make_int(4)})) == "ynt",
            "slice(1, 4) returns the [1, 4) substring");
        // Out-of-bounds character access is now an immediate error (the
        // retired poison-water model no longer degrades silently).
        // 越界字符访问现为即时错误（退役的毒水模型不再静默降级）。
        check(expect_runtime_error(
            "&io;\n$Program {@:: << [() -> () {\n"
            "  -(io::OStream out);\n"
            "  -(std::String(\"Synth\")! s);\n"
            "  out << (s.get(99));\n"
            "}];};",
            "out of bounds"),
            "out-of-bounds character access raises an immediate error");
    }

    // --------------------------------------------------------
    // 5. Control flow: if_ / while_ / repeat_ (Chapter 7 / D.16)
    // 5. 控制流：if_ / while_ / repeat_（第七章 / D.16）
    // --------------------------------------------------------

    void test_control_flow() {
        section("Control flow — if_ / while_ / repeat_ (D.16)");

        auto out = rt_builtin::make_ostream();

        // D.16: repeat_ runs 3 times; state accumulates from 0 to 3.
        // D.16：repeat_ 执行 3 次，state 从 0 累加到 3。
        auto counter = behavior(
            [](rt_basic::InstanceMap& /*env*/,
               rt_basic::InstanceListPtr paras) {
                auto state = rt_builtin::number_of(
                    rt_builtin::first_of(paras)).value_or(0);
                return list_of({rt_builtin::make_int(
                    static_cast<std::int64_t>(state) + 1)});
            }
        );
        auto last = call(rt_builtin::make_int(3), "repeat_", {counter});
        check(num(last) == 3, "3.repeat_(state + 1) == 3");

        // if_: the condition is the caller; the true branch returns last, the
        // false branch returns 0.
        // if_：条件为调用者，真分支返回 last，假分支返回 0。
        auto last_value = first_of(last);
        auto picked = call(
            first_of(call(last_value, ">", {rt_builtin::make_int(2)})),
            "if_",
            {
                behavior([last_value](rt_basic::InstanceMap&,
                                      rt_basic::InstanceListPtr) {
                    return list_of({last_value});
                }),
                behavior([](rt_basic::InstanceMap&,
                            rt_basic::InstanceListPtr) {
                    return list_of({rt_builtin::make_int(0)});
                }),
            }
        );
        check(num(picked) == 3, "(3.>(2)).if_(true branch) == 3");

        // Boolean constant as condition (Spec 7.1 example 2).
        // 布尔常数作条件（文档 7.1 示例 2）。
        auto constant = call(
            rt_builtin::make_boolean(true),
            "if_",
            {
                behavior([](rt_basic::InstanceMap&,
                            rt_basic::InstanceListPtr) {
                    return list_of({rt_builtin::make_int(1)});
                }),
                behavior([](rt_basic::InstanceMap&,
                            rt_basic::InstanceListPtr) {
                    return list_of({rt_builtin::make_int(0)});
                }),
            }
        );
        check(num(constant) == 1, "(true).if_ takes the true branch");

        // D.16 output: 3 and 3.
        // D.16 输出：3 和 3。
        flow(last_value, out);
        flow(rt_builtin::make_string(" "), out);
        flow(first_of(picked), out);
        flow(rt_builtin::make_string("\n"), out);

        // while_ (Spec 7.2 example): state accumulates from 0 to 10.
        // while_（文档 7.2 示例）：state 从 0 累加到 10。
        auto grow = behavior(
            [](rt_basic::InstanceMap& /*env*/,
               rt_basic::InstanceListPtr paras) {
                auto state = rt_builtin::number_of(
                    rt_builtin::first_of(paras)).value_or(0);
                return list_of({rt_builtin::make_int(
                    static_cast<std::int64_t>(state) + 1)});
            }
        );
        auto below_ten = behavior(
            [](rt_basic::InstanceMap& /*env*/,
               rt_basic::InstanceListPtr paras) {
                auto state = rt_builtin::number_of(
                    rt_builtin::first_of(paras)).value_or(0);
                return list_of({rt_builtin::make_boolean(state < 10)});
            }
        );
        auto looped = call(
            rt_builtin::make_boolean(true), "while_", {grow, below_ten}
        );
        check(num(looped) == 10,
            "(true).while_(body, check) returns last state == 10");

        // Initial condition false: ends immediately, returns the zero state.
        // 初始条件为假：直接结束，返回零值 state。
        auto skipped = call(
            rt_builtin::make_boolean(false), "while_", {grow, below_ten}
        );
        check(num(skipped) == 0, "(false).while_ ends immediately, state stays zero");

        // Non-const contract loop body: the body mutates the caller's
        // environment directly (Spec 10.3).
        // 非常数契约循环体：body 直接修改调用者环境（文档 10.3）。
        double total = 0;
        auto accumulate = behavior(
            [&total](rt_basic::InstanceMap& /*env*/,
                     rt_basic::InstanceListPtr paras) {
                auto state = rt_builtin::number_of(
                    rt_builtin::first_of(paras)).value_or(0);
                total += state + 1;   // accumulate 1 to 5 / 累加 1 到 5
                return list_of({rt_builtin::make_int(
                    static_cast<std::int64_t>(state) + 1)});
            }
        );
        call(rt_builtin::make_int(5), "repeat_", {accumulate});
        check(total == 15, "repeat_ loop body accumulates 1 to 5 == 15");
    }

    // --------------------------------------------------------
    // 6. IEEE-754 float semantics + retired poison-water model (Chapter 10 / D.8)
    // 6. IEEE-754 浮点语义 + 已退役的毒水模型（第十章 / D.8）
    // --------------------------------------------------------

    void test_ieee754_and_no_poison() {
        section("IEEE-754 floats and the retired poison-water model");

        auto out = rt_builtin::make_ostream();

        // Division by zero: IEEE 754 result is ±Infinity (NOT poison — the
        // value is real and inspectable, per the reworked error model).
        // 除以 0：IEEE 754 结果为 ±Infinity（非毒水——该值真实可检查，
        // 依重构后的报错模型）。
        auto inf = call(rt_builtin::make_int(10), "/",
            {rt_builtin::make_int(0)});
        check(std::isinf(num(inf)) && num(inf) > 0,
            "10./(0) yields +Infinity");
        auto ninf = call(rt_builtin::make_int(-3), "/",
            {rt_builtin::make_int(0)});
        check(std::isinf(num(ninf)) && num(ninf) < 0,
            "(-3)./(0) yields -Infinity");

        // 0/0 yields NaN (the only non-finite that is not Infinity).
        // 0/0 得 NaN（唯一非 Infinity 的非有限值）。
        auto nan = call(rt_builtin::make_int(0), "/",
            {rt_builtin::make_int(0)});
        check(std::isnan(num(nan)), "0./(0) yields NaN");

        // IEEE propagation: Infinity stays Infinity, NaN stays NaN — these are
        // real, inspectable values, not silent poison.
        // IEEE 传播：Infinity 保持 Infinity、NaN 保持 NaN——真实可检查，
        // 而非静默毒水。
        auto spread = call(first_of(inf), "+", {rt_builtin::make_int(1)});
        check(std::isinf(num(spread)), "Infinity + 1 stays Infinity");
        auto nan_spread = call(first_of(nan), "+", {rt_builtin::make_int(1)});
        check(std::isnan(num(nan_spread)), "NaN + 1 stays NaN");

        // Comparison with Infinity is well-defined (no contagion, no crash).
        // 与 Infinity 比较语义确定（无传染、不崩溃）。
        auto compared = call(first_of(inf), "<", {rt_builtin::make_int(5)});
        check(!rt_builtin::is_error(first_of(compared)),
            "comparison with Infinity is not poisoned");

        // The poison-water catcher `_case` has been retired along with the
        // poison-water model. Calling it is now an ordinary "method not found"
        // error — the same path as any missing method (see the frobnicate test
        // below). This proves the user-facing request: `1._case()` must error
        // normally, not silently catch nothing.
        // 毒水捕获器 `_case` 已随毒水模型一并退役。调用它现在就是普通的
        // “方法未找到”错误——与任何缺失方法走同一路径（见下 frobnicate 测试）。
        // 这印证了用户诉求：`1._case()` 须正常报错，而非静默捕获。
        check(expect_runtime_error(
            "&io;\n$Program {@:: << [() -> () {\n"
            "  -(io::OStream out);\n"
            "  -(std::Number! n);\n"
            "  out << (n._case([() -> () { }]));\n"
            "}];};",
            "not found"),
            "the retired '_case' is gone: it now raises a normal 'not found' error");

        // NaN/Infinity confirm the reworked model: they are real, inspectable
        // values, never poison — so there is nothing for a (now-removed)
        // catcher to do. A direct assertion that NaN survives arithmetic.
        // NaN/Infinity 印证重构后的模型：它们是真实可检查的值，绝非毒水——
        // 故（已移除的）捕获器也无需动作。直接断言 NaN 在算术后存活。
        check(std::isnan(num(nan_spread)),
            "NaN is a real value (not poison) and survives arithmetic");

        // D.8: graceful degradation on bad negotiation — flow a String into a
        // Number; never force a conversion, x keeps the zero value, no crash.
        // D.8：协商失败优雅降级——把 String 流入 Number，
        // 绝不暴力转换，x 保持零值，程序不崩溃。
        auto x = rt_builtin::make_int(0);
        flow(rt_builtin::make_string("not a number"), x);
        check(num_of(x) == 0, "String into Number: bad negotiation keeps zero");
        check(!rt_builtin::is_error(x), "degradation is not poisoned (D.8)");
        flow(x, out);
        flow(rt_builtin::make_string("\n"), out);

        // Illegal arithmetic argument: the signature enforcer rejects a
        // non-std::Number operand at the call boundary — an immediate,
        // duck-typed error (the old poison degradation is gone).
        // 非法算术参数：签名约束器在调用边界拒收非 std::Number 实参——
        // 即时的鸭子式错误（旧的毒水降级已移除）。
        check(expect_runtime_error(
            "&io;\n$Program {@:: << [() -> () {\n"
            "  -(io::OStream out);\n"
            "  -(std::Number! n);\n"
            "  out << (n.+( \"one\" ));\n"
            "}];};",
            "type mismatch"),
            "Number.+(String) rejected at the call boundary (type mismatch)");

        // Missing method: now an immediate error, not a silent poison object.
        // 方法缺失：现为即时错误，而非静默毒水对象。
        check(expect_runtime_error(
            "&io;\n$Program {@:: << [() -> () {\n"
            "  -(io::OStream out);\n"
            "  -(std::Number! n);\n"
            "  out << (n.frobnicate());\n"
            "}];};",
            "not found"),
            "calling a non-existent method raises an immediate error");
    }

    // --------------------------------------------------------
    // 6b. Method rebinding + Checker standard library
    // 6b. 方法重绑 + Checker 标准库
    // --------------------------------------------------------

    void test_method_rebind_and_checker() {
        section("Method rebinding (c.method.=(beh) / c.method << beh) + Checker");

        // Dynamic method rebind via `.=`: a method variable is just a member
        // holding a behavior, so assigning a behavior swaps the callable in
        // place. The user's $Counter example must print 4.
        // 经 `.=` 的动态方法重绑：方法变量即持有行为的成员，对其赋值即就地
        // 替换 callable。用户的 $Counter 示例须输出 4。
        check(expect_program_output(
            "&io;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(Counter c);\n"
            "    c.inc(); c.inc();\n"
            "    c.inc.=([()->() { value << value.+(2); }]);\n"
            "    c.inc();\n"
            "    -(io::OStream out);\n"
            "    out << c.get();\n"
            "  }];\n"
            "}\n"
            "$Counter {\n"
            "  -(std::Number value);\n"
            "  @get << [() ~> (result) { result << value; }];\n"
            "  @inc << [() -> () { value << value.+(1); }];\n"
            "}\n",
            "4"),
            "dynamic method rebind via '.=' changes behavior (prints 4)");

        // Symmetric flow form `c.inc << beh` rebinds too.
        // 对称流形式 `c.inc << beh` 同样重绑。
        check(expect_program_output(
            "&io;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(Counter c);\n"
            "    c.inc(); c.inc();\n"
            "    c.inc << [()->() { value << value.+(5); }];\n"
            "    c.inc();\n"
            "    -(io::OStream out);\n"
            "    out << c.get();\n"
            "  }];\n"
            "}\n"
            "$Counter {\n"
            "  -(std::Number value);\n"
            "  @get << [() ~> (result) { result << value; }];\n"
            "  @inc << [() -> () { value << value.+(1); }];\n"
            "}\n",
            "7"),
            "symmetric flow form 'c.inc << beh' also rebinds (prints 7)");

        // A const method (@!inc) must refuse to be rebound.
        // 常数方法（@!inc）须拒绝被重绑。
        check(expect_runtime_error(
            "&io;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(Consty c);\n"
            "    c.inc.=([()->() { value << value.+(1); }]);\n"
            "  }];\n"
            "}\n"
            "$Consty {\n"
            "  -(std::Number value);\n"
            "  @!inc << [() -> () { value << value.+(1); }];\n"
            "}\n",
            "ConstException"),
            "rebinding a const method (@!inc) raises ConstException");

        // Checker standard library: has_method / has_changed.
        // Checker 标准库：has_method / has_changed。
        check(expect_program_output(
            "&io;\n"
            "&assert;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(Counter c);\n"
            "    -(assert::Checker chk);\n"
            "    -(io::OStream out);\n"
            "    out << chk.has_method(c, \"inc\");\n"
            "    out << \";\";\n"
            "    out << chk.has_changed(c);\n"
            "    out << \";\";\n"
            "    c.inc.=([()->() { value << value.+(1); }]);\n"
            "    out << chk.has_changed(c);\n"
            "    out << \";\";\n"
            "  }];\n"
            "}\n"
            "$Counter {\n"
            "  -(std::Number value);\n"
            "  @inc << [() -> () { value << value.+(1); }];\n"
            "}\n",
            "true;false;true;"),
            "Checker.has_method true; has_changed false before, true after rebind");
    }

    // --------------------------------------------------------
    // Type-tag preservation + member initializers (root fixes)
    // 类型标签保留 + 成员初始化器（根源修复）
    // --------------------------------------------------------

    void test_type_preservation_and_member_init() {
        section("Type-tag preservation (universal receiver) + member initializers");

        // A behavior output parameter is an untyped `std::Object`; a value
        // threaded through it (repeat_ / while_ loop state) used to keep its
        // scalar value but lose its concrete type, so `st.+(1)` and `1.+(st)`
        // both failed signature enforcement. The universal receiver now
        // transparently acts as the scalar it holds.
        // 行为输出参数是无类型 std::Object；经其穿线的数值（repeat_/while_
        // 循环状态）曾保留标量值却丢失具体类型，导致 `st.+(1)` 与 `1.+(st)`
        // 都被签名检查拒绝。现通用接收方透明地代表其持有的标量。
        check(expect_program_output(
            "&io;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(io::OStream out);\n"
            "    -(std::Number n) << 5.repeat_([(st) -> (next) { next << st.+(1); }]);\n"
            "    out << n;\n"
            "  }];\n"
            "}\n",
            "5"),
            "repeat_ loop state keeps its type: st.+(1) works (prints 5)");

        // Direct method dispatch on a universal `std::Object` holder.
        // 对通用 std::Object 持有者直接派发方法。
        check(expect_program_output(
            "&io;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(io::OStream out);\n"
            "    -(std::Object x) << 10;\n"
            "    out << x.+(1);\n"
            "    out << 1.+(x);\n"
            "  }];\n"
            "}\n",
            "11"),
            "universal receiver delegates to held scalar (x.+(1) and 1.+(x) = 11)");

        // Member initializer written in a class body must be honored (was
        // silently dropped, leaving the member at its zero value).
        // 类体内所写的成员初始化器必须生效（此前被静默丢弃，成员停留在零值）。
        check(expect_program_output(
            "&io;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(io::OStream out);\n"
            "    -(Box b);\n"
            "    out << b.inner;\n"
            "  }];\n"
            "}\n"
            "$Box {\n"
            "  -(std::Number(7) inner);\n"
            "}\n",
            "7"),
            "class member initializer `-(std::Number(7) inner)` sets inner to 7");

        // while_ threads its loop state through an untyped output parameter
        // and compares it with `<` (delegated from the universal receiver).
        // while_ 把循环状态经无类型输出参数穿线，并用 `<` 比较（经通用接收方委托）。
        check(expect_program_output(
            "&io;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(io::OStream out);\n"
            "    -(std::Number m) << true.while_(\n"
            "      [(st) -> (next) { next << st.+(1); }],\n"
            "      [(st) ~> (flag) { flag << st.<(4); }]);\n"
            "    out << m;\n"
            "  }];\n"
            "}\n",
            "4"),
            "while_ threaded state keeps type and compares via delegated `<` (m=4)");

        // Nested repeat_ capturing an outer accumulator (closure capture).
        // 嵌套 repeat_ 捕获外层累加器（闭包捕获）。
        check(expect_program_output(
            "&io;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(io::OStream out);\n"
            "    -(std::Number total) << 0;\n"
            "    -(std::Number k) << 3.repeat_([(i) -> (o) {\n"
            "      -(std::Number t2) << i.repeat_([(j) -> (p) { total << total.+(1); p << j; }]);\n"
            "      o << i.+(1);\n"
            "    }]);\n"
            "    out << total;\n"
            "  }];\n"
            "}\n",
            "3"),
            "nested repeat_ with closure capture accumulates correctly (3)");

        // `self` keyword inside a method plus an empty-method declaration
        // `@touch;` must both be valid (self resolves to the instance).
        // 方法内 `self` 关键字与空方法声明 `@touch;` 均须合法（self 解析到实例）。
        check(expect_program_output(
            "&io;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(io::OStream out);\n"
            "    -(Box b);\n"
            "    out << b.peek();\n"
            "    b.touch();\n"
            "  }];\n"
            "}\n"
            "$Box {\n"
            "  -(std::Number(7) inner);\n"
            "  @peek << [() ~> (result) { result << self.inner; }];\n"
            "  @touch << [() -> () {}];\n"
            "}\n",
            "7"),
            "self keyword + empty method `@touch;` work (peek = 7)");
    }

    // --------------------------------------------------------
    // 7. std::Array (Spec 2.3.2)
    // 7. std::Array（文档 2.3.2）
    // --------------------------------------------------------

    void test_array() {
        section("std::Array — dynamic array");

        auto out = rt_builtin::make_ostream();
        auto arr = rt_builtin::make_array();

        call(arr, "push_back", {rt_builtin::make_int(10)});
        call(arr, "push_back", {rt_builtin::make_string("twenty")});
        check(num(call(arr, "size")) == 2, "size == 2 after push_back");
        check(num(call(arr, "get", {rt_builtin::make_int(0)})) == 10,
            "get(0) == 10");
        check(text(call(arr, "get", {rt_builtin::make_int(1)})) == "twenty",
            "get(1) is a mixed element (heterogeneous array)");

        call(arr, "insert", {rt_builtin::make_int(1),
                             rt_builtin::make_boolean(true)});
        check(flag(call(arr, "get", {rt_builtin::make_int(1)})),
            "elements shift back after insert(1, true)");
        check(text(call(arr, "get", {rt_builtin::make_int(2)})) == "twenty",
            "original element moves to slot 2 after insert");

        call(arr, "remove", {rt_builtin::make_int(1)});
        check(num(call(arr, "get", {rt_builtin::make_int(0)})) == 10
            && num(call(arr, "size")) == 2, "remove(1) deletes the middle element");

        check(flag(call(first_of(call(arr, "front")), "==",
            {rt_builtin::make_int(10)})), "front is the first element");
        check(text(call(arr, "back")) == "twenty", "back is the last element");

        call(arr, "pop_back");
        check(num(call(arr, "size")) == 1, "size == 1 after pop_back");

        // Publish / receive: an array is copied wholesale via a flow statement.
        // 公布 / 接收：数组经流语句整体拷贝。
        auto copy = rt_builtin::make_array();
        flow(arr, copy);
        call(copy, "push_back", {rt_builtin::make_int(99)});
        check(num(call(arr, "size")) == 1
            && num(call(copy, "size")) == 2,
            "publish/receive yields an independent copy, no interference");

        // Display: [10]
        // 显示：[10]
        flow(arr, out);
        flow(rt_builtin::make_string("\n"), out);

        // empty-array front / out-of-bounds get: now immediate errors.
        // 空数组 front / 越界 get：现为即时错误。
        check(expect_runtime_error(
            "&io;\n$Program {@:: << [() -> () {\n"
            "  -(io::OStream out);\n"
            "  -(std::Array! a);\n"
            "  out << (a.front());\n"
            "}];};",
            "empty"),
            "empty-array front raises an immediate error");
        check(expect_runtime_error(
            "&io;\n$Program {@:: << [() -> () {\n"
            "  -(io::OStream out);\n"
            "  -(std::Array! a);\n"
            "  out << (a.get(3));\n"
            "}];};",
            "out of bounds"),
            "out-of-bounds get raises an immediate error");

        call(arr, "clear");
        check(num(call(arr, "size")) == 0, "clear removes all elements");
    }

    // --------------------------------------------------------
    // 8. std::Dict (Spec 2.3.2)
    // 8. std::Dict（文档 2.3.2）
    // --------------------------------------------------------

    void test_dict() {
        section("std::Dict — key-value container");

        auto out = rt_builtin::make_ostream();
        auto dict = rt_builtin::make_dict();
        auto name_key = rt_builtin::make_string("name");

        call(dict, "set", {name_key, rt_builtin::make_string("Synth")});
        call(dict, "set", {rt_builtin::make_int(1),
                            rt_builtin::make_string("one")});
        check(num(call(dict, "size")) == 2, "size == 2 after setting two pairs");
        check(text(call(dict, "get", {name_key})) == "Synth",
            "get(string key)");
        check(text(call(dict, "get", {rt_builtin::make_int(1)})) == "one",
            "get(number key)");

        check(flag(call(dict, "has", {name_key})), "has: key exists");
        call(dict, "set", {name_key, rt_builtin::make_string("OOP")});
        check(text(call(dict, "get", {name_key})) == "OOP",
            "set on an existing key overwrites, size unchanged");
        check(num(call(dict, "size")) == 2, "overwrite does not add a pair");

        call(dict, "remove", {rt_builtin::make_int(1)});
        check(!flag(call(dict, "has", {rt_builtin::make_int(1)}))
            && num(call(dict, "size")) == 1, "remove deletes a key-value pair");

        check(expect_runtime_error(
            "&io;\n$Program {@:: << [() -> () {\n"
            "  -(io::OStream out);\n"
            "  -(std::Dict! d);\n"
            "  out << (d.get(\"ghost\"));\n"
            "}];};",
            "key does not exist"),
            "get on a non-existent key raises an immediate error");

        // keys / values: consistent with key order (by encoded order, stable).
        // keys / values：与键序一致（按编码序，稳定）。
        auto keys = call(dict, "keys");
        auto values = call(dict, "values");
        check(num(call(first_of(keys), "size")) == 1
            && text(call(first_of(keys), "get",
                {rt_builtin::make_int(0)})) == "name",
            "keys returns the key array");
        check(text(call(first_of(values), "get",
                {rt_builtin::make_int(0)})) == "OOP",
            "values returns the value array");

        // Publish / receive: a dict is copied wholesale.
        // 公布 / 接收：字典整体拷贝。
        auto copy = rt_builtin::make_dict();
        flow(dict, copy);
        call(copy, "set", {rt_builtin::make_string("extra"),
                            rt_builtin::make_int(1)});
        check(num(call(dict, "size")) == 1
            && num(call(copy, "size")) == 2,
            "dict copy is independent");

        // Display: {name -> OOP}
        // 显示：{name -> OOP}
        flow(dict, out);
        flow(rt_builtin::make_string("\n"), out);
    }

    // --------------------------------------------------------
    // 9. std::Tuple (Spec 5.4 / D.18)
    // 9. std::Tuple（文档 5.4 / D.18）
    // --------------------------------------------------------

    void test_tuple() {
        section("std::Tuple — immutable value sequence (D.18)");

        // D.18.1: literal initialization (parentheses pack multiple values).
        // D.18.1：字面量初始化（圆括号打包多个值）。
        auto point = rt_builtin::make_tuple(
            {rt_builtin::make_int(10), rt_builtin::make_int(20)}
        );
        check(num(call(point, "size")) == 2, "tuple literal size == 2");

        // D.18.2: indexed access; element types differ.
        // D.18.2：按索引访问，元素类型各不相同。
        auto mixed = rt_builtin::make_tuple(
            {
                rt_builtin::make_int(10),
                rt_builtin::make_string("Alice"),
                rt_builtin::make_boolean(true),
            }
        );
        check(num(call(mixed, "get", {rt_builtin::make_int(0)})) == 10,
            "get(0) is Number 10");
        check(text(call(mixed, "get", {rt_builtin::make_int(1)})) == "Alice",
            "get(1) is String \"Alice\"");
        check(flag(call(mixed, "get", {rt_builtin::make_int(2)})),
            "get(2) is Boolean true");

        // Tuple pass-through: multiple return values flow directly into
        // another tuple (runtime side of D.10).
        // 元组透传：多返回值直接流入另一个元组（D.10 的运行时侧）。
        auto passthrough = rt_builtin::make_tuple();
        flow(mixed, passthrough);
        check(num(call(passthrough, "size")) == 3
            && text(call(passthrough, "get",
                {rt_builtin::make_int(1)})) == "Alice",
            "tuple passes through a flow statement wholesale");

        // make: build a new tuple from several elements (Spec 2.3.2).
        // make：从若干元素构造新元组（文档 2.3.2）。
        auto built = call(point, "make",
            {rt_builtin::make_int(5), rt_builtin::make_int(15)});
        check(num(call(first_of(built), "size")) == 2
            && num(call(first_of(built), "get",
                {rt_builtin::make_int(0)})) == 5,
            "make(elements) builds a new tuple");

        // Nested tuple (Spec 5.4.2).
        // 嵌套元组（文档 5.4.2）。
        auto nested = rt_builtin::make_tuple(
            {point, first_of(built)}
        );
        check(num(call(nested, "size")) == 2, "tuples can nest");

        // Out of bounds: now an immediate error. 越界：现为即时错误。
        check(expect_runtime_error(
            "&io;\n$Program {@:: << [() -> () {\n"
            "  -(io::OStream out);\n"
            "  -(std::Tuple((10, \"Alice\", true))! t);\n"
            "  out << (t.get(9));\n"
            "}];};",
            "out of bounds"),
            "out-of-bounds tuple get raises an immediate error");
    }

    // --------------------------------------------------------
    // 10. Contract (Spec Chapter 1)
    // 10. 约束（Contract / 契约，文档第一章）
    // --------------------------------------------------------

    void test_contract() {
        section("Contract — class name is a contract, a duck-typed method list");

        // Class name as constraint: all of Number's method signatures
        // (Spec Chapter 1).
        // 类名当约束：Number 的全部方法签名（文档第一章）。
        auto number_contract = rt_builtin::contract_of(
            rt_builtin::PT_stdNumber
        );
        auto number_instance =
            std::dynamic_pointer_cast<runtime::RuntimeClass>(
                rt_builtin::make_int(1)
            );
        check(number_contract.validate(number_instance),
            "a Number instance satisfies the Number contract");

        auto string_instance =
            std::dynamic_pointer_cast<runtime::RuntimeClass>(
                rt_builtin::make_string("x")
            );
        check(!number_contract.validate(string_instance),
            "a String instance does not satisfy the Number contract "
            "(different signatures)");

        // A custom #Addable-style constraint: requires only a + method
        // (duck typing).
        // 自定义 #Addable 式约束：只要求一个 + 方法（鸭子类型）。
        rt_basic::ClassContract addable_string;
        addable_string.add_sign(make_sign("+",
            {{"other", "std::String"}}, {{"result", "std::String"}}));
        check(addable_string.validate(string_instance),
            "String satisfies a duck contract requiring only +");
        check(!addable_string.validate(number_instance),
            "Number's + signature differs, fails this contract");

        rt_basic::ClassContract addable_number;
        addable_number.add_sign(make_sign("+",
            {{"other", "std::Number"}}, {{"result", "std::Number"}}));
        check(addable_number.validate(number_instance),
            "Number satisfies its own duck contract");
    }

    // --------------------------------------------------------
    // 11. io streams (Spec 2.3.2 / 3.4)
    // 11. io 流（文档 2.3.2 / 3.4）
    // --------------------------------------------------------

    void test_ostream() {
        section("io::OStream — output stream");

        // OStream / IStream must be instantiated before use (Spec 2.2).
        // OStream / IStream 必须先实例化才能使用（文档 2.2）。
        auto out = rt_builtin::make_ostream();
        auto out_class =
            std::dynamic_pointer_cast<runtime::RuntimeClass>(out);
        check(out != nullptr, "OStream can be instantiated");
        check(out_class->get_methods().count(":=") == 1,
            "OStream has the receive function := ");

        // Writing does not mutate out's own state, so it may be declared
        // const (Spec 3.4.1).
        // 写入不修改 out 自身状态，可声明为常数（文档 3.4.1）。
        flow(rt_builtin::make_string("out"), out);
        flow(rt_builtin::make_string(" "), out);
        flow(rt_builtin::make_int(1), out);
        flow(rt_builtin::make_string(" "), out);
        flow(rt_builtin::make_boolean(true), out);
        flow(rt_builtin::make_string("\n"), out);
        check(out_class->get_attributes().empty(),
            "writing does not change out's own state (: = const contract)");
    }

    void test_istream(bool live) {
        section("io::IStream — input stream");

        auto in = rt_builtin::make_istream();
        auto in_class =
            std::dynamic_pointer_cast<runtime::RuntimeClass>(in);
        check(in != nullptr, "IStream can be instantiated");
        check(in_class->get_methods().count("=:") == 1,
            "IStream has the publish function =: ");

        // EOF on an exhausted IStream is now an immediate error (the retired
        // poison-water model no longer degrades silently). Verified through
        // the interpreter binary — no stdin needed (the very first read hits
        // EOF). Runs in the static suite (no --io required).
        // 耗尽输入的 IStream 在 EOF 时现为即时错误（退役的毒水模型不再
        // 静默降级）。经解释器二进制验收——无需 stdin（首次读取即遇 EOF）。
        // 在静态套件中运行（无需 --io）。
        check(expect_runtime_error(
            "&io;\n$Program {@:: << [() -> () {\n"
            "  -(io::IStream in);\n"
            "  -(std::String w) << in;\n"
            "}];};",
            "input stream ended"),
            "exhausted IStream raises an immediate error");

        if (!live) {
            std::cout << "  [SKIP] live input acceptance\n"
                         "        (run with --io and redirect stdin)\n";
            return;
        }

        // Publish function: read one word from standard input (Spec 2.3.2).
        // 公布函数：从标准输入读取一个词（文档 2.3.2）。
        auto published = call(in, "=:");
        check(text(published) == "Yeah.",
            "=: reads one word from standard input");
    }

    // --------------------------------------------------------
    // 12. Behaviors are also objects (Spec 5.1)
    // 12. 行为也是对象（文档 5.1）
    // --------------------------------------------------------

    void test_behavior_object() {
        section("Behaviors are also objects");

        auto out = rt_builtin::make_ostream();
        auto forty_two = behavior(
            [](rt_basic::InstanceMap& /*env*/,
               rt_basic::InstanceListPtr /*paras*/) {
                return list_of({rt_builtin::make_int(42)});
            }
        );
        check(forty_two->is_behav(),
            "a behavior object's is_behav() is true");

        // A behavior can be passed around like data (stored in an array,
        // fetched, executed later).
        // 行为可以像数据一样被传递（存进数组、取出、延迟执行）。
        auto shelf = rt_builtin::make_array({forty_two});
        auto fetched = call(shelf, "get", {rt_builtin::make_int(0)});
        rt_basic::InstanceMap scratch_env;
        auto delayed = rt_builtin::call_behavior(
            rt_builtin::first_of(fetched),
            scratch_env,
            rt_builtin::empty_result()
        );
        check(num(delayed) == 42, "behavior stored in array, fetched, deferred == 42");

        flow(rt_builtin::make_string("42\n"), out);
    }

    // --------------------------------------------------------
    // 13. Generic make and signature constraints (Runtime.make /
    //     CallableSign)
    //     The runtime performs no argument checking — the signature carries
    //     the constraint, and the compiler / interpreter checks before
    //     calling; the runtime only dispatches by name. Here we verify:
    //     (a) Runtime.make treats system objects and user-defined types
    //         alike;
    //     (b) method signatures correctly record type constraints (the
    //         interpreter's checking basis);
    //     (c) @! / @# are readable as Callable attributes;
    //     (d) value-level runtime errors (divide by zero, out of bounds,
    //         empty container, non-behavior argument) are degraded to
    //         poison inside the closure — not type checking.
    // 13. 通用 make 与签名约束（Runtime.make / CallableSign）
    //     runtime 不做参数核查——签名承载约束，编译器 / 解释器
    //     在调用前核查；runtime 只负责按名派发。此处验证：
    //     (a) Runtime.make 对系统对象与用户自定义类型一视同仁；
    //     (b) 方法签名正确记录类型约束（解释器核查依据）；
    //     (c) @! / @# 作为 Callable 属性可读；
    //     (d) 值层面运行时错误（除 0、越界、空容器、非行为
    //         参数）由闭包内降级为毒水——非类型核查。
    // --------------------------------------------------------

    void test_make_and_signs() {
        section("Generic make and signature constraints "
            "(Runtime.make / CallableSign)");

        // Runtime.make: create an instance by registered type name — system
        // objects and user-defined types share one interface (Spec Chapter 1:
        // a type IS a prototype).
        // Runtime.make：按注册类型名创建实例——系统对象与
        // 用户自定义类型走同一接口（文档第一章：类型即原型）。
        auto via_make = stdRT.make("Number");
        check(via_make != nullptr,
            "stdRT.make(\"Number\") creates a system object");
        auto nm = std::dynamic_pointer_cast<runtime::RuntimeClass>(
            via_make);
        nm->set_attribute(rt_builtin::VALUE_KEY,
            rt_builtin::cap_number(42, true));
        check(rt_builtin::number_of(via_make).value_or(-1) == 42,
            "a make-created instance can be filled like a factory");

        // User-defined type: after registering a prototype, make treats it
        // alike.
        // 用户自定义类型：注册原型后 make 一视同仁地创建。
        auto my_proto = std::make_shared<rt_basic::ClsProto>(
            rt_builtin::PT_stdObject);
        my_proto->set_method("greet", rt_builtin::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr) {
                return rt_builtin::list_of(
                    {rt_builtin::make_string("hi")});
            },
            rt_builtin::make_sign("greet", {},
                {{"result", "std::String"}})
        ));
        runtime::Prototypes userPT;
        userPT.regcls("Greeter", my_proto);
        stdRT.add_protos(userPT);
        auto greeter = stdRT.make("Greeter");
        check(greeter != nullptr,
            "make creates the user-defined type Greeter");
        check(text(call(greeter, "greet")) == "hi",
            "user-type method callable (same path as system objects)");

        // Signature is the constraint: CallableSign's inpara/outpara record
        // type constraints, which the interpreter checks (the runtime does
        // not guard at runtime).
        // 签名即约束：CallableSign 的 inpara/outpara 记录类型
        // 约束，解释器将据此核查（runtime 不在运行时守卫）。
        auto add_sign = rt_builtin::PT_stdString
            ->get_methods().at("+").get_sign();
        check(add_sign.name == "+"
            && add_sign.inpara.size() == 1
            && add_sign.inpara[0].second == "std::String"
            && add_sign.outpara[0].second == "std::String",
            "String.+ signature records constraint "
            "(std::String) -> (std::String)");

        // @! / @# are readable as Callable attributes (the interpreter's
        // checking basis).
        // @! / @# 作为 Callable 属性可读（解释器核查依据）。
        auto add_attr = rt_builtin::PT_stdString
            ->get_methods().at("+").get_attr();
        check(add_attr.first == true && add_attr.second == false,
            "native method @! flag readable via get_attr");

        // Value-level semantics (inside the closure, not type checking):
        // divide by zero yields Infinity (IEEE 754). Out-of-domain / wrong-type
        // situations are now immediate errors (the retired poison-water model
        // no longer degrades silently).
        // 值层面语义（闭包内，非类型核查）：除 0 得 Infinity（IEEE 754）。
        // 越界 / 类型不符等情形现为即时错误（退役的毒水模型不再静默降级）。
        check(std::isinf(num_of(first_of(call(
            rt_builtin::make_int(10), "/",
            {rt_builtin::make_int(0)})))),
            "divide by zero: value-level result is +Infinity (IEEE 754)");
        check(expect_runtime_error(
            "&io;\n$Program {@:: << [() -> () {\n"
            "  -(io::OStream out);\n"
            "  -(std::Array! a);\n"
            "  out << (a.front());\n"
            "}];};",
            "empty"),
            "empty-array front raises an immediate error (retired degradation)");
        check(expect_runtime_error(
            "&io;\n$Program {@:: << [() -> () {\n"
            "  -(io::OStream out);\n"
            "  -(std::Boolean(true)! b);\n"
            "  out << (b.if_(1, 0));\n"
            "}];};",
            "behavior"),
            "if_ receiving a non-behavior raises an immediate error");

        // Variadic signature ("..." convention): Tuple.make accepts any number
        // of elements.
        // 变参签名（"..." 约定）：Tuple.make 接受任意个元素。
        auto make_sign_tuple = rt_builtin::PT_stdTuple
            ->get_methods().at("make").get_sign();
        check(make_sign_tuple.inpara.size() == 1
            && make_sign_tuple.inpara[0].second == "value...",
            "Tuple.make signature is variadic value...");
        check(num(call(first_of(call(rt_builtin::make_tuple(),
            "make")), "size")) == 0,
            "make with zero args builds an empty tuple (variadic lower bound 0)");

        // := smart negotiation: signature exempts type interception, graceful
        // degradation per D.8.
        // := 智能协商：签名豁免类型拦截，按 D.8 优雅降级。
        auto x = rt_builtin::make_int(0);
        flow(rt_builtin::make_string("not a number"), x);
        check(num_of(x) == 0 && !rt_builtin::is_error(x),
            ":= bad negotiation graceful degradation "
            "(exempt from type checking, keeps zero, not poisoned)");
    }

} // namespace


    // --------------------------------------------------------
    // 13b. = method (explicit assignment, non-const mode, same-type overwrite)
    // 13b. = 方法（显式赋值，非常数模式，同类型直接覆盖）
    // --------------------------------------------------------
    void test_assign_method() {
        section("= method: same-type direct overwrite (non-const, no clone)");

        // Number: after a.=(b), a's value is directly overwritten by b (the
        // principle of ordinary assignment).
        // Number：a.=(b) 后 a 的值被 b 直接覆盖（一般赋值原理）。
        auto a = rt_builtin::make_int(10);
        auto b = rt_builtin::make_int(99);
        call(a, "=", {b});
        check(num_of(a) == 99, "Number.=(b): a directly overwritten by b (10 -> 99)");
        check(num_of(b) == 99, "Number.=(b): b itself unchanged (overwrite is not a reference)");

        // Cross-type: a Number receiving a String should gracefully ignore it
        // (keep original value).
        // 跨类型：Number 收到 String 应优雅忽略（保持原值）。
        auto c = rt_builtin::make_int(7);
        call(c, "=", {rt_builtin::make_string("oops")});
        check(num_of(c) == 7, "Number.=(String): cross-type ignored, keeps original 7");

        // Object: duck-type accepts any published value (the root's = method).
        // Object：鸭子式收下任意公布值（万物之源的 = 方法）。
        auto o = rt_builtin::make_object();
        call(o, "=", {rt_builtin::make_string("X")});
        check(text_of(o) == "X", "Object.=(x): duck-typed overwrite, value becomes x's content");

        // Array: =(another array) overwrites elements wholesale by position
        // (sequence receive semantics).
        // Array：=(另一个数组) 按位置整体覆盖元素（序列接收语义）。
        auto arr1 = rt_builtin::make_array();
        call(arr1, "push_back", {rt_builtin::make_int(1)});
        call(arr1, "push_back", {rt_builtin::make_int(2)});
        auto arr2 = rt_builtin::make_array();
        call(arr2, "push_back", {rt_builtin::make_int(3)});
        call(arr2, "push_back", {rt_builtin::make_int(4)});
        call(arr2, "push_back", {rt_builtin::make_int(5)});
        call(arr1, "=", {arr2});
        check(num(call(arr1, "size")) == 3,
            "Array.=(arr2): element count overwritten wholesale to 3");
        check(num(call(arr1, "get", {rt_builtin::make_int(0)})) == 3,
            "Array.=(arr2): element positions overwritten correctly (first 3)");
    }

    // --------------------------------------------------------
    // 16. Method-declaration forms & closure-local closures
    // 16. 方法声明的多种形式与闭包内局部闭包
    //     (regression for the four issues reported by the user:
    //      empty method ';', '.=' assignment form, closure-local
    //      closure variable, and the ASCII-only rvalue-flow error)
    // --------------------------------------------------------
    void test_method_forms() {
        section("16. Method forms (<< / .= / empty) & closure-local closures");

        // (a) Empty method declaration: '@name;' produces a no-op method and
        // must NOT raise an error.
        // 空方法声明：'@name;' 产生空方法，【不应】报错。
        check(expect_clean_run(
            "&io;&system;\n"
            "$Program {\n"
            "  @::;\n"
            "};\n"),
            "empty method '@::;' runs without error (declares a no-op method)");

        // (b) Assignment-form method binding via '.=' (alias of '<<').
        // '.=' 赋值式方法绑定（'<<' 的别名）。
        check(expect_program_output(
            "&io;&system;\n"
            "$Program {\n"
            "  @:: .= [()->() {\n"
            "    -(io::OStream outer);\n"
            "    outer.push_line(\"Hello, world!\");\n"
            "    -(system::System sys);\n"
            "    outer.push_line(1./(inf));\n"
            "  }];\n"
            "};\n",
            "Hello, world!"),
            "assignment-form method '@:: .= [...]' binds and runs");

        // (c) Closure-local closure: '@name << [...]' inside a behavior body
        // defines a local closure variable, callable as 'name()'.
        // 闭包内局部闭包：行为体内的 '@name << [...]' 定义局部闭包变量，
        // 可用 'name()' 调用。
        check(expect_program_output(
            "&io;\n"
            "$Program {\n"
            "  @:: << [()->() {\n"
            "    -(io::OStream out);\n"
            "    @clos << [()->() { out.push_line(\"called!\"); }];\n"
            "    clos();\n"
            "  }];\n"
            "};\n",
            "called!"),
            "closure-local closure '@name << [...]' defines a callable local");

        // (d) The legitimate rvalue-flow error is now reported as an
        // ASCII / English diagnostic (no box-drawing or non-ASCII glyphs).
        // 合法的右值流错误现在以 ASCII 英文诊断报告（无制表符或非 ASCII 字形）。
        check(expect_runtime_error(
            "&io;\n"
            "$Program {\n"
            "  @:: << [()->() { 1 << 0; }];\n"
            "};\n",
            "flow target must be a variable name"),
            "rvalue flow target ('1 << 0') is rejected with an ASCII English error");

        // (e) String escape '\\\\' is decoded to a single backslash at runtime.
        // 字符串转义 '\\' 在运行时被解码为单个反斜杠。
        check(expect_program_output(
            "&io;\n"
            "$Program {\n"
            "  @:: << [()->() {\n"
            "    -(io::OStream out);\n"
            "    out.push_line(\"p\\\\q\");\n"
            "  }];\n"
            "};\n",
            "p\\q"),
            "string escape '\\\\' produces a single backslash at runtime");
    }

    // --------------------------------------------------------
    // 17. sugar::Infix (syntactic-sugar standard library)
    // 17. sugar::Infix（语法糖标准库）
    // --------------------------------------------------------
    void test_sugar_infix() {
        section("sugar::Infix — arithmetic evaluation with environment");

        // 1+(2-3)*(3+5) = 1 + (-1)*8 = -7
        check(expect_program_output(
            "&sugar;\n&io;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(io::OStream out);\n"
            "    -(sugar::Infix(\"1+(2-3)*(3+5)\") e);\n"
            "    out << (e.parse());\n"
            "  }];\n"
            "};\n",
            "-7"),
            "parse() evaluates 1+(2-3)*(3+5) to -7");

        // Variable resolution: a + 3 with {a: 2} -> 5
        check(expect_program_output(
            "&sugar;\n&io;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(io::OStream out);\n"
            "    -(std::Dict d);\n"
            "    d.set(\"a\", 2);\n"
            "    -(sugar::Infix(\"a+3\") e);\n"
            "    e.env(d);\n"
            "    out << (e.parse());\n"
            "  }];\n"
            "};\n",
            "5"),
            "parse() resolves 'a' from the environment dict (a+3 = 5)");

        // Missing environment: an unresolved variable raises a parse-attributed
        // runtime error (not a silent poison value, not a crash).
        // 环境缺失：未定义变量抛出归属 parse 的运行时错误（非静默毒水、
        // 非崩溃）。
        check(expect_runtime_error(
            "&sugar;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(sugar::Infix e);\n"
            "    e.set(\"a+1\");\n"
            "    e.parse();\n"
            "  }];\n"
            "};\n",
            "parse"),
            "parse() with an unresolved variable (no env) raises a runtime error");

        // Present-but-missing key: 'a' is not in the environment dict.
        // 环境存在但缺键：'a' 不在环境字典中。
        check(expect_runtime_error(
            "&sugar;\n"
            "$Program {\n"
            "  @:: << [() -> () {\n"
            "    -(std::Dict d);\n"
            "    d.set(\"b\", 2);\n"
            "    -(sugar::Infix(\"a+1\") e);\n"
            "    e.env(d);\n"
            "    e.parse();\n"
            "  }];\n"
            "};\n",
            "parse"),
            "parse() with a missing key raises a runtime error");
    }

int main(int argc, char** argv) {
    bool live_input = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--io") {
            live_input = true;
        }
    }

    std::cout << "============================================================\n";
    std::cout << " Synth OOP Runtime - Built-in Objects Acceptance Tests\n";
    std::cout << " Per Spec v1.25 Section 2.3 and Appendix D\n";
    std::cout << "============================================================\n";

    rt_builtin::init_builtins();

    if (!synth_available()) {
        std::cerr << "FATAL: interpreter not found next to this test binary://n"
                  << "       " << synth_exe_path() << "\n"
                  << "       Build it first (e.g. `cmake --build build`).\n";
        return 2;
    }

    test_bootstrap();
    test_zero_and_flow();
    test_number();
    test_string();
    test_control_flow();
    test_ieee754_and_no_poison();
    test_method_rebind_and_checker();
    test_type_preservation_and_member_init();
    test_array();
    test_dict();
    test_tuple();
    test_contract();
    test_ostream();
    test_istream(live_input);
    test_behavior_object();
    test_make_and_signs();
    test_assign_method();
    test_method_forms();
    test_sugar_infix();

    std::cout << "\n------------------------------------------------------------\n";
    std::cout << std::format(" {} passed, {} failed\n", passed, failed);
    std::cout << "------------------------------------------------------------\n";

    return failed == 0 ? 0 : 1;
}
