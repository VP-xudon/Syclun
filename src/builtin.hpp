// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

#ifndef BUILTIN_HPP
#define BUILTIN_HPP

// ============================================================
// Builtin.hpp
//
// Synth OOP native objects (built-in objects) implementation.
// Synth OOP 原生对象（native objects）的内建实现。
//
// Implemented method-by-method per Section 2.3 of the
// "Synth OOP Language Specification v1.25":
// 依据《Synth OOP 语言文档 v1.25》第 2.3 节逐方法规格实现：
//
//   std::Object   ::  ~  =:  :=  =             (root of all things)
//   std::Object   ::  ~  =:  :=  =             （万物之源）
//   std::Number   + - * / % < > <= >= == != to_string repeat_
//                 =:  :=  =
//   std::Boolean  if_  while_  =:  :=  =
//   std::String   +  upper  lower  reverse  length  get  contains  slice
//                 =:  :=  =
//   std::Array    push_back  get  size  pop_back  remove  insert  clear
//                 front  back  =:  :=  =
//   std::Dict     get  set  remove  has  size  keys  values  =:  :=  =
//   std::Tuple    get  make  size  =:  :=  =
//   io::OStream   := (output to standard output)
//   io::IStream   =: (read one word from standard input)
//
// =: (publish) and := (receive) are a pair: the send/receive ends
// of a flow statement. = (assign method) shares the same origin as
// := — same-type data overwrites directly, which is the principle
// of ordinary assignment (a.=(b), -> non-const mode, not ~> nor =>;
// Spec 4.3: = is not an operator, but may be a method name).
// Copying via flow statements needs no separate implementation: copy
// semantics are carried by the publish-side =:'s clone.
// 其中 =: （公布）与 := （接收）成对：流语句的发送 / 接收端；
// = （赋值方法）与 := 同源——同类型数据直接覆盖即一般赋值的
// 原理（a.=(b)，-> 非常数模式，非 ~> 与 =>；文档 4.3：= 不是
// 运算符，但可以是方法名）。复制流语句无需单独实现：复制语义
// 由公布侧 =: 的克隆承担。
//
// ---------------------------- Conventions ----------------------------
// ---------------------------- 约定 ----------------------------
//
// This file defines no new classes and does not treat built-in
// objects specially: every built-in object is an instance of
// runtime::RuntimeClass whose prototype is rt_basic::ClsProto and
// whose methods are native closures (NativeClosure) of
// rt_basic::Callable. Built-in values are not carried by C++
// subclasses but by a "value capsule" — an ordinary
// runtime::RuntimeObject whose selfname field encodes the value:
// 本文件不定义任何新类，也不把内置对象区别对待：
// 一切内置对象都是 runtime::RuntimeClass 的实例，其原型是
// rt_basic::ClsProto，其方法都是 rt_basic::Callable 的原生闭包
// （NativeClosure）。内置值不用 C++ 子类承载，而是用"值胶囊"
// （value capsule）——一个普通的 runtime::RuntimeObject，其
// selfname 字段按下列格式编码值内容：
//
//     num:<value>:i | num:<value>:f     number (i int / f float)
//     num:<数值>:i | num:<数值>:f     数字（i 整数 / f 浮点）
//     str:<text>                       string (text may contain anything)
//     str:<原文>                      字符串（原文可含任意字符）
//     bool:t | bool:f                  boolean
//     bool:t | bool:f                 布尔
//     void                            zero value / no value
//      void                            零值 / 无值
//
// 1. Obtaining self (important):
// 1. self 的获取（重要）：
//    RuntimeClass::call_method passes the receiver's own attribute
//    table as env to the method closure, so **env IS self**. Built-in
//    objects store state directly in their own attribute table:
//    RuntimeClass::call_method 把接收者自身的 attributes 作为 env
//    传给方法闭包，因此 **env 即 self**。内置对象把状态直接存进
//    自身的属性表：
//        "#value"      scalar value capsule (Number / String / Boolean)
//        "#value"      标量值胶囊（Number / String / Boolean）
//        "#size"       container element count capsule (Array / Dict / Tuple)
//        "#size"       容器元素个数胶囊（Array / Dict / Tuple）
//        "#0" "#1" …   Array / Tuple elements
//        "#0" "#1" …   Array / Tuple 的元素
//        "#k:<key>"    Dict key object (<key> is the content encoding)
//        "#k:<键>"     Dict 的键对象（<键> 为内容编码）
//        "#v:<key>"    Dict value object
//        "#v:<键>"     Dict 的值对象
//    The "#" prefix is reserved for the runtime; user attributes
//    never collide with it.
//    "#" 前缀保留给运行时，用户属性不会与之冲突。
//
// 2. Behavior modes (contract, Spec Section 5.2):
// 2. 行为模式（契约，文档第 5.2 节）：
//        ->  non-const contract  =  NORMAL (0)
//        ->  非常数契约  =  NORMAL (0)
//        ~>  const contract      =  CONST  (1)
//        ~>  常数契约    =  CONST  (1)
//        =>  zero side-effect    =  STRICT (2)
//        =>  零副作用    =  STRICT (2)
//    In runtime.hpp, BehavStateOBJ is a private nested type of
//    Callable; externally a Callable can only be constructed with
//    `{}` (value-initialized, i.e. NORMAL). Therefore all closures
//    in this file run in the NORMAL tier — env is a reference to the
//    receiver's real attribute table; methods marked ~> / => rely on
//    "read-only discipline" to guarantee purity (the closure itself
//    does not write env), producing exactly the same effect as CONST
//    (CONST also just takes a copy). The three-tier mechanism is kept
//    intact for future interpreter-constructed user behaviors.
//    runtime.hpp 中 BehavStateOBJ 是 Callable 的私有嵌套类型，
//    外部只能以 `{}`（值初始化，即 NORMAL）构造 Callable。因此
//    本文件的闭包一律以 NORMAL 档运行——env 是接收者真实属性表
//    的引用；标为 ~> / => 的方法靠"只读纪律"保证纯度（闭包本身
//    不写 env），效果与 CONST 完全一致（CONST 也只是拿到副本）。
//    三档机制原样保留，供未来解释器构造用户行为时使用。
//
// 3. Poison Water Model (Spec Section 10.1):
// 3. 毒水模型（Poison Water Model，文档第 10.1 节）：
//    (Historical note: an earlier "poison water" design let type
//    mismatches propagate as poisoned capsules with an `_case` antidote.
//    That model is retired — type / constraint violations are now reported
//    immediately by the diagnostic reporter as hard runtime errors.)
//    （历史注记：早期"毒水"设计曾让类型不符以带毒胶囊传播并用 `_case`
//    解毒；该模型已退役——类型 / 约束违犯现由诊断上报器作为硬性
//    运行时错误即时报告。）
//
// 4. Zero-value law (Spec Section 4.4):
// 4. 零值法则（文档第 4.4 节）：
//    number 0, empty string, false, empty array / dict / tuple. The
//    loop state of while_ / repeat_ starts from the zero value
//    (Number 0).
//    数字 0、空串、false、空数组 / 字典 / 元组。while_ /
//    repeat_ 的循环状态 state 从零值（Number 0）开始。
//
// 5. Smart negotiation (Spec Section 3.3):
// 5. 智能协商（文档第 3.3 节）：
//    A flow statement = sender's "=:" publish + receiver's ":="
//    receive. Each built-in object's := checks whether the published
//    value is compatible with itself: negotiation failure never
//    forces a conversion, it keeps the zero value and degrades
//    gracefully (e.g. flowing a String into a Number). Same-type
//    data overwrites directly — this is also the principle of
//    ordinary assignment (all system-provided receive functions do
//    this). The assign method "=" (a.=(b)) shares the same overwrite
//    closure as :=, but with a different contract: = is a -> non-const
//    mode (assignment mutates the receiver, naturally impure) and does
//    not go through publish negotiation — copy semantics are carried
//    by the publish-side =:, so copy flow statements need no separate
//    implementation.
//    流语句 = 发送方 "=:" 公布 + 接收方 ":=" 接收。各内置对象
//    的 := 检查公布值与自己是否兼容：协商失败绝不暴力转换，
//    保持零值、优雅降级（如把 String 流入 Number）。同类型
//    数据则直接覆盖——这也是一般赋值的原理（系统提供的接收
//    函数都如此）。赋值方法 "="（a.=(b)）与 := 共用同一套
//    覆盖闭包，但契约不同：= 为 -> 非常数模式（赋值修改接收
//    者，天然非纯），且不经过公布协商——复制流语句因此不用
//    单独实现（复制语义由公布侧 =: 的克隆承担）。
//
// 6. Packages (Prototypes):
// 6. 包（Prototypes）：
//    std is the preset collection package (Spec Section 9.2). stdPT holds
//    Object / Number / Boolean / String / Array / Dict / Tuple; it is
//    injected into stdRT (the Runtime environment). io (OStream / IStream)
//    is now a C++-backed standard library (lib/cpp/io.hpp) registered by
//    init_stdlibs(), which init_builtins() also invokes. init_builtins()
//    is the only public bootstrap entry, called directly by main.cpp and
//    the acceptance files.
//    std 是预置集合包（文档第 9.2 节）。stdPT 收 Object / Number /
//    Boolean / String / Array / Dict / Tuple，注入 stdRT（Runtime 运行
//    环境）。io（OStream / IStream）现为 C++ 底层标准库
//    （lib/cpp/io.hpp），由 init_stdlibs() 登记（init_builtins() 亦会
//    调用之）。init_builtins() 是唯一的公开引导入口，供 main.cpp 与
//    验收文件直接调用。
//
// 7. Method modifiers and signature constraints (Spec 6.2 / 2.2,
//    Callable interface):
// 7. 方法修饰符与签名约束（文档 6.2 / 2.2，Callable 接口）：
//    The 4th parameter of Callable construction,
//    std::pair<bool, bool>, is { isConst(@!), isPrivate(@#) }.
//    Native object methods are all registered as {true, false}
//    (Spec: native methods are all const behaviors).
//    Callable 构造的第 4 参 std::pair<bool, bool> 即
//    { isConst(@!), isPrivate(@#) }。原生对象的方法一律以
//    {true, false} 注册（文档：原生方法都是常数行为）。
//    Modifiers and signatures (the inpara/outpara type strings of
//    CallableSign) together form the constraint — the compiler /
//    interpreter checks argument count and types before calling, and
//    checks @! / @# at rebind / dispatch; the runtime layer performs
//    no runtime guard and only dispatches by name. builtin defines
//    built-in objects as a "user" through the same interface, with no
//    privileged channel.
//    修饰符与签名（CallableSign 的 inpara/outpara 类型串）
//    共同构成约束——编译器 / 解释器在调用前据此核查参数
//    数量与类型、在重绑 / 派发时核查 @! / @#；runtime 层
//    不做运行时守卫，只负责按名派发。builtin 作为"用户"
//    用同一套接口定义内置对象，无任何特权通道。
//
// 8. Constraint notation (CallableSign type strings):
// 8. 约束写法（CallableSign 类型串）：
//    "value" / "void" / ""   any object (unconstrained placeholder)
//    "value" / "void" / ""   任意对象（无约束占位）
//    "@"                     behavior (is_behav)
//    "@"                     行为（behavior，is_behav）
//    "std::Object"           the root of all things — every object satisfies
//    "std::Object"           万物之源——一切对象皆满足
//    "std::X" / "io::X"      exact prototype identity match
//    "std::X" / "io::X"      原型身份精确匹配
//    "T..."                  variadic base type (last inpara)
//    "T..."                  变参基类型（最后一个 inpara）
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "runtime.hpp"


// ============================================================
// Standard Runtime
// ============================================================

inline runtime::Prototypes std_Prototypes;
inline runtime::Runtime    std_Runtime;

inline runtime::Prototypes& stdPT = std_Prototypes;
inline runtime::Runtime&    stdRT = std_Runtime;


namespace rt_builtin {


    // ========================================================
    // Prototypes
    //
    // Global handles for each built-in prototype (filled by
    // init_builtins()). Declared up front so method factories and
    // clone helpers can reference them.
    // 各内置原型的全局句柄（init_builtins() 负责填充）。
    // 声明在前，方法工厂与克隆辅助引用它们。
    // ========================================================

    inline rt_basic::ClsProtoPtr PT_stdObject;
    inline rt_basic::ClsProtoPtr PT_stdNumber;
    inline rt_basic::ClsProtoPtr PT_stdBoolean;
    inline rt_basic::ClsProtoPtr PT_stdString;
    inline rt_basic::ClsProtoPtr PT_stdArray;
    inline rt_basic::ClsProtoPtr PT_stdDict;
    inline rt_basic::ClsProtoPtr PT_stdTuple;


    // ========================================================
    // Conventions
    // ========================================================

    // Key conventions in the receiver's own attribute table (env IS self).
    // 接收者自身属性表（env 即 self）中各键的约定。
    inline const std::string VALUE_KEY   = "#value";  // scalar value capsule
    inline const std::string SIZE_KEY    = "#size";   // container element count capsule
    inline const std::string ELEM_PREFIX = "#";       // sequence element prefix: #0 #1 …
    inline const std::string DICT_KEYPRE = "#k:";     // Dict key object prefix
    inline const std::string DICT_VALPRE = "#v:";     // Dict value object prefix


    // ========================================================
    // Value Capsules
    //
    // Value capsule: an ordinary runtime::RuntimeObject whose selfname
    // encodes the value content. Encoding is described in header note 0.
    // 值胶囊：普通 runtime::RuntimeObject，selfname 编码值内容。
    // 编码见文件头注释第 0 条。
    // ========================================================

    // Shortest round-trip decimal representation of a number (used both
    // for encoding and for to_string / display).
    // 数值的最短往返十进制表示（既用于编码，也用于 to_string / 显示）。
    inline std::string number_to_text(double value) {
        // IEEE 754 non-finite values get stable, human-readable names
        // (they are real, inspectable results — never a crash or poison).
        // IEEE 754 非有限值给出稳定、可读的名称（它们是真实可检查的结果，
        // 绝非崩溃或毒水）。
        if (!std::isfinite(value)) {
            if (std::isnan(value)) return "NaN";
            return value < 0.0 ? "-Infinity" : "Infinity";
        }
        if (std::abs(value) < 9.0e15 &&
            value == std::floor(value) &&
            value == static_cast<double>(static_cast<long long>(value))) {
            return std::format("{}", static_cast<long long>(value));
        }
        char buffer[64];
        for (int precision = 1; precision <= 17; ++precision) {
            std::snprintf(buffer, sizeof buffer, "%.*g", precision, value);
            if (std::strtod(buffer, nullptr) == value) {
                return buffer;
            }
        }
        return buffer;
    }

    inline runtime::RuntimeObjectPtr cap_number(
        double value, bool is_integer
    ) {
        auto capsule = std::make_shared<runtime::RuntimeObject>();
        capsule->give_name(
            "num:" + number_to_text(value) + (is_integer ? ":i" : ":f")
        );
        return capsule;
    }

    inline runtime::RuntimeObjectPtr cap_string(const std::string& value) {
        auto capsule = std::make_shared<runtime::RuntimeObject>();
        capsule->give_name("str:" + value);
        return capsule;
    }

    inline runtime::RuntimeObjectPtr cap_boolean(bool value) {
        auto capsule = std::make_shared<runtime::RuntimeObject>();
        capsule->give_name(value ? "bool:t" : "bool:f");
        return capsule;
    }

    // Capsule type tag: the part before the first colon in selfname.
    // 胶囊类型标签：selfname 冒号之前的部分。
    inline std::string capsule_tag(
        const runtime::RuntimeObjectPtr& capsule
    ) {
        const std::string& text = capsule->selfname;
        auto stop = text.find(':');
        return stop == std::string::npos ? text : text.substr(0, stop);
    }

    // Numeric capsule payload: num:<value>:<i|f>
    // 数字胶囊负载：num:<数值>:<i|f>
    inline std::optional<double> capsule_number(
        const runtime::RuntimeObjectPtr& capsule
    ) {
        if (capsule_tag(capsule) != "num") {
            return std::nullopt;
        }
        const std::string& text = capsule->selfname;
        auto first = text.find(':');
        auto last = text.rfind(':');
        if (first == std::string::npos || last <= first) {
            return std::nullopt;
        }
        return std::strtod(
            text.substr(first + 1, last - first - 1).c_str(), nullptr
        );
    }

    inline std::optional<bool> capsule_is_integer(
        const runtime::RuntimeObjectPtr& capsule
    ) {
        if (capsule_tag(capsule) != "num") {
            return std::nullopt;
        }
        return capsule->selfname.substr(capsule->selfname.rfind(':') + 1)
            == "i";
    }

    // String capsule payload: str:<text>
    // 字符串胶囊负载：str:<原文>
    inline std::optional<std::string> capsule_string(
        const runtime::RuntimeObjectPtr& capsule
    ) {
        if (capsule_tag(capsule) != "str") {
            return std::nullopt;
        }
        return capsule->selfname.substr(capsule->selfname.find(':') + 1);
    }

    inline std::optional<bool> capsule_boolean(
        const runtime::RuntimeObjectPtr& capsule
    ) {
        if (capsule_tag(capsule) != "bool") {
            return std::nullopt;
        }
        return capsule->selfname == "bool:t";
    }


    // ========================================================
    // Unwrap Helpers
    //
    // unwrap: object -> value capsule. An instance (RuntimeClass) yields
    // its "#value" attribute; a capsule is returned as-is; a behavior
    // (RuntimeBehavior) has no value.
    // unwrap：对象 → 值胶囊。实例（RuntimeClass）取其 "#value"
    // 属性；胶囊原样返回；行为（RuntimeBehavior）无值。
    // ========================================================

    inline const rt_basic::InstanceMap* attributes_of(
        const runtime::RuntimeObjectPtr& object
    ) {
        auto* cls = dynamic_cast<runtime::RuntimeClass*>(object.get());
        return cls ? &cls->get_attributes() : nullptr;
    }

    inline runtime::RuntimeObjectPtr unwrap(
        const runtime::RuntimeObjectPtr& object
    ) {
        if (!object) {
            return nullptr;
        }
        if (auto* attributes = attributes_of(object)) {
            auto found = attributes->find(VALUE_KEY);
            return found == attributes->end() ? nullptr : found->second;
        }
        if (dynamic_cast<runtime::RuntimeBehavior*>(object.get())) {
            return nullptr;
        }
        return object;
    }

    // Read each type's value out of any object (the read side of negotiation).
    // 从任意对象身上读出各类型的值（协商的读侧）。
    inline std::optional<double> number_of(
        const runtime::RuntimeObjectPtr& object
    ) {
        auto capsule = unwrap(object);
        return capsule ? capsule_number(capsule) : std::nullopt;
    }

    inline std::optional<std::string> string_of(
        const runtime::RuntimeObjectPtr& object
    ) {
        auto capsule = unwrap(object);
        return capsule ? capsule_string(capsule) : std::nullopt;
    }

    inline std::optional<bool> boolean_of(
        const runtime::RuntimeObjectPtr& object
    ) {
        auto capsule = unwrap(object);
        return capsule ? capsule_boolean(capsule) : std::nullopt;
    }


    // ========================================================
    // Native error reporting (post poison-water retirement)
    // 原生错误上报（毒水模型退役后）
    // ========================================================
//
// A native method that cannot fulfill its contract raises a `NativeError`
// (a std::exception subclass). The interpreter's top-level driver catches
// it and reports a g++-style diagnostic via Thrower before exiting; the
// async runtime catches it inside `run_closure_isolated` and turns it into
// an `Error` capsule so a failing task never crashes the host. This
// replaces the old poison-water contagion model entirely -- there is no
// longer any "poisoned value" that flows through the computation.
// 无法履约的原生方法抛出 NativeError（std::exception 子类）。解释器顶层
// 驱动捕获它后经 Thrower 上报类 g++ 诊断并退出；异步运行时在
// run_closure_isolated 内捕获，转为 Error 胶囊，使失败任务绝不拖垮宿主。
// 这彻底取代了旧毒水传染模型——不再有任何“流过计算的毒值”。
    struct NativeError : public std::exception {
        std::string what_msg;
        explicit NativeError(std::string m) : what_msg(std::move(m)) {}
        const char* what() const noexcept override { return what_msg.c_str(); }
    };

    // Raise a native runtime error. Never returns.
    // 抛出一个原生运行时错误。永不返回。
    [[noreturn]] inline runtime::RuntimeObjectPtr native_error(
        const std::string& message
    ) {
        throw NativeError(message);
    }
    [[noreturn]] inline runtime::RuntimeObjectPtr native_error(
        runtime::RuntimeObjectPtr /*instance*/, const std::string& message
    ) {
        throw NativeError(message);
    }

    // True when `o` is an `Error` object (the async failure payload).
    // 当 o 是 Error 对象（异步失败负载）时为真。
    inline bool is_error(const runtime::RuntimeObjectPtr& o) {
        if (!o) return false;
        return o->get_etag() == "Error";
    }


    // ========================================================
    // Runtime Helpers
    // ========================================================

    // Empty output list (normal return of a void method).
    // 空输出列表（void 方法的正常返回）。
    inline rt_basic::InstanceListPtr empty_result() {
        return std::make_shared<std::vector<runtime::RuntimeObjectPtr>>();
    }

    inline rt_basic::InstanceListPtr list_of(
        std::initializer_list<runtime::RuntimeObjectPtr> items
    ) {
        return std::make_shared<std::vector<runtime::RuntimeObjectPtr>>(
            items
        );
    }

    // First element of a result list (single-return case); nullptr if empty.
    // 取结果列表的首个元素（单返回值场合），空则 nullptr。
    inline runtime::RuntimeObjectPtr first_of(
        const rt_basic::InstanceListPtr& result
    ) {
        return result && !result->empty() ? (*result)[0] : nullptr;
    }

    inline runtime::RuntimeObjectPtr para_at(
        const rt_basic::InstanceListPtr& paras, std::size_t index
    ) {
        return paras && index < paras->size() ? (*paras)[index] : nullptr;
    }

    // The receiver's scalar value capsule (env IS self).
    // 接收者的标量值胶囊（env 即 self）。
    inline runtime::RuntimeObjectPtr self_value_of(
        const rt_basic::InstanceMap& env
    ) {
        auto found = env.find(VALUE_KEY);
        return found == env.end() ? nullptr : found->second;
    }

    // Call a behavior object; gracefully degrade to poison if not a behavior.
    // 调用一个行为对象；不是行为则优雅降级为毒水。
    inline rt_basic::InstanceListPtr call_behavior(
        const runtime::RuntimeObjectPtr& object,
        rt_basic::InstanceMap& env,
        const rt_basic::InstanceListPtr& paras
    ) {
        auto* behavior =
            dynamic_cast<runtime::RuntimeBehavior*>(object.get());
        if (!behavior) {
            return list_of({native_error("argument must be a behavior")});
        }
        return behavior->call(env, paras);
    }

    // ========================================================
    // Signature Enforcement (runtime call-boundary type checking)
    // ========================================================
    //
    // Native / built-in method signatures (CallableSign.inpara) now carry
    // precise type tags such as "std::Number", "re::Pattern", "std::Array".
    // These are enforced at the call boundary by rt_basic::g_sign_enforcer
    // (wired in init_builtins), which rejects argument-type mismatches
    // before the method body runs — so "a bunch of garbage" can no longer
    // slip into a closure and blow up with a cryptic in-body error.
    // 原生 / 内置方法签名（CallableSign.inpara）现已携带精确类型标签，
    // 如 "std::Number" / "re::Pattern" / "std::Array"。它们由
    // rt_basic::g_sign_enforcer（在 init_builtins 中接线）于调用边界强制
    // 核查，在方法体运行前拒收类型不符的实参，避免一堆乱东西溜进闭包后
    // 再抛出晦涩的内部错误。

    // Resolve a runtime object to its signature type-tag.
    // 把运行时对象解析为签名所用的类型标签。
    inline std::string arg_type_key(const runtime::RuntimeObjectPtr& o) {
        if (!o) return "std::Object";
        // Library / user classes are RuntimeClass: read the authoritative
        // prototype name stamped by Prototypes::regcls.
        // 库 / 用户类为 RuntimeClass：读取 Prototypes::regcls 烙印的权威原型名。
        if (auto* rc = dynamic_cast<runtime::RuntimeClass*>(o.get())) {
            if (auto proto = rc->get_prototype();
                proto && !proto->name.empty()) {
                // A universal `std::Object` that holds a scalar value is
                // signature-matched as that scalar type (duck typing — the
                // root fix for behavior-parameter type-tag degradation: a value
                // threaded through an untyped output parameter kept its scalar
                // value but lost its concrete type, so passing it to a method
                // expecting `std::Number` was wrongly rejected).
                // 持有标量值的通用 std::Object 在签名层面按该标量类型匹配
                //（鸭子式——类型标签退化的根源修复：经无类型输出参数穿线的
                // 数值保留了标量值却丢了具体类型，导致被错误地拒收于要求
                // std::Number 的方法）。
                if (proto->name == "Object") {
                    if (auto* attrs = attributes_of(o)) {
                        auto f = attrs->find(VALUE_KEY);
                        if (f != attrs->end() && f->second) {
                            const std::string tag = capsule_tag(f->second);
                            if (tag == "num")  return "std::Number";
                            if (tag == "str")  return "std::String";
                            if (tag == "bool") return "std::Boolean";
                        }
                    }
                }
                return proto->name;
            }
            return o->selfname.empty() ? "std::Object" : o->selfname;
        }
        // Scalars are capsules: the tag is the part before the first ':' in
        // selfname (e.g. "num:", "str:", "bool:"). Map to the signature
        // namespace so it matches "std::Number" / "std::String" / "std::Boolean".
        // 标量为胶囊：标签即 selfname 中首个 ':' 之前的部分。映射到签名命名空间，
        // 以与 "std::Number" / "std::String" / "std::Boolean" 对齐。
        const std::string& tag = o->selfname;
        auto pos = tag.find(':');
        std::string key = (pos == std::string::npos) ? tag : tag.substr(0, pos);
        if (key == "num")  return "std::Number";
        if (key == "str")  return "std::String";
        if (key == "bool") return "std::Boolean";
        return key.empty() ? "std::Object" : key;
    }

    // Namespace-tolerant type match (the runtime already resolves
    // module::Class == Class via the getcls fallback).
    // 命名空间容错类型比对（runtime 经 getcls 回退把 module::Class 视作 Class）。
    inline bool type_match(
        const std::string& expect, const std::string& key
    ) {
        if (expect == key) return true;
        // rfind("::") finds the last two-character separator; find_last_of()
        // is a character SET and would match a lone ':' (see Runtime::getcls).
        // rfind("::") 定位最后一个双字符分隔符；find_last_of 是字符集，
        // 会匹配单个 ':'（参见 Runtime::getcls 的说明）。
        auto unqual = [](const std::string& s) {
            auto p = s.rfind("::");
            return p == std::string::npos ? s : s.substr(p + 2);
        };
        if (unqual(expect) == unqual(key)) return true;
        // Array / Tuple interchangeability (documented duality).
        // Array / Tuple 互认（文档载明的双重性）。
        bool exArr  = (expect == "std::Array"  || expect == "std::Tuple");
        bool keyArr = (key    == "std::Array"  || key    == "std::Tuple");
        return exArr && keyArr;
    }

    // The real enforcer assigned to rt_basic::g_sign_enforcer in
    // init_builtins. Returns true if the call may proceed.
    // 在 init_builtins 中赋值给 rt_basic::g_sign_enforcer 的真实约束器。
    // 返回 true 表示放行本次调用。
    inline bool enforce_sign(
        const rt_basic::CallableSign& sign,
        const rt_basic::InstanceListPtr& paras
    ) {
        // Flow / lifecycle methods are signature-agnostic by design.
        // 流 / 生命周期方法按设计对签名无感。
        const std::string& mname = sign.name;
        if (mname == "::" || mname == "~" || mname == "=:" ||
            mname == ":=" || mname == "=") {
            return true;
        }
        if (!paras) return true;
        const auto& actual = *paras;
        for (std::size_t i = 0; i < sign.inpara.size(); ++i) {
            const std::string& expect = sign.inpara[i].second;
            // Universal receiver ("@"), universal element tag ("value", used
            // by Array / Dict / Tuple for heterogeneous elements and keys),
            // the universal object type ("std::Object"), and variadic ("...")
            // params are not type-checked at the boundary.
            // 通用接收（"@"）、通用元素标签（"value"，Array / Dict / Tuple
            // 用于异构元素与键）、通用对象类型（"std::Object"）与变参（"..."）
            // 均不在边界做类型核查。
            if (expect == "@" || expect == "value" || expect == "std::Object" ||
                (expect.size() >= 3 &&
                 expect.substr(expect.size() - 3) == "...")) {
                continue;
            }
            // An empty expected type means "untyped / accept anything"
            // (a bare parameter such as `[(x) -> (r)]`). Never reject it.
            // 期望类型为空表示“无类型约束 / 任意类型”（如 `[(x) -> (r)]`
            // 的裸参数），绝不拒收。
            if (expect.empty()) continue;
            // Only concrete runtime types are verifiable at the call boundary.
            // A concrete type is namespaced ("module::Class") or a built-in
            // scalar/container ("std::Number", "std::Array", ...). User
            // constraint names (e.g. "Addable") and user class names are NOT
            // runtime-checkable here — the §9.8 roll-call is a compile-time
            // feature not yet implemented for parameters — so we skip them and
            // never falsely reject a well-formed call.
            // 仅具体运行期类型可在调用边界核查：具体类型要么带命名空间
            // （"module::Class"），要么是内建标量 / 容器（"std::Number"、
            // "std::Array" 等）。用户约束名（如 "Addable"）与用户类名并非
            // 运行期可验证类型——§9.8 点名单是尚未在参数级实现的编译期
            // 特性——故跳过，绝不误拒合法调用。
            bool concrete = (expect.find("::") != std::string::npos)
                         || (expect.rfind("std::", 0) == 0);
            if (!concrete) continue;
            if (i >= actual.size()) {
                // Fewer actuals than declared: defer to in-closure handling.
                // 实参少于声明：交闭包处理（优雅降级）。
                continue;
            }
            const runtime::RuntimeObjectPtr& o = actual[i];
            if (!o) continue;
            // A poisoned actual is deferred to in-closure degradation.
            // 已毒水的实参交闭包降级处理。
            if (!type_match(expect, arg_type_key(o))) {
                // Immediate, duck-typed constraint error (replaces the old
                // poison-water degradation): raise with a precise, traceable
                // diagnostic naming the method and the expected / actual types.
                // 即时、鸭子式约束错误（取代旧的毒水降级）：附精确可追踪的
                // 诊断，点明方法名与期望 / 实际类型。
                Thrower.throwE("TypeException",
                    "Argument type mismatch for method '" + sign.name
                    + "': expected '" + expect + "', got '"
                    + arg_type_key(o) + "'");
                return false; // unreachable: Thrower.throwE terminates
            }
        }
        return true;
    }


    // ========================================================
    // Container Helpers (shared by Array / Dict / Tuple)
    // ========================================================

    inline std::string elem_key(std::size_t index) {
        return ELEM_PREFIX + std::to_string(index);
    }

    inline std::size_t container_size(
        const rt_basic::InstanceMap& env
    ) {
        auto found = env.find(SIZE_KEY);
        if (found == env.end()) {
            return 0;
        }
        auto count = number_of(found->second);
        return count ? static_cast<std::size_t>(std::max(0.0, *count)) : 0;
    }

    inline void set_container_size(
        rt_basic::InstanceMap& env, std::size_t count
    ) {
        env[SIZE_KEY] = cap_number(static_cast<double>(count), true);
    }

    // Dict key encoding: capsules by content (selfname), others by identity
    // (pointer).
    // Dict 的键编码：胶囊按内容（selfname），其余按身份（指针）。
    inline std::string encode_key(const runtime::RuntimeObjectPtr& key) {
        if (auto capsule = unwrap(key)) {
            return capsule->selfname;
        }
        return std::format(
            "obj:{:016x}",
            reinterpret_cast<std::uintptr_t>(key.get())
        );
    }

    // Sequence clone: rebuild a same-type instance from env (the source
    // attribute table) — used by the publish side of ":=".
    // 序列克隆：按 env（源属性表）重建一个同类实例（":=" 的公布侧）。
    inline runtime::RuntimeObjectPtr clone_sequence(
        const rt_basic::InstanceMap& env,
        const rt_basic::ClsProtoPtr& proto,
        const std::string& name
    ) {
        auto replica = std::make_shared<runtime::RuntimeClass>(proto);
        std::size_t count = container_size(env);
        for (std::size_t i = 0; i < count; ++i) {
            auto found = env.find(elem_key(i));
            if (found != env.end()) {
                replica->set_attribute(elem_key(i), found->second);
            }
        }
        replica->set_attribute(
            SIZE_KEY, cap_number(static_cast<double>(count), true)
        );
        replica->give_name(name);
        return replica;
    }

    // Dict clone: copy all "#k:" / "#v:" entries and "#size".
    // 字典克隆：复制全部 "#k:" / "#v:" 条目与 "#size"。
    inline runtime::RuntimeObjectPtr clone_mapping(
        const rt_basic::InstanceMap& env,
        const rt_basic::ClsProtoPtr& proto,
        const std::string& name
    ) {
        auto replica = std::make_shared<runtime::RuntimeClass>(proto);
        for (const auto& [key, value] : env) {
            if (key.starts_with(DICT_KEYPRE) || key.starts_with(DICT_VALPRE)
                || key == SIZE_KEY) {
                replica->set_attribute(key, value);
            }
        }
        replica->give_name(name);
        return replica;
    }


    // ========================================================
    // Display Helpers (shared by OStream output and error messages)
    // ========================================================

    inline std::string display(
        const runtime::RuntimeObjectPtr& object, int depth = 0
    );

    inline std::string display_sequence(
        const rt_basic::InstanceMap& env, int depth
    ) {
        std::string text = "[";
        std::size_t count = container_size(env);
        for (std::size_t i = 0; i < count; ++i) {
            if (i != 0) {
                text += ", ";
            }
            auto found = env.find(elem_key(i));
            text += found != env.end()
                ? display(found->second, depth + 1)
                : "<missing>";
        }
        return text + "]";
    }

    // Encoded key to readable text: str:name -> name, num:42:i -> 42,
    // bool:t -> true, obj:<ptr> -> <object>. Display only.
    // 编码键转可读文本：str:name -> name，num:42:i -> 42，
    // bool:t -> true，obj:<ptr> -> <object>。仅供 display 使用。
    inline std::string key_text(const std::string& encoded) {
        if (encoded.starts_with("str:")) {
            return encoded.substr(4);
        }
        if (encoded.starts_with("num:")) {
            auto body = encoded.substr(4);
            auto cut = body.rfind(':');
            return cut == std::string::npos ? body : body.substr(0, cut);
        }
        if (encoded.starts_with("bool:")) {
            return encoded.substr(5) == "t" ? "true" : "false";
        }
        if (encoded.starts_with("obj:")) {
            return "<object>";
        }
        return encoded;
    }

    inline std::string display_mapping(
        const rt_basic::InstanceMap& env, int depth
    ) {
        std::map<std::string, std::string> pairs;
        for (const auto& [key, value] : env) {
            if (key.starts_with(DICT_VALPRE)) {
                pairs[display(value, depth + 1)] = key_text(key.substr(3));
            }
        }
        std::string text = "{";
        bool first = true;
        for (const auto& [value_text, decoded_key] : pairs) {
            if (!first) {
                text += ", ";
            }
            first = false;
            text += decoded_key + " -> " + value_text;
        }
        return text + "}";
    }

    inline std::string display(
        const runtime::RuntimeObjectPtr& object, int depth
    ) {
        if (!object) {
            return "<null>";
        }
        if (depth > 8) {
            return "...";
        }
        if (dynamic_cast<runtime::RuntimeBehavior*>(object.get())) {
            return "<behavior>";
        }
        if (auto* attributes = attributes_of(object)) {
            bool mapping = false;
            for (const auto& [key, value] : *attributes) {
                (void)value;
                if (key.starts_with(DICT_VALPRE)) {
                    mapping = true;
                    break;
                }
            }
            if (mapping) {
                return display_mapping(*attributes, depth);
            }
            if (attributes->count(SIZE_KEY)) {
                return display_sequence(*attributes, depth);
            }
            auto found = attributes->find(VALUE_KEY);
            return found != attributes->end()
                ? display(found->second, depth + 1)
                : "<object " + object->selfname + ">";
        }
        const std::string tag = capsule_tag(object);
        if (tag == "num") {
            return number_to_text(capsule_number(object).value_or(0.0));
        }
        if (tag == "str") {
            return capsule_string(object).value_or("");
        }
        if (tag == "bool") {
            return capsule_boolean(object).value_or(false) ? "true" : "false";
        }
        return "<" + object->selfname + ">";
    }


    // ========================================================
    // Callable Helpers
    // ========================================================

    inline rt_basic::CallableSign make_sign(
        const std::string& name,
        const std::vector<std::pair<std::string, std::string>>& inpara = {},
        const std::vector<std::pair<std::string, std::string>>& outpara = {}
    ) {
        return rt_basic::CallableSign(name, inpara, outpara);
    }

    // Native method: NativeClosure + signature (constraint) + modifiers.
    // The inpara/outpara type strings of the signature (CallableSign) are
    // the type constraints on arguments and return values — the compiler /
    // interpreter checks them before calling; the runtime layer performs no
    // runtime type guard, so the closure body may trust arguments satisfy
    // the constraint. The modifier {isConst(@!), isPrivate(@#)} is recorded
    // as a Callable attribute for the interpreter to check at rebind /
    // dispatch. State is taken as `{}` (value-initialized = NORMAL,
    // non-const contract); the purity of ~> / => is guaranteed by the
    // closure's read-only discipline (see header note 2).
    // 原生方法：NativeClosure + 签名（约束）+ 修饰符。
    // 签名（CallableSign）的 inpara/outpara 类型串即参数与
    // 返回值的类型约束——编译器 / 解释器在调用前据此核查，
    // runtime 层不做运行时类型守卫，闭包体可信任参数符合约束。
    // 修饰符 {isConst(@!), isPrivate(@#)} 作为 Callable 属性
    // 记录，供解释器在重绑 / 派发时核查。状态取 `{}`（值
    // 初始化 = NORMAL，非常数契约）；~> / => 的纯度由闭包
    // 只读纪律保证（详见文件头注释第 2 条）。
    inline rt_basic::Callable native_method(
        rt_basic::NativeClosure body, rt_basic::CallableSign sign,
        std::pair<bool, bool> attr = {true, false}
    ) {
        return rt_basic::Callable(
            std::move(body), std::move(sign), {}, attr
        );
    }


    // ========================================================
    // Receive / Publish / Assign Closures (shared closure factories)
    //
    // A flow statement (Spec 3.4.2) = sender's "=:" publish + receiver's
    // ":=" receive. The assign method "=" (non-const mode ->) shares the
    // same runtime behavior as the receive function ":=": same-type data
    // overwrites directly, different types degrade gracefully keeping the
    // zero value — this is the principle of ordinary assignment. The only
    // difference is the contract:
    //   ":="  the receive end of a flow statement (published value
    //         negotiates in; containers are cloned via "=:", copy
    //         semantics are carried by the publish side, so the flow
    //         statement itself needs no separate copy logic)
    //   "="    explicit assign method (a.=(b)), overwrites directly, does
    //          not go through publish negotiation; the language designer
    //          designated it a non-const (->) method, not ~> nor =>
    // Method names and signatures differ but closure bodies are identical,
    // so a factory is shared.
    // 流语句（文档 3.4.2）= 发送方 "=:" 公布 + 接收方 ":=" 接收。
    // 赋值方法 "="（非常数模式 ->）与接收函数 ":=" 的运行时
    // 行为同源：同类型数据直接覆盖，异类型优雅降级保持零值
    // ——这就是一般赋值的原理。区别只在契约：
    //   ":="  流语句的接收端（公布值协商进入，容器经 "=:" 克隆，
    //         复制语义由公布侧实现，流语句本身无需单独的复制逻辑）
    //   "="   显式赋值方法（a.=(b)），直接覆盖，不经过公布协商；
    //         语言设计者钦定其为非常数（->）方法，非 ~> 与 =>
    // 方法名与签名不同、闭包体相同，故用工厂共用。
    // ========================================================

    // The root's receive / assign closure: duck-type accept a single
    // published value. What is stored is the "value capsule" (taken out by
    // unwrap), consistent with the scalar receive / self_value_of read
    // path — otherwise reading back from a pure Object instance would
    // yield the original object pointer.
    // 万物之源的接收 / 赋值闭包：鸭子式收下单个公布值。
    // 存的是「值胶囊」（unwrap 取出），与标量接收 / self_value_of
    // 的读路径保持一致——否则从纯 Object 实例读回会变成原对象指针。
    inline rt_basic::NativeClosure object_receive_closure() {
        return [](
            rt_basic::InstanceMap& env,
            rt_basic::InstanceListPtr paras
        ) {
            auto incoming = para_at(paras, 0);
            if (incoming && paras && paras->size() == 1) {
                if (auto capsule = unwrap(incoming)) {
                    env[VALUE_KEY] = capsule;
                }
            }
            return empty_result();
        };
    }

    // Scalar value capsule publish closure: publishes "#value" (shared by
    // Number / Boolean / String and Object; each signature is precise to
    // its own type).
    // 标量值胶囊的公布闭包：公布 "#value"（Number / Boolean /
    // String 与 Object 共用；签名各自精确到本类型）。
    inline rt_basic::NativeClosure value_publish_closure() {
        return [](
            rt_basic::InstanceMap& env,
            rt_basic::InstanceListPtr /*paras*/
        ) {
            auto value = self_value_of(env);
            return value ? list_of({value}) : empty_result();
        };
    }

    // Scalar receive / assign closure: same-type capsule overwrites
    // directly, different types ignored (graceful degradation of smart
    // negotiation, Spec 3.3 / D.8).
    // 标量接收 / 赋值闭包：同类型胶囊直接覆盖，异类型忽略
    // （智能协商的优雅降级，文档 3.3 / D.8）。
    inline rt_basic::NativeClosure scalar_receive_closure(
        const std::string& tag
    ) {
        return [tag](
            rt_basic::InstanceMap& env,
            rt_basic::InstanceListPtr paras
        ) {
            auto incoming = para_at(paras, 0);
            // Single-variable tuple destructuring (spec: 单个变量被元组赋值时
            // 自动取元组首项). When a scalar variable is fed a std::Tuple, bind
            // it to the tuple's FIRST element. Container / stream receivers
            // keep the whole value, so this never strips a tuple handed to an
            // Array / Tuple / OStream.
            // 仅对标量接收方生效；容器与流接收方保留整体值，因此递给
            // Array / Tuple / OStream 的元组不会被拆掉首项。
            if (auto* cls = dynamic_cast<runtime::RuntimeClass*>(incoming.get())) {
                if (cls->get_prototype() == ::stdRT.getcls("Tuple")) {
                    if (auto* src = attributes_of(incoming)) {
                        auto found = src->find(elem_key(0));
                        if (found != src->end()) incoming = found->second;
                    }
                }
            }
            if (auto capsule = unwrap(incoming)) {
                if (capsule_tag(capsule) == tag) {
                    env[VALUE_KEY] = capsule;
                }
            }
            return empty_result();
        };
    }

    // Sequence (Array / Tuple) receive / assign closure: a multi-element
    // publish is received wholesale by position; a single published value
    // copies the other side's sequence elements.
    // 序列（Array / Tuple）接收 / 赋值闭包：多元素公布按位置
    // 整体接收；单个公布值则拷贝对方序列的元素。
    inline rt_basic::NativeClosure sequence_receive_closure() {
        return [](
            rt_basic::InstanceMap& env,
            rt_basic::InstanceListPtr paras
        ) {
            if (paras && paras->size() > 1) {
                std::size_t count = 0;
                for (const auto& item : *paras) {
                    env[elem_key(count)] = item;
                    ++count;
                }
                set_container_size(env, count);
                return empty_result();
            }
            if (auto* source = attributes_of(para_at(paras, 0))) {
                auto count = container_size(*source);
                for (std::size_t i = 0; i < count; ++i) {
                    auto found = source->find(elem_key(i));
                    if (found != source->end()) {
                        env[elem_key(i)] = found->second;
                    }
                }
                set_container_size(env, count);
            }
            return empty_result();
        };
    }

    // Dict receive / assign closure: copy all entries of another dict.
    // 字典接收 / 赋值闭包：拷贝另一个字典的全部条目。
    inline rt_basic::NativeClosure dict_receive_closure() {
        return [](
            rt_basic::InstanceMap& env,
            rt_basic::InstanceListPtr paras
        ) {
            if (auto* source = attributes_of(para_at(paras, 0))) {
                for (const auto& [key, value] : *source) {
                    if (key.starts_with(DICT_KEYPRE)
                        || key.starts_with(DICT_VALPRE)
                        || key == SIZE_KEY) {
                        env[key] = value;
                    }
                }
            }
            return empty_result();
        };
    }


    // ========================================================
    // Instance Factories
    //
    // Used by the interpreter and acceptance code to create built-in
    // object instances. Same general path as Runtime::make (prototype ->
    // RuntimeClass), no special class; rt_builtin's make_xxx are just
    // convenience wrappers that inject the value capsule.
    // 供解释器与验收代码创建内置对象的实例。与 Runtime::make
    // 同一通用通路（原型 → RuntimeClass），无任何特殊类；
    // builtin 的 make_xxx 只是塞入值胶囊的便捷封装。
    // ========================================================

    inline runtime::RuntimeClassPtr instantiate(
        const rt_basic::ClsProtoPtr& proto, const std::string& name
    ) {
        if (!proto) {
            Thrower.throwE(
                "InterException",
                "Prototype <" + name + "> is not ready. "
                "Call init_builtins() first."
            );
        }
        auto instance = std::make_shared<runtime::RuntimeClass>(proto);
        instance->give_name(name);
        return instance;
    }

    // Number (integer-ness only affects display format, Spec 5.5).
    // 数字（整数性与否只影响显示格式，文档 5.5）。
    inline runtime::RuntimeObjectPtr make_number(
        double value, bool is_integer = true
    ) {
        auto instance = instantiate(PT_stdNumber, "std::Number");
        instance->set_attribute(VALUE_KEY, cap_number(value, is_integer));
        return instance;
    }

    inline runtime::RuntimeObjectPtr make_int(std::int64_t value) {
        return make_number(static_cast<double>(value), true);
    }

    inline runtime::RuntimeObjectPtr make_float(double value) {
        return make_number(value, false);
    }

    inline runtime::RuntimeObjectPtr make_string(const std::string& value) {
        auto instance = instantiate(PT_stdString, "std::String");
        instance->set_attribute(VALUE_KEY, cap_string(value));
        return instance;
    }

    inline runtime::RuntimeObjectPtr make_boolean(bool value) {
        auto instance = instantiate(PT_stdBoolean, "std::Boolean");
        instance->set_attribute(VALUE_KEY, cap_boolean(value));
        return instance;
    }

    inline runtime::RuntimeObjectPtr make_array(
        const std::vector<runtime::RuntimeObjectPtr>& elements = {}
    ) {
        auto instance = instantiate(PT_stdArray, "std::Array");
        for (std::size_t i = 0; i < elements.size(); ++i) {
            instance->set_attribute(elem_key(i), elements[i]);
        }
        instance->set_attribute(
            SIZE_KEY,
            cap_number(static_cast<double>(elements.size()), true)
        );
        return instance;
    }

    inline runtime::RuntimeObjectPtr make_dict() {
        auto instance = instantiate(PT_stdDict, "std::Dict");
        instance->set_attribute(SIZE_KEY, cap_number(0.0, true));
        return instance;
    }

    inline runtime::RuntimeObjectPtr make_tuple(
        const std::vector<runtime::RuntimeObjectPtr>& elements = {}
    ) {
        auto instance = instantiate(PT_stdTuple, "std::Tuple");
        for (std::size_t i = 0; i < elements.size(); ++i) {
            instance->set_attribute(elem_key(i), elements[i]);
        }
        instance->set_attribute(
            SIZE_KEY,
            cap_number(static_cast<double>(elements.size()), true)
        );
        return instance;
    }

    // The zero-value instance of the root of all things.
    // 万物之源的零值实例。
    inline runtime::RuntimeObjectPtr make_object() {
        return instantiate(PT_stdObject, "std::Object");
    }

    // Behavior object (RuntimeBehavior is a class provided by runtime).
    // 行为对象（RuntimeBehavior 是 runtime 提供的类）。
    inline runtime::RuntimeObjectPtr make_behavior(
        rt_basic::Callable body
    ) {
        return std::make_shared<runtime::RuntimeBehavior>(std::move(body));
    }

    // io stream instances (Spec 2.2: must be instantiated before use).
    // io 流实例（文档 2.2：必须先实例化才能使用）。
    //
    // io is a C++-backed standard library (lib/cpp/io.hpp) registered by
    // init_stdlibs(); make instantiates it from the global runtime.
    // io 现为 C++ 底层标准库（lib/cpp/io.hpp），由 init_stdlibs() 登记；
    // 此处经全局 runtime 的 make 实例化。
    inline runtime::RuntimeObjectPtr make_ostream() {
        return stdRT.make("io::OStream");
    }

    inline runtime::RuntimeObjectPtr make_istream() {
        return stdRT.make("io::IStream");
    }


    // ========================================================
    // std::Object —— the root of all things (Spec 2.3.1)
    // ========================================================

    // Object.::() ~> (void) —— constructor: called when an object is created.
    // Object.::() ~> (void) —— 构造：对象创建时调用。
    inline rt_basic::Callable method_Object_construct() {
        return native_method(
            [](
                rt_basic::InstanceMap& /*env*/,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                return empty_result();
            },
            make_sign("::", {}, {})
        );
    }

    // Object.~() ~> (void) —— destructor: called when an object is destroyed.
    // Object.~() ~> (void) —— 析构：对象销毁时调用。
    inline rt_basic::Callable method_Object_destruct() {
        return native_method(
            [](
                rt_basic::InstanceMap& /*env*/,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                return empty_result();
            },
            make_sign("~", {}, {})
        );
    }

    // Object.=:() ~> (result) —— publish: the send end of a flow statement.
    // The base version publishes the "#value" capsule; scalar subclasses
    // override with a precise signature (Number / Boolean / String);
    // composite types (Array / Dict / Tuple) each override to a clone
    // publish.
    // Object.=:() ~> (result) —— 公布：流语句的发送方。
    // 基类版本公布 "#value" 值胶囊；标量子类以精确签名覆盖
    // （Number / Boolean / String），复合类型（Array / Dict /
    // Tuple）各自覆盖为克隆公布。
    inline rt_basic::Callable method_Object_publish() {
        return native_method(
            value_publish_closure(),
            make_sign("=:", {}, {{"result", "std::Object"}})
        );
    }

    // Object.:=(value) ~> (void) —— receive: the receive end of a flow
    // statement. The base version duck-type accepts a single published
    // value; scalar subclasses each override to do type negotiation.
    // Object.:=(value) ~> (void) —— 接收：流语句的接收方。
    // 基类版本鸭子式收下单个公布值；标量子类各自覆盖做类型协商。
    inline rt_basic::Callable method_Object_receive() {
        return native_method(
            object_receive_closure(),
            make_sign(":=", {{"value", "std::Object"}}, {})
        );
    }

    // Object.=(value) -> (void) —— assign method (Spec 4.3: = is not an
    // operator, but may be a method name). The closure shares the same
    // origin as := (duck-type direct overwrite — also the principle of
    // ordinary assignment); the contract differs: -> non-const mode (not
    // ~> nor => — assignment mutates the receiver, naturally impure), and
    // it does not go through publish negotiation: in a.=(b), b enters a
    // as-is; copy semantics belong only to the publish side =: of flow
    // statements.
    // Object.=(value) -> (void) —— 赋值方法（文档 4.3：= 不是
    // 运算符，但可以是方法名）。闭包与 := 同源（鸭子式直接
    // 覆盖，这也是一般赋值的原理）；契约不同：-> 非常数模式
    // （非 ~> 与 =>——赋值修改接收者，天然非纯），且不经公布
    // 协商：a.=(b) 中 b 原样进入 a，复制语义只属于流语句的
    // 公布侧 =:。
    inline rt_basic::Callable method_Object_assign() {
        return native_method(
            object_receive_closure(),
            make_sign("=", {{"value", "std::Object"}}, {})
        );
    }

    // Object._case was a poison-water-era exception catcher; the poison-water
    // model is retired, so it no longer exists. Runtime errors are reported
    // directly by the diagnostic reporter (exception_throw.hpp) with a real
    // execution stack. Any use of `_case` now surfaces a normal
    // "Method '_case' not found." diagnostic.
    // Object._case 是毒水时代的异常捕获器；毒水模型已退役，故不再存在。
    // 运行时错误现由诊断上报器（exception_throw.hpp）直接报告并带真实
    // 执行栈。使用 `_case` 现在会按常规报出“Method '_case' not found.”。

    // ========================================================
    // std::Number —— arithmetic (Spec 2.3.2)
    // ========================================================

    namespace detail {

        // General arithmetic: op ∈ { +, -, *, /, % } (Spec 5.5: an operation
        // is a method call).
        // 通用算术：op ∈ { +, -, *, /, % }（文档 5.5：运算即方法调用）。
        inline rt_basic::Callable number_arith(const std::string& op) {
            return native_method(
                [op](
                    rt_basic::InstanceMap& env,
                    rt_basic::InstanceListPtr paras
                ) {
                    auto self = self_value_of(env);
                    auto other = para_at(paras, 0);


                    auto lhs = number_of(self);
                    auto rhs = number_of(other);
                    if (!lhs || !rhs) {
                        return list_of({native_error(
                            make_number(0),
                            "arithmetic operands must be std::Number"
                        )});
                    }

                    // IEEE 754 arithmetic: division / modulo by zero yields
                    // ±Infinity (and 0/0 or modulus-by-0 yields NaN) — these
                    // are real, inspectable results, not poison. Other
                    // non-finite combinations (inf - inf, 0 * inf, ...) follow
                    // the standard and also produce NaN.
                    // IEEE 754 算术：除以 0 得 ±Infinity（0/0 与模 0 得 NaN）
                    // —— 这些是可检查的真实结果，而非毒水。其余非有限组合
                    // （inf - inf、0 * inf 等）按标准同样得 NaN。
                    double result = 0.0;
                    if (op == "+") {
                        result = *lhs + *rhs;
                    } else if (op == "-") {
                        result = *lhs - *rhs;
                    } else if (op == "*") {
                        result = *lhs * *rhs;
                    } else if (op == "/") {
                        result = *lhs / *rhs;
                    } else {
                        result = std::fmod(*lhs, *rhs);
                    }
                    bool integral =
                        capsule_is_integer(unwrap(self)).value_or(false)
                        && capsule_is_integer(unwrap(other)).value_or(false)
                        && std::isfinite(result)
                        && (op != "/" || std::fmod(*lhs, *rhs) == 0.0);
                    return list_of({make_number(result, integral)});
                },
                make_sign(
                    op, {{"other", "std::Number"}}, {{"result", "std::Number"}}
                )
            );
        }

        // General comparison: the six comparison methods of Spec 2.3.2.
        // 通用比较：文档 2.3.2 的六个比较方法。
        inline rt_basic::Callable number_compare(
            const std::string& name, bool (*compare)(double, double)
        ) {
            return native_method(
                [name, compare](
                    rt_basic::InstanceMap& env,
                    rt_basic::InstanceListPtr paras
                ) {
                    auto self = self_value_of(env);
                    auto other = para_at(paras, 0);

                    auto lhs = number_of(self);
                    auto rhs = number_of(other);
                    if (!lhs || !rhs) {
                        return list_of({native_error(
                            make_boolean(false),
                            "comparison operands must be std::Number"
                        )});
                    }
                    (void)name;
                    return list_of({make_boolean(compare(*lhs, *rhs))});
                },
                make_sign(
                    name,
                    {{"other", "std::Number"}},
                    {{"result", "std::Boolean"}}
                )
            );
        }

        inline bool num_lt(double a, double b) { return a < b; }
        inline bool num_gt(double a, double b) { return a > b; }
        inline bool num_le(double a, double b) { return a <= b; }
        inline bool num_ge(double a, double b) { return a >= b; }
        inline bool num_eq(double a, double b) { return a == b; }
        inline bool num_ne(double a, double b) { return a != b; }

    } // namespace detail

    inline rt_basic::Callable method_Number_add() {
        return detail::number_arith("+");
    }
    inline rt_basic::Callable method_Number_sub() {
        return detail::number_arith("-");
    }
    inline rt_basic::Callable method_Number_mul() {
        return detail::number_arith("*");
    }
    inline rt_basic::Callable method_Number_div() {
        return detail::number_arith("/");
    }
    inline rt_basic::Callable method_Number_mod() {
        return detail::number_arith("%");
    }

    inline rt_basic::Callable method_Number_lt() {
        return detail::number_compare("<", detail::num_lt);
    }
    inline rt_basic::Callable method_Number_gt() {
        return detail::number_compare(">", detail::num_gt);
    }
    inline rt_basic::Callable method_Number_le() {
        return detail::number_compare("<=", detail::num_le);
    }
    inline rt_basic::Callable method_Number_ge() {
        return detail::number_compare(">=", detail::num_ge);
    }
    inline rt_basic::Callable method_Number_eq() {
        return detail::number_compare("==", detail::num_eq);
    }
    inline rt_basic::Callable method_Number_ne() {
        return detail::number_compare("!=", detail::num_ne);
    }

    // Number.to_string() => (result) —— decimal string representation.
    // Number.to_string() => (result) —— 十进制字符串表示。
    inline rt_basic::Callable method_Number_to_string() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                auto self = self_value_of(env);
                auto value = number_of(self);
                if (!value) {
                    return list_of({native_error(
                        make_string(""),
                        "to_string receiver must be std::Number"
                    )});
                }
                return list_of({make_string(number_to_text(*value))});
            },
            make_sign("to_string", {}, {{"result", "std::String"}})
        );
    }

    // Number.repeat_(body) -> (value) —— loop (Spec 7.3). self is the loop
    // count; body has the shape [(state) -> (state)]; state starts from the
    // zero value (Number 0); returns the last body return value.
    // Number.repeat_(body) -> (value) —— 循环（文档 7.3）。
    // self 为循环次数；body 形如 [(state) -> (state)]；
    // state 从零值（Number 0）开始；返回最后一次 body 的返回值。
    inline rt_basic::Callable method_Number_repeat_() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto self = self_value_of(env);
                auto count = number_of(self);
                if (!count) {
                    return list_of({native_error(
                        make_int(0),
                        "repeat_ receiver must be std::Number"
                    )});
                }
                auto rounds = static_cast<long long>(*count);
                if (rounds < 0) {
                    rounds = 0;
                }

                auto state = list_of({make_int(0)});
                for (long long i = 0; i < rounds; ++i) {
                    auto next =
                        call_behavior(para_at(paras, 0), env, state);
                    if (auto head = first_of(next)) {
                        state = list_of({head});
                    }
                }
                return state;
            },
            make_sign(
                "repeat_", {{"body", "@"}}, {{"value", "value"}}
            )
        );
    }

    // Number.=:() ~> (std::Number) —— publish value capsule (overrides base,
    // output signature precise to std::Number for compile-time flow-type
    // checking).
    // Number.=:() ~> (std::Number) —— 公布值胶囊（覆盖基类，
    // 输出签名精确到 std::Number，供编译期流语句类型核查）。
    inline rt_basic::Callable method_Number_publish() {
        return native_method(
            value_publish_closure(),
            make_sign("=:", {}, {{"result", "std::Number"}})
        );
    }

    // Number.:=(value) ~> (void) —— receive (smart negotiation). Only accepts
    // Number values; on negotiation failure keeps the zero value, never
    // forces a conversion (D.8). A poisoned value capsule is accepted as-is
    // — assignment is contagion (Spec 10.1.2).
    // Number.:=(value) ~> (void) —— 接收（智能协商）。
    // 只接受 Number 值；协商失败保持零值，绝不暴力转换（D.8）。
    // 毒水值胶囊照单全收——赋值即传染（文档 10.1.2）。
    inline rt_basic::Callable method_Number_receive() {
        return native_method(
            scalar_receive_closure("num"),
            make_sign(
                ":=", {{"value", "std::Number"}}, {}
            )
        );
    }

    // Number.=(value) -> (void) —— assign method: same-type overwrites
    // directly, different types degrade gracefully keeping the zero value.
    // Closure shares the same origin as := (scalar factory); contract
    // differs: -> non-const mode, no publish negotiation (b enters as-is).
    // Number.=(value) -> (void) —— 赋值方法：同类型直接覆盖，
    // 异类型优雅降级保持零值。闭包与 := 同源（scalar 工厂），
    // 契约不同：-> 非常数模式，不经公布协商（b 原样进入）。
    inline rt_basic::Callable method_Number_assign() {
        return native_method(
            scalar_receive_closure("num"),
            make_sign(
                "=", {{"value", "std::Number"}}, {}
            )
        );
    }


    // ========================================================
    // std::Boolean —— control flow (Spec 2.3.2 / Chapter 7)
    // ========================================================

    // Boolean.if_(true_branch, false_branch) -> (value) (Spec 7.1). self true
    // executes true_branch, else false_branch; value is the executed
    // branch's output.
    // Boolean.if_(true_branch, false_branch) -> (value)（文档 7.1）。
    // self 为真时执行 true_branch，否则执行 false_branch；
    // value 为所执行分支的输出。
    inline rt_basic::Callable method_Boolean_if_() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto self = self_value_of(env);
                bool flag = boolean_of(self).value_or(false);
                return call_behavior(
                    flag ? para_at(paras, 0) : para_at(paras, 1),
                    env,
                    empty_result()
                );
            },
            make_sign(
                "if_",
                {{"true_branch", "@"}, {"false_branch", "@"}},
                {{"value", "value"}}
            )
        );
    }

    // Boolean.while_(body, condition_check) -> (value) (Spec 7.2). self is the
    // initial condition; body has the shape [(state) -> (state)],
    // condition_check has the shape [(state) ~> (flag)] (condition checked
    // after); state starts from the zero value; returns the last body return
    // value.
    // Boolean.while_(body, condition_check) -> (value)（文档 7.2）。
    // self 为初始条件；body 形如 [(state) -> (state)]，
    // condition_check 形如 [(state) ~> (flag)]（条件检查在后）；
    // state 从零值开始；返回最后一次 body 的返回值。
    inline rt_basic::Callable method_Boolean_while_() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto self = self_value_of(env);
                bool flag = boolean_of(self).value_or(false);

                auto state = list_of({make_int(0)});
                while (flag) {
                    auto stepped =
                        call_behavior(para_at(paras, 0), env, state);
                    if (auto head = first_of(stepped)) {
                        state = list_of({head});
                    }
                    auto checked =
                        call_behavior(para_at(paras, 1), env, state);
                    flag = boolean_of(first_of(checked)).value_or(false);
                }
                return state;
            },
            make_sign(
                "while_",
                {{"body", "@"}, {"condition_check", "@"}},
                {{"value", "value"}}
            )
        );
    }

    // Boolean.=:() ~> (std::Boolean) —— publish value capsule (precise sign).
    // Boolean.=:() ~> (std::Boolean) —— 公布值胶囊（精确签名）。
    inline rt_basic::Callable method_Boolean_publish() {
        return native_method(
            value_publish_closure(),
            make_sign("=:", {}, {{"result", "std::Boolean"}})
        );
    }

    // Boolean.:=(value) ~> (void) —— only accepts boolean values, others
    // degrade gracefully.
    // Boolean.:=(value) ~> (void) —— 只接受布尔值，其余优雅降级。
    inline rt_basic::Callable method_Boolean_receive() {
        return native_method(
            scalar_receive_closure("bool"),
            make_sign(
                ":=", {{"value", "std::Boolean"}}, {}
            )
        );
    }

    // Boolean.=(value) -> (void) —— assign method (shared closure, -> contract).
    // Boolean.=(value) -> (void) —— 赋值方法（同源闭包，-> 契约）。
    inline rt_basic::Callable method_Boolean_assign() {
        return native_method(
            scalar_receive_closure("bool"),
            make_sign(
                "=", {{"value", "std::Boolean"}}, {}
            )
        );
    }


    // ========================================================
    // std::String —— immutable character sequence (Spec 2.3.2)
    // ========================================================

    // String.+(other) => (result) —— concatenation, returns a new string
    // (the original is unchanged).
    // String.+(other) => (result) —— 拼接，返回新字符串（原串不变）。
    inline rt_basic::Callable method_String_add() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto self = self_value_of(env);
                auto other = para_at(paras, 0);
                auto lhs = string_of(self);
                auto rhs = string_of(other);
                if (!lhs || !rhs) {
                    return list_of({native_error(
                        make_string(""),
                        "concatenation operand must be std::String"
                    )});
                }
                return list_of({make_string(*lhs + *rhs)});
            },
            make_sign(
                "+", {{"other", "std::String"}}, {{"result", "std::String"}}
            )
        );
    }

    // String.upper() / lower() / reverse() => (result)
    // String.upper() / lower() / reverse() => (result)
    inline rt_basic::Callable method_String_upper() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                auto text = string_of(self_value_of(env));
                if (!text) {
                    return list_of({native_error(
                        make_string(""), "receiver must be std::String"
                    )});
                }
                std::string upper;
                for (unsigned char ch : *text) {
                    upper += static_cast<char>(std::toupper(ch));
                }
                return list_of({make_string(upper)});
            },
            make_sign("upper", {}, {{"result", "std::String"}})
        );
    }

    inline rt_basic::Callable method_String_lower() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                auto text = string_of(self_value_of(env));
                if (!text) {
                    return list_of({native_error(
                        make_string(""), "receiver must be std::String"
                    )});
                }
                std::string lower;
                for (unsigned char ch : *text) {
                    lower += static_cast<char>(std::tolower(ch));
                }
                return list_of({make_string(lower)});
            },
            make_sign("lower", {}, {{"result", "std::String"}})
        );
    }

    inline rt_basic::Callable method_String_reverse() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                auto text = string_of(self_value_of(env));
                if (!text) {
                    return list_of({native_error(
                        make_string(""), "receiver must be std::String"
                    )});
                }
                return list_of({make_string(std::string(text->rbegin(), text->rend()))});
            },
            make_sign("reverse", {}, {{"result", "std::String"}})
        );
    }

    // String.length() => (result) —— character count.
    // String.length() => (result) —— 字符个数。
    inline rt_basic::Callable method_String_length() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                auto text = string_of(self_value_of(env));
                if (!text) {
                    return list_of({native_error(
                        make_int(0), "receiver must be std::String"
                    )});
                }
                return list_of({make_int(
                    static_cast<std::int64_t>(text->size())
                )});
            },
            make_sign("length", {}, {{"result", "std::Number"}})
        );
    }

    // String.get(index) => (result) —— the index-th character (from 0).
    // String.get(index) => (result) —— 第 index 个字符（从 0 开始）。
    inline rt_basic::Callable method_String_get() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto text = string_of(self_value_of(env));
                auto index = number_of(para_at(paras, 0));
                if (!text || !index) {
                    return list_of({native_error(
                        make_string(""), "get requires a std::String receiver and a std::Number index"
                    )});
                }
                // 0-based numbering: user index 0 means the first character.
                // 编号从 0 开始：用户传入 0 即第一个字符。
                if (*index < 0
                    || static_cast<std::size_t>(*index) >= text->size()) {
                    return list_of({native_error(
                        make_string(""), "string index out of bounds"
                    )});
                }
                return list_of({make_string(
                    std::string(1, (*text)[static_cast<std::size_t>(*index)])
                )});
            },
            make_sign(
                "get", {{"index", "std::Number"}}, {{"result", "std::String"}}
            )
        );
    }

    // String.contains(sub) => (result) —— whether it contains the substring.
    // String.contains(sub) => (result) —— 是否包含子串。
    inline rt_basic::Callable method_String_contains() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto text = string_of(self_value_of(env));
                auto sub = string_of(para_at(paras, 0));
                if (!text || !sub) {
                    return list_of({native_error(
                        make_boolean(false),
                        "contains requires a std::String receiver and a std::String argument"
                    )});
                }
                return list_of({make_boolean(
                    text->find(*sub) != std::string::npos
                )});
            },
            make_sign(
                "contains",
                {{"sub", "std::String"}},
                {{"result", "std::Boolean"}}
            )
        );
    }

    // String.slice(start, end) => (result) —— substring [start, end).
    // String.slice(start, end) => (result) —— [start, end) 子串。
    inline rt_basic::Callable method_String_slice() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto text = string_of(self_value_of(env));
                auto start = number_of(para_at(paras, 0));
                auto end = number_of(para_at(paras, 1));
                if (!text || !start || !end) {
                    return list_of({native_error(
                        make_string(""),
                        "slice requires a std::String receiver and two std::Number bounds"
                    )});
                }
                auto size = static_cast<double>(text->size());
                auto from = std::clamp(*start, 0.0, size);
                auto to = std::clamp(*end, from, size);
                return list_of({make_string(
                    text->substr(
                        static_cast<std::size_t>(from),
                        static_cast<std::size_t>(to - from)
                    )
                )});
            },
            make_sign(
                "slice",
                {{"start", "std::Number"}, {"end", "std::Number"}},
                {{"result", "std::String"}}
            )
        );
    }

    // String.=:() ~> (std::String) —— publish value capsule (precise sign).
    // String.=:() ~> (std::String) —— 公布值胶囊（精确签名）。
    inline rt_basic::Callable method_String_publish() {
        return native_method(
            value_publish_closure(),
            make_sign("=:", {}, {{"result", "std::String"}})
        );
    }

    // String.:=(value) ~> (void) —— only accepts strings.
    // String.:=(value) ~> (void) —— 只接受字符串。
    inline rt_basic::Callable method_String_receive() {
        return native_method(
            scalar_receive_closure("str"),
            make_sign(
                ":=", {{"value", "std::String"}}, {}
            )
        );
    }

    // String.=(value) -> (void) —— assign method (shared closure, -> contract).
    // String.=(value) -> (void) —— 赋值方法（同源闭包，-> 契约）。
    inline rt_basic::Callable method_String_assign() {
        return native_method(
            scalar_receive_closure("str"),
            make_sign(
                "=", {{"value", "std::String"}}, {}
            )
        );
    }


    // ========================================================
    // std::Array —— dynamic array (equivalent to std::vector)
    // ========================================================

    // Array.push_back(value) -> (void) —— append an element at the end.
    // Array.push_back(value) -> (void) —— 末尾追加元素。
    inline rt_basic::Callable method_Array_push_back() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                if (auto incoming = para_at(paras, 0)) {
                    auto count = container_size(env);
                    env[elem_key(count)] = incoming;
                    set_container_size(env, count + 1);
                }
                return empty_result();
            },
            make_sign("push_back", {{"value", "value"}}, {})
        );
    }

    // Array.get(index) ~> (value) —— fetch element by index (from 0).
    // Array.get(index) ~> (value) —— 按索引取元素（从 0 开始）。
    inline rt_basic::Callable method_Array_get() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto index = number_of(para_at(paras, 0));
                if (!index) {
                    return list_of({native_error("array index must be std::Number")});
                }
                auto count = container_size(env);
                // 0-based numbering: user index 0 means the first element.
                // 编号从 0 开始：用户传入 0 即第一个元素。
                if (*index < 0
                    || static_cast<std::size_t>(*index) >= count) {
                    return list_of({native_error("array index out of bounds")});
                }
                auto found = env.find(elem_key(static_cast<std::size_t>(*index)));
                return list_of({found != env.end()
                    ? found->second
                    : native_error("array element missing")});
            },
            make_sign(
                "get", {{"index", "std::Number"}}, {{"value", "value"}}
            )
        );
    }

    // Array.size() ~> (result) —— element count.
    // Array.size() ~> (result) —— 元素个数。
    inline rt_basic::Callable method_Array_size() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                return list_of({make_int(
                    static_cast<std::int64_t>(container_size(env))
                )});
            },
            make_sign("size", {}, {{"result", "std::Number"}})
        );
    }

    // Array.pop_back() -> (void) —— remove the last element.
    // Array.pop_back() -> (void) —— 移除末尾元素。
    inline rt_basic::Callable method_Array_pop_back() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                auto count = container_size(env);
                if (count > 0) {
                    env.erase(elem_key(count - 1));
                    set_container_size(env, count - 1);
                }
                return empty_result();
            },
            make_sign("pop_back", {}, {})
        );
    }

    // Array.remove(index) -> (void) —— remove the index-th element.
    // Array.remove(index) -> (void) —— 删除第 index 个元素。
    inline rt_basic::Callable method_Array_remove() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto index = number_of(para_at(paras, 0));
                auto count = container_size(env);
                // 0-based numbering: user index 0 means the first element.
                // 编号从 0 开始：用户传入 0 即第一个元素。
                if (!index || *index < 0
                    || static_cast<std::size_t>(*index) >= count) {
                    return empty_result();
                }
                auto at = static_cast<std::size_t>(*index);
                for (std::size_t i = at; i + 1 < count; ++i) {
                    env[elem_key(i)] = env[elem_key(i + 1)];
                }
                env.erase(elem_key(count - 1));
                set_container_size(env, count - 1);
                return empty_result();
            },
            make_sign(
                "remove", {{"index", "std::Number"}}, {}
            )
        );
    }

    // Array.insert(index, value) -> (void) —— insert at index.
    // Array.insert(index, value) -> (void) —— 在 index 处插入。
    inline rt_basic::Callable method_Array_insert() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto index = number_of(para_at(paras, 0));
                auto incoming = para_at(paras, 1);
                if (!index || !incoming) {
                    return empty_result();
                }
                auto count = container_size(env);
                // 0-based numbering: user index 0 inserts before the first
                // element. Clamp keeps the internal position in range.
                // 编号从 0 开始：用户传入 0 即在首个元素之前插入。
                auto at = static_cast<std::size_t>(
                    std::clamp(*index, 0.0, static_cast<double>(count))
                );
                for (std::size_t i = count; i > at; --i) {
                    env[elem_key(i)] = env[elem_key(i - 1)];
                }
                env[elem_key(at)] = incoming;
                set_container_size(env, count + 1);
                return empty_result();
            },
            make_sign(
                "insert",
                {{"index", "std::Number"}, {"value", "value"}},
                {}
            )
        );
    }

    // Array.clear() -> (void) —— remove all elements.
    // Array.clear() -> (void) —— 清空所有元素。
    inline rt_basic::Callable method_Array_clear() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                auto count = container_size(env);
                for (std::size_t i = 0; i < count; ++i) {
                    env.erase(elem_key(i));
                }
                set_container_size(env, 0);
                return empty_result();
            },
            make_sign("clear", {}, {})
        );
    }

    // Array.front() ~> (value) —— first element; degrades to poison on empty.
    // Array.front() ~> (value) —— 首元素；空数组带毒降级。
    inline rt_basic::Callable method_Array_front() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                if (container_size(env) == 0) {
                    return list_of({native_error("array is empty, no first element")});
                }
                auto found = env.find(elem_key(0));
                return list_of({found != env.end()
                    ? found->second
                    : native_error("array element missing")});
            },
            make_sign("front", {}, {{"value", "value"}})
        );
    }

    // Array.back() ~> (value) —— last element; degrades to poison on empty.
    // Array.back() ~> (value) —— 末元素；空数组带毒降级。
    inline rt_basic::Callable method_Array_back() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                auto count = container_size(env);
                if (count == 0) {
                    return list_of({native_error("array is empty, no last element")});
                }
                auto found = env.find(elem_key(count - 1));
                return list_of({found != env.end()
                    ? found->second
                    : native_error("array element missing")});
            },
            make_sign("back", {}, {{"value", "value"}})
        );
    }

    // Array.=:() ~> (result) —— publish a copy of itself.
    // Array.=:() ~> (result) —— 公布自身的拷贝。
    inline rt_basic::Callable method_Array_publish() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                return list_of({clone_sequence(
                    env, PT_stdArray, "std::Array"
                )});
            },
            make_sign("=:", {}, {{"result", "std::Array"}})
        );
    }

    // Array.:=(value) ~> (void) —— receive another array's element copy; a
    // multi-element publish (literal) is received wholesale by position.
    // Array.:=(value) ~> (void) —— 接收另一个数组的元素拷贝；
    // 多元素公布（字面量）按位置整体接收。
    inline rt_basic::Callable method_Array_receive() {
        return native_method(
            sequence_receive_closure(),
            make_sign(
                ":=", {{"value", "std::Array"}}, {}
            )
        );
    }

    // Array.=(value) -> (void) —— assign method: overwrite elements wholly.
    // Closure shares the same origin as := (sequence factory); -> non-const
    // contract, no publish negotiation — a.=(b) does not clone b (copying
    // belongs only to the =: side of flow statements).
    // Array.=(value) -> (void) —— 赋值方法：整体覆盖元素。
    // 闭包与 := 同源（sequence 工厂）；-> 非常数契约，不经
    // 公布协商——a.=(b) 不克隆 b（复制只属于流语句的 =: 侧）。
    inline rt_basic::Callable method_Array_assign() {
        return native_method(
            sequence_receive_closure(),
            make_sign(
                "=", {{"value", "std::Array"}}, {}
            )
        );
    }


    // ========================================================
    // std::Dict —— key-value container
    // ========================================================

    // Dict.get(key) ~> (value) —— fetch by key; degrades to poison when the
    // key does not exist.
    // Dict.get(key) ~> (value) —— 按键取值；键不存在时毒水降级。
    inline rt_basic::Callable method_Dict_get() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto key = para_at(paras, 0);
                if (!key) {
                    return list_of({native_error("missing key argument")});
                }
                auto found = env.find(DICT_VALPRE + encode_key(key));
                return list_of({found != env.end()
                    ? found->second
                    : native_error("key does not exist")});
            },
            make_sign("get", {{"key", "value"}}, {{"value", "value"}})
        );
    }

    // Dict.set(key, value) -> (void) —— set a key-value pair, overwrite if
    // it already exists.
    // Dict.set(key, value) -> (void) —— 设置键值对，已存在则覆盖。
    inline rt_basic::Callable method_Dict_set() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto key = para_at(paras, 0);
                auto value = para_at(paras, 1);
                if (!key || !value) {
                    return empty_result();
                }
                auto slot = DICT_VALPRE + encode_key(key);
                bool existed = env.count(slot) != 0;
                env[DICT_KEYPRE + slot.substr(3)] = key;
                env[slot] = value;
                if (!existed) {
                    set_container_size(env, container_size(env) + 1);
                }
                return empty_result();
            },
            make_sign(
                "set", {{"key", "value"}, {"value", "value"}},
                {}
            )
        );
    }

    // Dict.remove(key) -> (void) —— remove a key-value pair.
    // Dict.remove(key) -> (void) —— 删除键值对。
    inline rt_basic::Callable method_Dict_remove() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto key = para_at(paras, 0);
                if (!key) {
                    return empty_result();
                }
                auto slot = DICT_VALPRE + encode_key(key);
                if (env.erase(slot) != 0) {
                    env.erase(DICT_KEYPRE + slot.substr(3));
                    set_container_size(env, container_size(env) - 1);
                }
                return empty_result();
            },
            make_sign("remove", {{"key", "value"}}, {})
        );
    }

    // Dict.has(key) ~> (result) —— whether it contains the key.
    // Dict.has(key) ~> (result) —— 是否含键。
    inline rt_basic::Callable method_Dict_has() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto key = para_at(paras, 0);
                return list_of({make_boolean(
                    key
                    && env.count(DICT_VALPRE + encode_key(key)) != 0
                )});
            },
            make_sign("has", {{"key", "value"}}, {{"result", "std::Boolean"}})
        );
    }

    // Dict.size() ~> (result) —— number of key-value pairs.
    // Dict.size() ~> (result) —— 键值对数量。
    inline rt_basic::Callable method_Dict_size() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                return list_of({make_int(
                    static_cast<std::int64_t>(container_size(env))
                )});
            },
            make_sign("size", {}, {{"result", "std::Number"}})
        );
    }

    // Dict.keys() ~> (result) —— an array of all keys (by encoded order,
    // stable).
    // Dict.keys() ~> (result) —— 所有键组成的数组（按编码序，稳定）。
    inline rt_basic::Callable method_Dict_keys() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                std::map<std::string, runtime::RuntimeObjectPtr> ordered;
                for (const auto& [key, value] : env) {
                    if (key.starts_with(DICT_KEYPRE)) {
                        ordered[key.substr(DICT_KEYPRE.size())] = value;
                    }
                }
                std::vector<runtime::RuntimeObjectPtr> keys;
                keys.reserve(ordered.size());
                for (const auto& [encoded, key] : ordered) {
                    (void)encoded;
                    keys.push_back(key);
                }
                return list_of({make_array(keys)});
            },
            make_sign("keys", {}, {{"result", "std::Array"}})
        );
    }

    // Dict.values() ~> (result) —— an array of all values (same order as
    // keys).
    // Dict.values() ~> (result) —— 所有值组成的数组（与 keys 同序）。
    inline rt_basic::Callable method_Dict_values() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                std::map<std::string, runtime::RuntimeObjectPtr> ordered;
                for (const auto& [key, value] : env) {
                    if (key.starts_with(DICT_VALPRE)) {
                        ordered[key.substr(DICT_VALPRE.size())] = value;
                    }
                }
                std::vector<runtime::RuntimeObjectPtr> values;
                values.reserve(ordered.size());
                for (const auto& [encoded, value] : ordered) {
                    (void)encoded;
                    values.push_back(value);
                }
                return list_of({make_array(values)});
            },
            make_sign("values", {}, {{"result", "std::Array"}})
        );
    }

    // Dict.=:() ~> (result) —— publish a copy of itself.
    // Dict.=:() ~> (result) —— 公布自身的拷贝。
    inline rt_basic::Callable method_Dict_publish() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                return list_of({clone_mapping(env, PT_stdDict, "std::Dict")});
            },
            make_sign("=:", {}, {{"result", "std::Dict"}})
        );
    }

    // Dict.:=(value) ~> (void) —— receive another dict's entry copy.
    // Dict.:=(value) ~> (void) —— 接收另一个字典的条目拷贝。
    inline rt_basic::Callable method_Dict_receive() {
        return native_method(
            dict_receive_closure(),
            make_sign(":=", {{"value", "std::Dict"}}, {})
        );
    }

    // Dict.=(value) -> (void) —— assign method: overwrite entries wholly.
    // Closure shares the same origin as := (dict factory); -> non-const
    // contract, no publish negotiation.
    // Dict.=(value) -> (void) —— 赋值方法：整体覆盖条目。
    // 闭包与 := 同源（dict 工厂）；-> 非常数契约，不经公布协商。
    inline rt_basic::Callable method_Dict_assign() {
        return native_method(
            dict_receive_closure(),
            make_sign("=", {{"value", "std::Dict"}}, {})
        );
    }


    // ========================================================
    // std::Tuple —— immutable value sequence (Spec 5.4)
    // ========================================================

    // Tuple.get(index) ~> (value) —— fetch element by index (from 0).
    // Tuple.get(index) ~> (value) —— 按索引取元素（从 0 开始）。
    inline rt_basic::Callable method_Tuple_get() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr paras
            ) {
                auto index = number_of(para_at(paras, 0));
                if (!index) {
                    return list_of({native_error("tuple index must be std::Number")});
                }
                auto count = container_size(env);
                // 0-based numbering: user index 0 means the first element.
                // 编号从 0 开始：用户传入 0 即第一个元素。
                if (*index < 0
                    || static_cast<std::size_t>(*index) >= count) {
                    return list_of({native_error("tuple index out of bounds")});
                }
                auto found = env.find(elem_key(static_cast<std::size_t>(*index)));
                return list_of({found != env.end()
                    ? found->second
                    : native_error("tuple element missing")});
            },
            make_sign("get", {{"index", "std::Number"}}, {{"value", "value"}})
        );
    }

    // Tuple.make(elements) => (result) —— build a new tuple from several
    // elements. The only method that does not need self (does not touch
    // env).
    // Tuple.make(elements) => (result) —— 从若干元素构造新元组。
    // 唯一不需要 self 的方法（不触碰 env）。
    inline rt_basic::Callable method_Tuple_make() {
        return native_method(
            [](
                rt_basic::InstanceMap& /*env*/,
                rt_basic::InstanceListPtr paras
            ) {
                std::vector<runtime::RuntimeObjectPtr> elements;
                if (paras) {
                    elements = *paras;
                }
                // Explicit qualification: avoid ADL resolving make_tuple to
                // std::make_tuple.
                // 显式限定：避免 ADL 把 make_tuple 解析成 std::make_tuple。
                return list_of({rt_builtin::make_tuple(elements)});
            },
            make_sign("make", {{"elements", "value..."}},
                {{"result", "std::Tuple"}})
        );
    }

    // Tuple.size() ~> (result) —— element count.
    // Tuple.size() ~> (result) —— 元素个数。
    inline rt_basic::Callable method_Tuple_size() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                return list_of({make_int(
                    static_cast<std::int64_t>(container_size(env))
                )});
            },
            make_sign("size", {}, {{"result", "std::Number"}})
        );
    }

    // Tuple.=:() ~> (result) —— publish a copy of itself.
    // Tuple.=:() ~> (result) —— 公布自身的拷贝。
    inline rt_basic::Callable method_Tuple_publish() {
        return native_method(
            [](
                rt_basic::InstanceMap& env,
                rt_basic::InstanceListPtr /*paras*/
            ) {
                return list_of({clone_sequence(
                    env, PT_stdTuple, "std::Tuple"
                )});
            },
            make_sign("=:", {}, {{"result", "std::Tuple"}})
        );
    }

    // Tuple.:=(value) ~> (void) —— receive tuple copy / multi-element literal.
    // Tuple.:=(value) ~> (void) —— 接收元组拷贝 / 多元素字面量。
    inline rt_basic::Callable method_Tuple_receive() {
        return native_method(
            sequence_receive_closure(),
            make_sign(":=", {{"value", "std::Tuple"}}, {})
        );
    }

    // Tuple.=(value) -> (void) —— assign method: overwrite elements wholly.
    // Closure shares the same origin as := (sequence factory); -> non-const
    // contract.
    // Tuple.=(value) -> (void) —— 赋值方法：整体覆盖元素。
    // 闭包与 := 同源（sequence 工厂）；-> 非常数契约。
    inline rt_basic::Callable method_Tuple_assign() {
        return native_method(
            sequence_receive_closure(),
            make_sign("=", {{"value", "std::Tuple"}}, {})
        );
    }


    // NOTE: io::OStream / io::IStream moved to lib/cpp/io.hpp (native
    // standard library). Their prototypes are registered by init_stdlibs().
    // 注：io::OStream / io::IStream 已迁移至 lib/cpp/io.hpp（原生标准库），
    // 其原型由 init_stdlibs() 登记。


    // ========================================================
    // Constraints (contracts)
    //
    // Freeze all of a prototype's method signatures into a constraint (Spec
    // Chapter 1: a constraint is a "method list"; filling in a class name
    // automatically matches that class's full method signatures).
    // 把一个原型的全部方法签名固化为约束（文档第一章：
    // 约束即"方法清单"，填类名时自动匹配该类全部方法签名）。
    // ========================================================

    inline rt_basic::ClassContract contract_of(
        const rt_basic::ClsProtoPtr& proto
    ) {
        rt_basic::ClassContract contract;
        if (proto) {
            for (const auto& [name, method] : proto->get_methods()) {
                (void)name;
                contract.add_sign(method.get_sign());
            }
        }
        return contract;
    }


    // ========================================================
    // Initialize
    //
    // init_builtins(): the only public bootstrap entry, idempotent. Each
    // prototype first copies the reserved methods from std::Object
    // (:: ~ =: :=; Spec 2.3.1: inherited by default, no re-impl
    // needed), then supplements / overrides its own methods. std and io are
    // two independent collection packages, both finally injected into the
    // runtime environment stdRT.
    // init_builtins()：唯一公开引导入口，幂等。
    // 各原型先从 std::Object 拷贝继承保留方法
    // （:: ~ =: :=，文档 2.3.1：默认继承，无需重复实现），
    // 再补充 / 覆盖自身方法。std 与 io 是两个独立集合包，
    // 最终都注入运行环境 stdRT。
    // ========================================================

    inline void init_prototypes() {

        // ----------------------------------------------------
        // std::Object —— the root of all things
        // ----------------------------------------------------

        PT_stdObject = std::make_shared<rt_basic::ClsProto>();

        PT_stdObject->set_method("::", method_Object_construct());
        PT_stdObject->set_method("~", method_Object_destruct());
        PT_stdObject->set_method("=:", method_Object_publish());
        PT_stdObject->set_method(":=", method_Object_receive());
        PT_stdObject->set_method("=", method_Object_assign());

        // NOTE: the base Object prototype intentionally carries NO default
        // "#value" attribute. A bare Object has no scalar value, and the base
        // publish closure (value_publish_closure) already degrades to an empty
        // result when "#value" is absent. Seeding "#value" here would leak a
        // stray attribute into EVERY subclass (e.g. io::OStream, whose
        // acceptance test requires its attribute table to stay empty after a
        // write), so the zero-value seeds live only on the concrete scalar
        // prototypes further below.
        // 注意：基类 Object 原型刻意不携带缺省 "#value" 属性。裸 Object
        // 无标量值，基类的公布闭包（value_publish_closure）在缺失 "#value"
        // 时已优雅降级为空结果。若在此处播种 "#value"，会把它泄漏进每个
        // 子类（例如 io::OStream，其验收要求写入后属性表仍为空），故
        // 零值种子只放在下方具体标量原型上。

        stdPT.regcls("Object", PT_stdObject);


        // ----------------------------------------------------
        // std::Number —— number (unifies integer and float)
        // ----------------------------------------------------

        PT_stdNumber =
            std::make_shared<rt_basic::ClsProto>(PT_stdObject);

        PT_stdNumber->set_method("+", method_Number_add());
        PT_stdNumber->set_method("-", method_Number_sub());
        PT_stdNumber->set_method("*", method_Number_mul());
        PT_stdNumber->set_method("/", method_Number_div());
        PT_stdNumber->set_method("%", method_Number_mod());
        PT_stdNumber->set_method("<", method_Number_lt());
        PT_stdNumber->set_method(">", method_Number_gt());
        PT_stdNumber->set_method("<=", method_Number_le());
        PT_stdNumber->set_method(">=", method_Number_ge());
        PT_stdNumber->set_method("==", method_Number_eq());
        PT_stdNumber->set_method("!=", method_Number_ne());
        PT_stdNumber->set_method("to_string", method_Number_to_string());
        PT_stdNumber->set_method("repeat_", method_Number_repeat_());
        // "=:" overrides the base reserved method: signature precise to
        // std::Number. ":=" overrides the base reserved method: set_method
        // binds generically (the @! guard is removed — modifiers are checked
        // by the interpreter, the runtime does not guard). "=" assign method:
        // -> non-const contract (shares the := closure factory).
        // "=:" 覆盖基类保留方法：签名精确到 std::Number。
        // ":=" 覆盖基类保留方法：set_method 通用绑定
        //（@! 守卫已移除——修饰符交解释器核查，runtime 不守卫）。
        // "="  赋值方法：-> 非常数契约（与 := 同源闭包工厂）。
        PT_stdNumber->set_method("=:", method_Number_publish());
        PT_stdNumber->set_method(":=", method_Number_receive());
        PT_stdNumber->set_method("=", method_Number_assign());
        // Explicit constructor: -(std::Number(value)! x) initialises x via the
        // same negotiation the receive end uses (duck-typed, @-typed arg).
        // 显式构造函数：-(std::Number(value)! x) 经与接收端相同的协商初始化 x
        // （鸭子式，参数类型 @）。
        PT_stdNumber->set_method("::",
            native_method(scalar_receive_closure("num"),
                          make_sign("::", {{"value", "@"}}, {})));

        // Zero-value law: a fresh Number is 0. make_number() overwrites this
        // per-instance, so this only seeds the prototype.
        // 零值法则：新造 Number 缺省为 0。make_number() 按实例覆盖，
        // 故此处仅作原型种子。
        PT_stdNumber->set_attribute(VALUE_KEY, cap_number(0.0, true));

        stdPT.regcls("Number", PT_stdNumber);


        // ----------------------------------------------------
        // std::Boolean —— boolean (if_ / while_ control flow)
        // ----------------------------------------------------

        PT_stdBoolean =
            std::make_shared<rt_basic::ClsProto>(PT_stdObject);

        PT_stdBoolean->set_method("if_", method_Boolean_if_());
        PT_stdBoolean->set_method("while_", method_Boolean_while_());
        PT_stdBoolean->set_method("=:", method_Boolean_publish());
        PT_stdBoolean->set_method(":=", method_Boolean_receive());
        PT_stdBoolean->set_method("=", method_Boolean_assign());
        // Explicit constructor: -(std::Boolean(value)! x). 显式构造函数。
        PT_stdBoolean->set_method("::",
            native_method(scalar_receive_closure("bool"),
                          make_sign("::", {{"value", "@"}}, {})));

        // Zero-value law: a fresh Boolean is false. 零值法则：新造 Boolean 缺省 false。
        PT_stdBoolean->set_attribute(VALUE_KEY, cap_boolean(false));

        stdPT.regcls("Boolean", PT_stdBoolean);


        // ----------------------------------------------------
        // std::String —— immutable character sequence
        // ----------------------------------------------------

        PT_stdString =
            std::make_shared<rt_basic::ClsProto>(PT_stdObject);

        PT_stdString->set_method("+", method_String_add());
        PT_stdString->set_method("upper", method_String_upper());
        PT_stdString->set_method("lower", method_String_lower());
        PT_stdString->set_method("reverse", method_String_reverse());
        PT_stdString->set_method("length", method_String_length());
        PT_stdString->set_method("get", method_String_get());
        PT_stdString->set_method("contains", method_String_contains());
        PT_stdString->set_method("slice", method_String_slice());
        PT_stdString->set_method("=:", method_String_publish());
        PT_stdString->set_method(":=", method_String_receive());
        PT_stdString->set_method("=", method_String_assign());
        // Explicit constructor: -(std::String(value)! x). 显式构造函数。
        PT_stdString->set_method("::",
            native_method(scalar_receive_closure("str"),
                          make_sign("::", {{"value", "@"}}, {})));

        // Zero-value law: a fresh String is empty. 零值法则：新造 String 缺省为空串。
        PT_stdString->set_attribute(VALUE_KEY, cap_string(""));

        stdPT.regcls("String", PT_stdString);


        // ----------------------------------------------------
        // std::Array —— dynamic array (equivalent to std::vector)
        // ----------------------------------------------------

        PT_stdArray =
            std::make_shared<rt_basic::ClsProto>(PT_stdObject);

        PT_stdArray->set_method("push_back", method_Array_push_back());
        PT_stdArray->set_method("get", method_Array_get());
        PT_stdArray->set_method("size", method_Array_size());
        PT_stdArray->set_method("pop_back", method_Array_pop_back());
        PT_stdArray->set_method("remove", method_Array_remove());
        PT_stdArray->set_method("insert", method_Array_insert());
        PT_stdArray->set_method("clear", method_Array_clear());
        PT_stdArray->set_method("front", method_Array_front());
        PT_stdArray->set_method("back", method_Array_back());
        PT_stdArray->set_method("=:", method_Array_publish());
        PT_stdArray->set_method(":=", method_Array_receive());
        PT_stdArray->set_method("=", method_Array_assign());
        // Explicit constructor: -(std::Array(value)! x) — value may be a
        // std::Tuple / std::Array (populates elements) or a single element.
        // 显式构造函数：-(std::Array(value)! x) —— value 可为 std::Tuple /
        // std::Array（填充元素）或单个元素。
        PT_stdArray->set_method("::",
            native_method(sequence_receive_closure(),
                          make_sign("::", {{"value", "@"}}, {})));

        stdPT.regcls("Array", PT_stdArray);


        // ----------------------------------------------------
        // std::Dict —— dictionary
        // ----------------------------------------------------

        PT_stdDict = std::make_shared<rt_basic::ClsProto>(PT_stdObject);

        PT_stdDict->set_method("get", method_Dict_get());
        PT_stdDict->set_method("set", method_Dict_set());
        PT_stdDict->set_method("remove", method_Dict_remove());
        PT_stdDict->set_method("has", method_Dict_has());
        PT_stdDict->set_method("size", method_Dict_size());
        PT_stdDict->set_method("keys", method_Dict_keys());
        PT_stdDict->set_method("values", method_Dict_values());
        PT_stdDict->set_method("=:", method_Dict_publish());
        PT_stdDict->set_method(":=", method_Dict_receive());
        PT_stdDict->set_method("=", method_Dict_assign());
        // Explicit constructor: -(std::Dict(value)! x) — copies another dict.
        // 显式构造函数：-(std::Dict(value)! x) —— 拷贝另一个字典。
        PT_stdDict->set_method("::",
            native_method(dict_receive_closure(),
                          make_sign("::", {{"value", "@"}}, {})));

        stdPT.regcls("Dict", PT_stdDict);


        // ----------------------------------------------------
        // std::Tuple —— immutable value sequence
        // ----------------------------------------------------

        PT_stdTuple = std::make_shared<rt_basic::ClsProto>(PT_stdObject);

        PT_stdTuple->set_method("get", method_Tuple_get());
        PT_stdTuple->set_method("make", method_Tuple_make());
        PT_stdTuple->set_method("size", method_Tuple_size());
        PT_stdTuple->set_method("=:", method_Tuple_publish());
        PT_stdTuple->set_method(":=", method_Tuple_receive());
        PT_stdTuple->set_method("=", method_Tuple_assign());
        // Explicit constructor: -(std::Tuple(value)! x) — value may be a
        // std::Tuple (copies elements) or a single element.
        // 显式构造函数：-(std::Tuple(value)! x) —— value 可为 std::Tuple
        // （拷贝元素）或单个元素。
        PT_stdTuple->set_method("::",
            native_method(sequence_receive_closure(),
                          make_sign("::", {{"value", "@"}}, {})));

        stdPT.regcls("Tuple", PT_stdTuple);


        // ----------------------------------------------------
        // Inject the std package into the runtime
        // ----------------------------------------------------

        stdRT.add_protos(stdPT);
    }

    // Bootstrap entry: fill the std package and inject into stdRT
    // (idempotent). The C++-backed standard libraries (including io) are
    // brought up by init_stdlibs(), which init_builtins() also invokes.
    // 引导入口：填充 std 集合包并注入 stdRT（幂等）。C++ 底层标准库
    // （含 io）由 init_stdlibs() 拉起，init_builtins() 亦会调用之。
    // 供 main.cpp 与验收文件直接调用。

    // Forward declaration: defined later in this namespace; init_builtins()
    // invokes it to bring up the C++-backed standard libraries.
    // 前向声明：本命名空间后文定义；init_builtins() 调用它以拉起
    // C++ 底层标准库。
    inline void init_stdlibs();

    inline void init_builtins() {
        static bool ready = false;
        if (ready) {
            return;
        }
        ready = true;
        init_prototypes();
        // Wire the runtime signature enforcer and bring up the C++-backed
        // standard libraries (including io) so they are available even when
        // only init_builtins() is invoked (e.g. the runtime acceptance
        // suite, which does not call init_stdlibs()).
        // 接线运行时签名约束器，并拉起 C++ 底层标准库（含 io），
        // 使仅调用 init_builtins() 的场景（如 runtime 验收套件，未调用
        // init_stdlibs()）亦能使用这些库。
        rt_basic::g_sign_enforcer = enforce_sign;
        init_stdlibs();
    }

    // ========================================================
    // Standard-library registry (C++-backed libraries)
    //
    // A standard library that needs a native (C++) backend (e.g. file,
    // system) ships as a pair: a `lib/<name>.synl` interface and a
    // `lib/<name>.hpp` implementation. The .hpp self-registers here via
    // register_native_lib at static-init time, so the interpreter can run
    // the registered initializer when the program is loaded. Pure-Synth-OOP
    // libraries (lib/<name>.synl only) carry no native code and are simply
    // interpreted by the `&<name>;` import statement.
    // 标准库注册表（需要 C++ 底层实现的标准库）
    //
    // 需要原生（C++）底层的标准库（如 file、system）成对提供：
    // `lib/<name>.synl` 接口 + `lib/<name>.hpp` 实现。.hpp 在静态初始化时
    // 经 register_native_lib 自注册，使解释器在载入程序时可运行被注册的
    // 初始化器。纯 Synth-OOP 标准库（仅 lib/<name>.synl）不含原生代码，
    // 直接由 `&<name>;` import 语句解释执行。
    // ========================================================

    // name -> native initializer (run once at program start).
    // 库名 -> 原生初始化器（程序启动时运行一次）。
    inline std::unordered_map<std::string, void(*)()>& native_lib_registry() {
        static std::unordered_map<std::string, void(*)()> reg;
        return reg;
    }

    // Register a C++-backed standard library under `name`.
    // 把一个以 C++ 为底层的标准库按名登记。
    inline void register_native_lib(const std::string& name, void(*init)()) {
        native_lib_registry()[name] = init;
    }

    // Run every registered native-library initializer (idempotent in effect:
    // re-registering a prototype simply overwrites the previous one).
    // 运行全部已注册的原生库初始化器（效果幂等：重新登记原型只是覆盖）。
    inline void init_stdlibs() {
        for (auto& kv : native_lib_registry()) {
            kv.second();
        }
    }

    // Run the native initializer for a single library name (if registered).
    // 运行单个库名的原生初始化器（若已注册）。
    inline void init_native_lib(const std::string& name) {
        auto& reg = native_lib_registry();
        auto it = reg.find(name);
        if (it != reg.end()) {
            it->second();
        }
    }

    // Resolved path to the standard-library directory (set by the host from
    // the executable location before running a program). Defaults to "lib".
    // 标准库目录的解析路径（宿主在运行程序前依可执行文件位置设定）。
    // 缺省为 "lib"。
    inline std::string& stdlib_dir() {
        static std::string dir = "lib";
        return dir;
    }

} // namespace rt_builtin

#endif
