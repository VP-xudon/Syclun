// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// interpreter.hpp
//
// Synth OOP tree-walking interpreter.
// Synth OOP 树遍历解释器。
//
// This is the missing half of the implementation: the parser turns source
// into an AST, builtin.hpp implements the native objects, and this file
// walks the AST and actually executes it. It also fixes runtime.hpp's
// TODO:103 by registering g_user_behavior_executor so that user-defined
// behavior AST nodes can be invoked through Callable::call.
// 这是实现缺失的另一半：parser 把源码变成 AST，builtin.hpp 实现原生对象，
// 本文件遍历 AST 并真正执行它。同时它注册 g_user_behavior_executor
// 修好了 runtime.hpp 的 TODO:103，使用户定义的行为 AST 节点能经
// Callable::call 被调用。
//
// Execution model (per the language spec):
// 执行模型（依据语言文档）：
//   * The program's top level only defines types ($Class / #Contract /
//     &module). There is no global scope; the entry point is the `$Program`
//     class's `@::` (construct) behavior, which the interpreter runs
//     automatically on instantiation ("the compiler starts the program by
//     instantiating $Program; execution is its lifecycle").
//     程序顶层只定义类型（$Class / #Contract / &module）。没有全局作用域；
//     入口是 `$Program` 类的 `@::`（构造）行为，解释器在实例化时自动运行
//     （“编译器启动即实例化 $Program，执行即生命周期”）。
//   * A method body (behavior) executes with the object's attributes as its
//     member environment; locals and parameters live in a frame on top.
//     方法体（行为）以对象的属性表作为成员环境执行；局部变量与参数位于
//     其上的帧中。
//   * An inline behavior literal (e.g. an if_ branch) is a *closure*: it
//     captures the caller's local scope so the branch can read the caller's
//     variables (spec C.5).
//     内联行为字面量（如 if_ 分支）是*闭包*：捕获调用者的局部作用域，
//     使分支能读取调用者的变量（文档 C.5）。
//   * Flow `a << b` is executed as: publish b (b's "=:"), then receive into
//     a (a's ":="). `>>` is normalized to `<<` by the parser.
//     流语句 `a << b` 的执行为：公布 b（b 的 “=:”），再接收进 a（a 的 “:=”）。
//     `>>` 已由 parser 归一化为 `<<`。
// ============================================================

#ifndef INTERPRETER_HPP
#define INTERPRETER_HPP

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <limits>

#include "runtime.hpp"
#include "parser.hpp"
#include "builtin.hpp"
#include "../lib/cpp/std_libs.hpp"   // C++-backed standard libraries (self-registering)

namespace interp {

    // Pull in the namespaces we lean on so the code below stays readable.
    // 引入常用命名空间以保持下文可读。
    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    using runtime::RuntimeClassPtr;
    using runtime::RuntimeBehavior;
    using rt_basic::InstanceMap;
    using rt_basic::InstanceListPtr;
    using rt_basic::Callable;
    using rt_basic::CallableSign;
    using rt_basic::ClsProto;
    using rt_basic::ClsProtoPtr;
    using rt_basic::ClassContract;
    using parser::AstNode;
    using parser::AstNodePtr;

    namespace rb = rt_builtin;

    // ---- Interpreter call frame / 解释器调用帧 ----
    // A frame carries the locals (variables + bound parameters) for one
    // behavior invocation, plus a pointer to the outer environment (the
    // object's members for a method, or the captured scope for a closure).
    // 一个帧承载单次行为调用的局部变量（变量 + 已绑定参数），
    // 外加一个指向外部环境（方法的对象成员 / 闭包的捕获作用域）的指针。
    struct Frame {
        InstanceMap locals;                       // local variables / 局部变量
        std::unordered_map<std::string, bool>
            constFlag;                            // const locals / 常数局部标记
        // Per-variable constraint: name -> constraint (contract / class) name.
        // A variable carrying a constraint must always hold a value that
        // satisfies it; checked on declaration, initialization, and assignment.
        // 每变量约束：变量名 -> 约束（约束 / 类名）。带约束的变量必须始终
        // 持有满足该约束的值；在声明、初始化与赋值时校验。
        std::unordered_map<std::string, std::string> constraints;
        InstanceMap* outer = nullptr;             // members or captured scope
        RuntimeClassPtr self = nullptr;           // current instance (for
                                                 // self-calls / `self` keyword)
                                                 // 当前实例（用于自调用 / `self` 关键字）
        std::string mode = "->";                  // behavior mode / 行为模式
    };

    // Recursion depth guard: a configurable ceiling on nested behavior calls.
    // Exceeding it force-interrupts (e.g. an infinite `self.` recursion) with a
    // clear RecursionLimitException instead of a stack overflow / hang.
    // 递归深度护栏：嵌套行为调用的可配置上限。超出即以清晰的
    // RecursionLimitException 强制中断（例如无限的 `self.` 自递归），
    // 而非栈溢出 / 卡死。
    inline int g_recursion_depth = 0;
    // Recursion ceiling. The native (C++) stack is enlarged at link time
    // (-Wl,--stack,33554432 -> 32 MB) so that a deep but *legitimate* recursion
    // can run; this guard catches only *runaway* self-recursion well before
    // that 32 MB budget is exhausted (each Synth-level self-call consumes
    // several C++ frames).
    // 递归上限。原生（C++）栈在链接期被放大（-Wl,--stack,33554432 → 32MB），
    // 使“深但合法”的递归得以运行；本护栏只在失控自递归耗尽 32MB 预算前拦下
    // 它（每层 Synth 自递归消耗多个 C++ 帧）。
    inline const int RECURSION_LIMIT = 1000;

    // Registered contracts, keyed by name (filled while defining #Contract).
    // 已注册的约束，按名索引（定义 #Contract 时填充）。
    inline std::unordered_map<std::string, ClassContract>& contracts() {
        static std::unordered_map<std::string, ClassContract> table;
        return table;
    }

    // ---- forward declarations / 前置声明 ----
    InstanceListPtr exec_behavior(
        InstanceMap& env,
        const InstanceListPtr& paras,
        AstNodePtr behavior,
        const CallableSign& sign,
        RuntimeClassPtr self = nullptr
    );
    RuntimeObjectPtr eval_expr(Frame& f, AstNodePtr node);
    InstanceListPtr eval_expr_list(Frame& f, AstNodePtr node);
    void exec_block(Frame& f, AstNodePtr block);
    void exec_stmt(Frame& f, AstNodePtr stmt);
    void exec_vardef(Frame& f, AstNodePtr node);
    void call_constructor(Frame& f, RuntimeObjectPtr obj, AstNodePtr argsNode);
    RuntimeObjectPtr resolve(Frame& f, const std::string& name);
    InstanceListPtr invoke(
        RuntimeObjectPtr object, const std::string& name, InstanceListPtr args
    );
    void flow_into(Frame& f, RuntimeObjectPtr receiver, RuntimeObjectPtr sender);
    RuntimeObjectPtr exec_flow(Frame& f, AstNodePtr flowNode);
    RuntimeObjectPtr make_closure(AstNodePtr behavior, Frame& f);
    RuntimeObjectPtr instantiate(const std::string& typeName);
    void define_class(AstNodePtr classdef);
    void define_contract(AstNodePtr contractdef);
    void import_library(const std::string& name);

    // ========================================================
    // Error reporting / 错误上报
    // ========================================================

    // Raise a runtime / semantic error. Thrower exits the process, so this
    // effectively never returns to the caller (the attribute is omitted on
    // purpose: Thrower.throwE is not guaranteed [[noreturn]] in this TU, and
    // marking it so would trigger a spurious "function does return" warning).
    // 抛出运行时 / 语义错误。Thrower 会直接退出进程，
    // 故实际永不返回（此处刻意不标 [[noreturn]]：本编译单元中
    // Thrower.throwE 未必被标为 noreturn，强标会触发“函数仍会返回”的误警）。
    inline void interp_error(
        const std::string& name, const std::string& message
    ) {
        Thrower.throwE(name, message);
    }

    // ========================================================
    // Small helpers / 小工具
    // ========================================================

    // Map a behavior-mode arrow to its Callable state.
    // 把行为模式箭头映射到 Callable 状态。
    inline rt_basic::BehavStateOBJ state_from_mode(
        const std::string& m
    ) {
        if (m == "=>") return rt_basic::BehavStateOBJ::STRICT;
        if (m == "~>") return rt_basic::BehavStateOBJ::CONST;
        return rt_basic::BehavStateOBJ::NORMAL;
    }

    // Build a CallableSign from a behavior / signature node (params + outputs).
    // 从行为 / 签名节点（参数 + 输出）构造 CallableSign。
    //
    // IMPORTANT — AST shape (see parser.hpp parse_behavior / parse_sign_core):
    //   行为 AST 形态（见 parser.hpp 的 parse_behavior / parse_sign_core）：
    //     * A `behavior` node wraps a `sign` node as its FIRST child and the
    //       body `block` as the SECOND:  behavior = [sign, block].
    //       behavior 节点把 sign 节点包为首个子节点、把函数体 block 包为第二
    //       个子节点：behavior = [sign, block]。
    //     * A `sign` node carries the mode in `value` and the parameter /
    //       output containers in `kids[0]` / `kids[1]`:
    //       sign = {value=mode, kids=[params, outputs]}.
    //       sign 节点把模式存于 value、把参数 / 输出容器存于 kids[0]/[1]。
    //     * A methoddef's right-hand side is a `behavior` node; a contract
    //       signitem's child is a bare `sign` node. This helper unwraps the
    //       `behavior` wrapper (if any) so it works for both call sites.
    //       methoddef 右侧是 behavior 节点；约束 signitem 的子节点是裸
    //       sign 节点。本函数会按需剥去 behavior 外壳，两处调用通用。
    inline CallableSign build_sign(AstNodePtr node, const std::string& name) {
        if (!node) {
            interp_error("InterException", "build_sign on a null node");
        }
        // Unwrap a `behavior` wrapper to reach the inner `sign` node.
        // 剥去 behavior 外壳，取到内部 sign 节点。
        AstNodePtr signNode =
            (node->kind == "behavior") ? node->kids[0] : node;
        if (!signNode || signNode->kids.size() < 2) {
            // No parameters / outputs declared: an empty signature.
            // 未声明参数 / 输出：空签名。
            return rb::make_sign(name, {}, {});
        }
        std::vector<std::pair<std::string, std::string>> in, out;
        for (auto& p : signNode->kids[0]->kids) {
            // mode "type"      -> the type name is in p->value (e.g. std::Number)
            // mode "none"      -> untyped, empty expected type (accept anything)
            // mode "constraint"-> a #Constraint name in p->value (e.g. Addable);
            //                     recorded for completeness but NOT runtime-
            //                     checked (the §9.8 roll-call is compile-time
            //                     and not yet implemented for parameters)
            // mode "behavior"  -> a behavior/@ qualifier; no concrete type
            // mode "behaviorsign" -> a behavior with a signature qualifier
            // mode "constraint" 把约束名（p->value，如 Addable）记入签名，
            // 但运行期不据此核查（§9.8 点名单为编译期特性，尚未在参数级实现）。
            std::string type = (p->mode == "type")        ? p->value
                              : (p->mode == "none")        ? ""
                              : (p->mode == "constraint")  ? p->value
                              : p->mode;
            in.emplace_back(p->name, type);
        }
        for (auto& o : signNode->kids[1]->kids) {
            std::string type = (o->mode == "type") ? o->value
                                  : (o->mode == "none") ? "" : o->mode;
            out.emplace_back(o->name, type);
        }
        return rb::make_sign(name, in, out);
    }

    // ========================================================
    // Object instantiation / 对象实例化
    // ========================================================

    // Deep-clone a single object so that instances do not share member state.
    // 深拷贝单个对象，使各实例不共享成员状态。
    inline RuntimeObjectPtr clone_obj(RuntimeObjectPtr obj);

    // Deep-clone every member attribute of a class instance so that two
    // instances of the same class keep independent member objects
    // (the prototype's zero-value members are otherwise shared by pointer).
    // 深拷贝类实例的每个成员属性，使同类两实例各自持有独立的成员对象
    // （否则原型中的零值成员会按指针被共享）。
    inline void deep_clone_members(RuntimeClassPtr cls) {
        if (!cls) return;
        std::vector<std::pair<std::string, RuntimeObjectPtr>> entries;
        for (auto& kv : cls->get_attributes()) {
            entries.emplace_back(kv.first, clone_obj(kv.second));
        }
        for (auto& kv : entries) {
            cls->set_attribute(kv.first, kv.second);
        }
    }

    inline RuntimeObjectPtr clone_obj(RuntimeObjectPtr obj) {
        auto src = std::dynamic_pointer_cast<RuntimeClass>(obj);
        if (!src) return obj;                       // primitive: share as-is
        auto proto = src->get_prototype();
        if (!proto) return obj;
        auto copy = std::make_shared<RuntimeClass>(proto);
        // Copy the scalar value capsule via publish / receive.
        // 经公布 / 接收拷贝标量值胶囊。
        auto published = invoke(src, "=:", rb::empty_result());
        invoke(copy, ":=", published);
        deep_clone_members(copy);                   // recurse into members
        return copy;
    }

    // Instantiate a type and run its `@::` construct behavior if present.
    // 实例化一个类型，并在存在 `@::` 构造行为时运行它。
    inline RuntimeObjectPtr instantiate(const std::string& typeName) {
        auto instance = ::stdRT.make(typeName);
        if (!instance) {
            interp_error(
                "RuntimeException",
                "Cannot instantiate unknown type '" + typeName + "'."
            );
        }
        auto cls = std::dynamic_pointer_cast<RuntimeClass>(instance);
        if (cls) {
            deep_clone_members(cls);
            auto methods = cls->get_methods();
            auto found = methods.find("::");
            if (found != methods.end()) {
                // Run the constructor with the (cloned) member environment.
                // The instance is passed as `self` so the constructor can use
                // self-method calls / the `self` keyword (Bug B fix).
                // 以（已克隆的）成员环境运行构造函数，并把实例作为 `self`
                // 传入，使构造函数也能自调用 / 使用 `self` 关键字（修复 Bug B）。
                diag::call_stack().push_back(
                    {"in '@::' (constructor of '"
                        + (cls->selfname.empty() ? "?" : cls->selfname)
                        + "')",
                     diag::source_file(),
                     diag::cur_locus().line,
                     diag::cur_locus().col});
                found->second.call(cls->get_attributes(), rb::empty_result(), cls);
                diag::call_stack().pop_back();
            }
        }
        return instance;
    }

    // Register a finished prototype under a name in the runtime.
    // 把构建完成的原型按名登记进运行时。
    inline void register_proto(const std::string& name, ClsProtoPtr proto) {
        runtime::Prototypes p;
        p.regcls(name, proto);
        ::stdRT.add_protos(p);
    }

    // ========================================================
    // Method dispatch / 方法派发
    // ========================================================

    // Unified method-call entry, mirroring the acceptance tests' `invoke`.
    // 统一方法调用入口，与验收测试的 invoke 一致。
    // Materialize a scalar value held inside a universal `std::Object` into a
    // concrete typed instance, so the Object can act as that type for method
    // dispatch. Returns nullptr when the holder does not carry a scalar value
    // (e.g. an empty Object, or a composite held elsewhere).
    // 把通用 std::Object 内部持有的标量值“实体化”为具体类型实例，
    // 使该 Object 能作为该类型参与方法派发。若持有者并未携带标量值
    //（如空 Object，或复合值存于别处）则返回 nullptr。
    inline runtime::RuntimeClassPtr materialize_scalar(
        const RuntimeObjectPtr& holder
    ) {
        auto* attrs = rb::attributes_of(holder);
        if (!attrs) return nullptr;
        auto found = attrs->find(rb::VALUE_KEY);
        if (found == attrs->end() || !found->second) return nullptr;
        const std::string tag = rb::capsule_tag(found->second);
        if (tag == "num") {
            auto v = rb::capsule_number(found->second);
            auto i = rb::capsule_is_integer(found->second);
            if (!v) return nullptr;
            return std::dynamic_pointer_cast<runtime::RuntimeClass>(
                rb::make_number(*v, i.value_or(false)));
        }
        if (tag == "str") {
            auto s = rb::capsule_string(found->second);
            if (!s) return nullptr;
            return std::dynamic_pointer_cast<runtime::RuntimeClass>(
                rb::make_string(*s));
        }
        if (tag == "bool") {
            auto b = rb::capsule_boolean(found->second);
            if (!b) return nullptr;
            return std::dynamic_pointer_cast<runtime::RuntimeClass>(
                rb::make_boolean(*b));
        }
        return nullptr;
    }

    // Verify that `value` satisfies the named constraint (a #Contract or a
    // class). If the constraint is unknown, the check is skipped (a contract
    // the program never defined cannot be enforced). A value that is a scalar
    // (e.g. a std::Number capsule) is validated via its concrete prototype so
    // that user contracts such as #Addable still apply to it.
    // 校验 value 是否满足命名约束（#Contract 或类）。若约束未知则跳过
    //（程序从未定义的约束无法落实）。标量值（如 std::Number 胶囊）按
    // 其具体原型校验，使 #Addable 之类的用户约束也能作用于它。
    inline void check_constraint(
        const std::string& constraintName, RuntimeObjectPtr value
    ) {
        if (constraintName.empty()) return;
        auto& cs = contracts();
        if (!cs.count(constraintName)) {
            // Not a #Contract. Fall back to a class-name constraint: the value
            // must be an instance of that class (exact prototype match, with
            // namespace tolerance). A class used as a type constraint is a
            // first-class feature (e.g. `-(io::OStream[io::OStream] out)`), so
            // it must be enforced at runtime just like a #Contract.
            // 非 #Contract：回退到「类名约束」——值须为该类的实例（精确原型
            // 匹配，命名空间容错）。以类名作约束（如
            // `-(io::OStream[io::OStream] out)`）是第一等特性，须与 #Contract
            // 同样在运行期落实。
            auto proto = ::stdRT.getcls(constraintName);
            if (!proto) {
                // Truly unknown (neither #Contract nor a registered class):
                // cannot verify; never falsely reject.
                // 既非 #Contract 也非已注册类：运行期无法核查，绝不误拒。
                return;
            }
            // Resolve the value's concrete type key and compare prototypes
            // (namespace-tolerant exact match). The runtime keeps no inheritance
            // chain, so subclasses are not walked; an exact prototype match is
            // the supported and sufficient check here.
            // 解析值的具体类型键并比对原型（命名空间容错的精确匹配）。运行期
            // 不保留继承链，故不做子类遍历；精确原型匹配即已足够。
            std::string vkey = rb::arg_type_key(value);
            // rfind("::") finds the last two-character separator; find_last_of()
            // is a character SET and would match a lone ':'.
            // rfind("::") 定位最后一个双字符分隔符；find_last_of 是字符集，
            // 会匹配单个 ':'。
            auto unqual = [](const std::string& s) {
                auto p = s.rfind("::");
                return p == std::string::npos ? s : s.substr(p + 2);
            };
            if (unqual(vkey) != unqual(proto->name)) {
                interp_error(
                    "ConstraintException",
                    "value of type '" + (vkey.empty() ? "<unknown>" : vkey)
                        + "' does not satisfy class constraint '"
                        + constraintName + "'"
                );
            }
            return;
        }
        RuntimeClassPtr cls =
            std::dynamic_pointer_cast<RuntimeClass>(value);
        if (!cls) {
            // Scalar / primitive value: validate against its concrete prototype.
            // 标量 / 原始值：按其具体原型校验。
            std::string tkey = rb::arg_type_key(value);
            auto proto = ::stdRT.getcls(tkey);
            if (!proto) {
                interp_error(
                    "ConstraintException",
                    "value of type '" + (tkey.empty() ? "<unknown>" : tkey)
                        + "' is not an object and cannot satisfy constraint '"
                        + constraintName + "'"
                );
            }
            cls = std::make_shared<RuntimeClass>(proto);
        }
        if (!cs[constraintName].validate(cls)) {
            interp_error(
                "ConstraintException",
                "value does not satisfy constraint '" + constraintName + "'"
            );
        }
    }

    inline InstanceListPtr invoke(
        RuntimeObjectPtr object, const std::string& name, InstanceListPtr args
    ) {
        auto cls = std::dynamic_pointer_cast<RuntimeClass>(object);
        if (!cls) {
            return rb::list_of(
                {rb::native_error("method call on a non-object receiver")}
            );
        }
        // Duck-typed dispatch for the universal receiver: a generic
        // `std::Object` that holds a scalar value behaves as that scalar for
        // method calls. This is the root fix for type-tag degradation — a
        // value threaded through an untyped behavior output parameter (the
        // loop state of `repeat_` / `while_`, an `if_` branch result, …)
        // kept its scalar value but lost its concrete type, so methods such
        // as `+` could no longer be called on it. The Object now transparently
        // stands in for the value it carries. Lifecycle / flow methods
        // (:: ~ =: := =) belong to Object itself and are never delegated.
        // 通用接收方（万物之源）的鸭子式派发：持有标量值的通用 std::Object
        // 在方法调用时表现为该标量类型。这是类型标签退化的根源修复——
        // 经无类型行为输出参数（repeat_/while_ 的循环状态、if_ 分支结果…）
        // 穿线的数值保留了标量值却丢了具体类型，导致无法对其调用 + 等方法。
        // 现 Object 透明地代表其持有的值。生命周期 / 流方法（:: ~ =: := =）
        // 属于 Object 自身，绝不委派。
        auto is_lifecycle = [](const std::string& n) {
            return n == "::" || n == "~" || n == "=:" || n == "=" || n == ":=";
        };
        if (cls->get_prototype() && cls->get_prototype()->name == "Object"
                && !is_lifecycle(name)) {
            if (auto delegate = materialize_scalar(cls)) {
                auto res = invoke(delegate, name, args);
                // Propagate any in-place mutation of the held value back to the
                // generic Object (a behavior that mutates its threaded state
                // via a setter). Pure operations leave the value unchanged, so
                // this is a safe no-op in the common case.
                // 把持有值的就地修改写回通用 Object（对穿线状态做设值的行为）。
                // 纯操作不改动值，故常见情形为安全空操作。
                if (auto* dattrs = rb::attributes_of(delegate)) {
                    auto f = dattrs->find(rb::VALUE_KEY);
                    if (f != dattrs->end()) {
                        cls->set_attribute(rb::VALUE_KEY, f->second);
                    }
                }
                return res;
            }
        }
        return cls->call_method(name, args);
    }

    // ========================================================
    // Name resolution / 名字解析
    // ========================================================

    // Resolve a variable name to an object, honoring the scope chain and the
    // behavior mode (a `=>` strict behavior may not read the outer scope).
    // 把变量名解析为对象，遵循作用域链与行为模式
    // （`=>` 严格行为不得读取外部环境）。
    inline RuntimeObjectPtr resolve(Frame& f, const std::string& name) {
        if (name == "self") {
            // `self` refers to the instance whose method is currently running.
            // `self.method(args)` resolves the receiver here; bare `method()`
            // calls go through the `selfcall` expression node.
            // `self` 指向当前正在运行其方法的实例。`self.method(args)` 于此
            // 解析接收方；裸 `method()` 调用则走 `selfcall` 表达式节点。
            if (!f.self) {
                interp_error(
                    "InterException",
                    "'self' is not available outside a method body"
                );
            }
            return f.self;
        }
        if (name == "_") {
            interp_error("InterException", "placeholder '_' used as a value");
        }
        if (f.locals.count(name)) {
            return f.locals[name];
        }
        if (f.outer && f.outer->count(name)) {
            if (f.mode == "=>") {
                interp_error(
                    "ModeException",
                    "strict behavior ('=>') cannot access outer variable '"
                        + name + "'"
                );
            }
            return (*f.outer)[name];
        }
        auto global = ::stdRT.getobj(name);
        if (global) {
            return global;
        }
        interp_error("InterException", "undefined variable '" + name + "'");
        return nullptr;          // unreachable: interp_error exits the process
    }

    // ========================================================
    // Flow execution / 流语句执行
    // ========================================================

    // Perform `receiver << sender`: publish the sender, receive into the
    // receiver. Both spellings share this path (the parser normalized `>>`).
    // 执行 `receiver << sender`：公布发送方，接收进接收方。两种写法共用
    // 此通路（parser 已将 `>>` 归一化）。
    inline void flow_into(
        Frame& f, RuntimeObjectPtr receiver, RuntimeObjectPtr sender
    ) {
        if (!receiver || !sender) {
            interp_error("InterException", "flow with a null endpoint");
        }
        auto published = invoke(sender, "=:", rb::empty_result());
        invoke(receiver, ":=", published);
    }

    // Rebind a user method on a runtime object *in place*. This is the single
    // shared implementation behind both `obj.method.=(beh)` and
    // `obj.method << beh`. A method is just a member variable holding a
    // behavior, so assigning a behavior to it simply swaps the callable — no
    // special case. Const methods (@!inc) refuse to be rebound; a non-behavior
    // RHS is rejected. The caller must already have confirmed `field` names an
    // existing method, otherwise this rebinds nothing and returns normally.
    // 就地重绑对象的用户方法（共用实现：`obj.method.=(beh)` 与
    // `obj.method << beh`）。方法即持有行为的成员变量，对其赋值即替换
    // callable——无需特判。常数方法（@!inc）拒绝重绑；非行为右值被拒。调用方
    // 须先确认 field 确为已有方法，否则本函数不做事、正常返回。
    inline void rebind_method(
        RuntimeClass* cls, const std::string& field, AstNodePtr behAST
    ) {
        auto mit = cls->get_methods().find(field);
        if (mit == cls->get_methods().end()) return;
        if (mit->second.get_attr().first) {
            interp_error("ConstException",
                "cannot reassign const method '" + field
                + "' (declare it without '!' to make it mutable)");
        }
        if (!behAST) {
            interp_error("InterException",
                "method '" + field + "' can only be rebound to a behavior");
        }
        CallableSign sign = build_sign(behAST, field);
        Callable c(behAST, sign, state_from_mode(behAST->value), {false, false});
        cls->set_method(field, c);
        cls->methods_dirty = true;
    }

    // Execute a flow AST node, returning the receiver object (so that nested
    // flows such as `out << (-(T x) << v)` evaluate to the inner receiver).
    // 执行流 AST 节点，返回接收方对象（使 `out << (-(T x) << v)` 这类嵌套
    // 流求值得到内层接收方）。
    inline RuntimeObjectPtr exec_flow(Frame& f, AstNodePtr flowNode) {
        auto senderObj = eval_expr(f, flowNode->kids[1]);

        AstNodePtr recvNode = flowNode->kids[0];
        RuntimeObjectPtr receiverObj = nullptr;
        bool isOuter = false;

        if (recvNode->kind == "name") {
            std::string name = recvNode->value;
            if (f.locals.count(name)) {
                receiverObj = f.locals[name];
                // Reassigning a const local is forbidden (spec D.6).
                // 重赋值常数局部变量是被禁止的（文档 D.6）。
                if (f.constFlag.count(name) && f.constFlag[name]) {
                    interp_error(
                        "ConstException",
                        "cannot reassign const variable '" + name
                            + "' (declare it without '!' or initialize it "
                              "inline via a constructor)"
                    );
                }
                // Variable constraint: a constrained local must keep holding a
                // value that satisfies its constraint after the assignment.
                // 变量约束：带约束的局部变量在赋值后仍须持有满足该约束的值。
                if (f.constraints.count(name)
                        && !f.constraints[name].empty()) {
                    // A flow into an OBJECT receiver calls the object's `:=`
                    // method (stream / receive) and never rebinds the variable
                    // to a new object, so the variable's held type is unchanged
                    // and its constraint stays satisfied - we must NOT check the
                    // streamed value, or legitimate streaming into a constrained
                    // object (e.g. `out << 3`) would be falsely rejected.
                    // Scalars (Number / String / Boolean) do change their value
                    // via `:=`, so the check still applies to them.
                    // 对象接收方的流调用对象的 `:=` 方法（流出 / 接收），从不
                    // 把变量重绑到新对象，故变量所持有类型不变、约束仍满足——
                    // 不可核查被流出的值，否则向受约束对象的合法流出（如
                    // `out << 3`）会被误拒。标量经 `:=` 改变自身值，故仍须核查。
                    std::string rkey = rb::arg_type_key(receiverObj);
                    bool scalar = (rkey == "std::Number" || rkey == "std::String"
                                || rkey == "std::Boolean");
                    if (scalar) {
                        check_constraint(f.constraints[name], senderObj);
                    }
                }
            } else if (f.outer && f.outer->count(name)) {
                receiverObj = (*f.outer)[name];
                isOuter = true;
            } else {
                interp_error("InterException", "undefined variable '" + name + "'");
            }
        } else if (recvNode->kind == "inst") {
            // Inline declaration used as a flow receiver: a const inline
            // declaration must NOT be (re)assigned by a flow statement.
            // 用作流接收方的行内声明：常数行内声明不得被流语句（重新）赋值。
            if (recvNode->isConst) {
                interp_error(
                    "ConstException",
                    "cannot initialize const variable '" + recvNode->name
                        + "' through a flow statement; use a constructor, e.g. "
                          "-(std::Number(0)! " + recvNode->name + ")"
                );
            }
            // Inline-declaration constraint: if the declared variable is
            // constrained (e.g. `-(std::Object[Addable] x)`), the incoming
            // value must satisfy it. 行内声明约束：若声明带约束（如
            // `-(std::Object[Addable] x)`），流入的值须满足之。
            if (!recvNode->constraint.empty()) {
                check_constraint(recvNode->constraint, senderObj);
            }
            receiverObj = eval_expr(f, recvNode);   // declares + binds (non-const)
        } else if (recvNode->kind == "access") {
            // Symmetric flow form of method rebinding: `obj.method << beh`.
            // If the field names an existing user method on the owner, rebind
            // it in place (the behavior literal on the right becomes the new
            // callable). Otherwise this is a normal attribute flow and we fall
            // through to the usual access evaluation below.
            // 方法重绑定的对称流形式：`obj.method << beh`。若 field 确为所有者
            // 上的已有用户方法，则就地重绑（右侧行为字面量成为新 callable）；
            // 否则视作普通属性流，落到下方常规 access 求值。
            auto owner = eval_expr(f, recvNode->kids[0]);
            const std::string& field = recvNode->value;
            auto cls = std::dynamic_pointer_cast<RuntimeClass>(owner);
            if (cls) {
                auto mit = cls->get_methods().find(field);
                if (mit != cls->get_methods().end()) {
                    // The flow's right side is the behavior literal; read its
                    // AST directly. A RuntimeBehavior produced by make_closure
                    // keeps a lambda, not the AST, so the node must be taken
                    // before evaluation (the `.=` form does the same).
                    // 流的右侧即行为字面量，直接读取其 AST。make_closure 产出的
                    // RuntimeBehavior 保存的是 lambda 而非 AST，故须在求值前
                    // 取节点（`.=` 形式同理）。
                    AstNodePtr behAST = nullptr;
                    AstNodePtr senderNode = flowNode->kids[1];
                    if (senderNode->kind == "behavior") {
                        behAST = senderNode;
                    } else {
                        auto rbptr = std::dynamic_pointer_cast<RuntimeBehavior>(
                            senderObj);
                        if (rbptr) behAST = rbptr->get_astn();
                    }
                    rebind_method(cls.get(), field, behAST);
                    return cls;
                }
            }
            receiverObj = eval_expr(f, recvNode);
        } else if (recvNode->kind == "tuple") {
            // Tuple destructuring as a flow receiver (spec 5.4.3):
            //   (-(a), -(b)) << self.pair();
            // The sender is evaluated as a *value list* (a `call` returning
            // multiple outputs yields the whole list, not just its first
            // element) and each non-placeholder element of the receiver is
            // bound positionally, mirroring exec_vardef's multi-declaration
            // path. A single std::Tuple value on the right expands to its
            // elements so `(-(a), -(b)) << self.tuple()` also works.
            // 元组解构作为流接收方（文档 5.4.3）：把发送方求值为*值列表*
            // （返回多输出的 `call` 给出整个列表而非仅首项），接收方各非占位
            // 元素按位置绑定，复用 exec_vardef 多声明路径。右侧单个 std::Tuple
            // 值展开为其元素，故 `(-(a), -(b)) << self.tuple()` 同样生效。
            bool anyOuter = false;
            auto initList = eval_expr_list(f, flowNode->kids[1]);
            if (initList->size() == 1) {
                auto& only = (*initList)[0];
                if (auto tup =
                        std::dynamic_pointer_cast<RuntimeClass>(only)) {
                    if (tup->get_prototype()
                            && tup->get_prototype()->name == "Tuple") {
                        auto expanded =
                            std::make_shared<
                                std::vector<RuntimeObjectPtr>>();
                        auto* attrs = rb::attributes_of(only);
                        std::size_t n = rb::container_size(*attrs);
                        for (std::size_t i = 0; i < n; ++i) {
                            auto fnd = attrs->find(rb::elem_key(i));
                            if (fnd != attrs->end()) {
                                expanded->push_back(fnd->second);
                            }
                        }
                        initList = expanded;
                    }
                }
            }
            std::size_t idx = 0;
            for (auto& elem : recvNode->kids) {
                if (elem->isPlaceholder) { ++idx; continue; }
                RuntimeObjectPtr elemReceiver = nullptr;
                std::string elemName;
                if (elem->kind == "name") {
                    elemName = elem->value;
                    if (f.locals.count(elemName)) {
                        elemReceiver = f.locals[elemName];
                        // Reassigning a const local is forbidden (spec D.6).
                        // 重赋值常数局部变量是被禁止的（文档 D.6）。
                        if (f.constFlag.count(elemName)
                                && f.constFlag[elemName]) {
                            interp_error(
                                "ConstException",
                                "cannot reassign const variable '"
                                    + elemName
                                    + "' (declare it without '!' or "
                                      "initialize it inline via a constructor)"
                            );
                        }
                        // Variable constraint re-check after assignment.
                        // 赋值后再次校验变量约束。
                        if (f.constraints.count(elemName)
                                && !f.constraints[elemName].empty()) {
                            check_constraint(
                                f.constraints[elemName], f.locals[elemName]);
                        }
                    } else if (f.outer && f.outer->count(elemName)) {
                        elemReceiver = (*f.outer)[elemName];
                        anyOuter = true;
                    } else {
                        interp_error(
                            "InterException",
                            "undefined variable '" + elemName + "'");
                    }
                } else if (elem->kind == "inst") {
                    // Inline declaration used as a flow receiver: a const
                    // inline declaration must NOT be (re)assigned by a flow.
                    // 用作流接收方的行内声明：常数行内声明不得被流（重新）赋值。
                    elemName = elem->name;
                    if (elem->isConst) {
                        interp_error(
                            "ConstException",
                            "cannot initialize const variable '" + elemName
                                + "' through a flow statement; use a "
                                  "constructor, e.g. -(std::Number(0)! "
                                + elemName + ")");
                    }
                    elemReceiver =
                        eval_expr(f, elem);   // declares + binds (non-const)
                } else {
                    interp_error(
                        "InterException",
                        "tuple destructuring target must be a variable name "
                        "or inline declaration, not a '" + elem->kind + "'");
                }
                if (idx < initList->size()) {
                    flow_into(f, elemReceiver, (*initList)[idx]);
                    // Re-check inline-declaration constraint after binding.
                    // 绑定后再次校验行内声明约束。
                    if (elem->kind == "inst" && !elem->constraint.empty()) {
                        check_constraint(elem->constraint, elemReceiver);
                    }
                } else {
                    interp_error(
                        "InterException",
                        "too few values to destructure into '"
                            + (elem->kind == "name" ? elem->value : elem->name)
                            + "'");
                }
                ++idx;
            }
            // A `~>` const behavior may read but not write the outer scope
            // (spec D.4). Tuple destructuring into an outer member counts as a
            // write and is therefore forbidden there too.
            // `~>` 常数行为可读但不可写外部环境（文档 D.4）。元组解构到外层成员
            // 视作写操作，故同样禁止。
            if (anyOuter && f.mode == "~>") {
                interp_error(
                    "ModeException",
                    "const behavior ('~>') cannot modify outer member "
                    "through tuple destructuring");
            }
            return nullptr;   // a tuple has no single receiver object
        } else {
            // The left side of a flow is not a variable (it is an expression
            // result / rvalue such as `1.+(1)` or a method call). You cannot
            // assign into a non-variable, so this is an error.
            // 流的左侧不是变量（而是表达式结果 / 右值，如 `1.+(1)` 或方法调用）。
            // 不能对“非变量”赋值，故报错。
            interp_error(
                "InterException",
                "flow target must be a variable name (e.g. `x << value`), "
                "not an expression result (kind '" + recvNode->kind
                    + "') - a constant / rvalue cannot be the left side of '<<'"
            );
        }

        // A `~>` const behavior may read but not write the outer scope
        // (spec D.4).
        // `~>` 常数行为可读但不可写外部环境（文档 D.4）。
        if (isOuter && f.mode == "~>") {
            std::string name = (recvNode->kind == "name") ? recvNode->value : "?";
            interp_error(
                "ModeException",
                "const behavior ('~>') cannot modify outer member '"
                    + name + "'"
            );
        }

        flow_into(f, receiverObj, senderObj);
        return receiverObj;
    }

    // ========================================================
    // Expression evaluation / 表达式求值
    // ========================================================

    // Evaluate an argument: a behavior literal becomes a closure; anything
    // else becomes a value object.
    // 求一个实参：行为字面量变为闭包，其余变为值对象。
    inline RuntimeObjectPtr eval_arg(Frame& f, AstNodePtr argNode) {
        if (argNode->kind == "behavior") {
            return make_closure(argNode, f);
        }
        return eval_expr(f, argNode);
    }

    // Evaluate an expression to a single object.
    // 把一个表达式求值为单个对象。
    inline RuntimeObjectPtr eval_expr(Frame& f, AstNodePtr node) {
        if (!node) {
            interp_error("InterException", "null expression node");
        }
        // Record the current evaluation locus for the diagnostic reporter.
        // 记录当前求值位置，供诊断上报器使用。
        diag::set_locus(diag::source_file(), node->line, node->col);
        const std::string& k = node->kind;

        if (k == "number") {
            // Integer vs float only affects display format.
            // 整数性与否只影响显示格式。
            bool isInt = (node->value.find('.') == std::string::npos)
                      && (node->value.find('e') == std::string::npos)
                      && (node->value.find('E') == std::string::npos);
            return rb::make_number(std::stod(node->value), isInt);
        }
        if (k == "string") {
            return rb::make_string(node->value);
        }
        if (k == "bool") {
            return rb::make_boolean(node->value == "true");
        }
        if (k == "name") {
            // Keywords that parse to constant built-in instances
            // (mirrors how `13` is a std::Number): NaN and Infinity are
            // real std::Number objects carrying the IEEE non-finite value.
            // 解析为内置常数实例的关键字（与 `13` 即 std::Number 同理）：
            // NaN 与 Infinity 是承载 IEEE 非有限值的真实 std::Number 对象。
            if (node->value == "NaN") {
                return rb::make_float(
                    std::numeric_limits<double>::quiet_NaN());
            }
            if (node->value == "Infinity" || node->value == "inf") {
                return rb::make_float(
                    std::numeric_limits<double>::infinity());
            }
            return resolve(f, node->value);
        }
        if (k == "inst") {
            // Instantiation expression: create the object, bind it to its
            // variable in the frame, and return it as the expression value.
            // 实例化表达式：创建对象，绑定到帧中变量，并作为表达式值返回。
            auto obj = ::stdRT.make(node->value);
            if (!obj) {
                interp_error(
                    "RuntimeException",
                    "Cannot instantiate unknown type '" + node->value + "'."
                );
            }
            obj->give_name(node->name);
            if (!node->isPlaceholder && !node->name.empty()) {
                f.locals[node->name] = obj;
            }
            // Constructor init in expression position: `-(Type(args) name)`.
            // 表达式位置的构造初始化：`-(类型(实参) 名)`。
            if (!node->kids.empty()) {
                call_constructor(f, obj, node->kids[0]);
            }
            return obj;
        }
        if (k == "call") {
            auto receiverObj = eval_expr(f, node->kids[0]);
            std::vector<RuntimeObjectPtr> args;
            for (auto& a : node->kids[1]->kids) {
                args.push_back(eval_arg(f, a));
            }
            // The assign method `=` must target a variable (a bare name or a
            // member access), never an expression result such as `1.+(1)`.
            // 赋值方法 `=` 必须作用于变量（裸名或成员访问），
            // 不能是 `1.+(1)` 这类表达式结果。
            if (node->value == "=" && node->kids[0]->kind != "name"
                    && node->kids[0]->kind != "access") {
                interp_error(
                    "InterException",
                    "cannot assign to a non-variable expression (the left "
                    "side of '.=(...)' must be a variable, e.g. `x.=(value)`)"
                );
            }
            // Member-method (re)binding: `obj.method.=(beh)` or `obj.method = beh`.
            // A method variable is just a member holding a behavior; assigning
            // to it swaps the callable. This is the natural consequence of
            // "a method is a variable bound to a behavior" — no special rule.
            // Const methods (@!inc) refuse to be rebound.
            // 成员方法（重）绑定：`obj.method.=(beh)` 或 `obj.method = beh`。
            // 方法变量即持有行为的成员，对其赋值即替换 callable——这正是
            // “方法即绑定到行为之上的变量”的自然结论，无需特判。常数方法
            // （@!inc）拒绝被重绑。
            if (node->value == "=" && node->kids[0]->kind == "access") {
                auto owner = eval_expr(f, node->kids[0]->kids[0]);
                const std::string& field = node->kids[0]->value;
                auto cls = std::dynamic_pointer_cast<RuntimeClass>(owner);
                if (cls) {
                    auto mit = cls->get_methods().find(field);
                    if (mit != cls->get_methods().end()) {
                        // Resolve the RHS behavior AST: a literal behavior
                        // node, or a value that is already a RuntimeBehavior.
                        // 解析右值行为 AST：字面量行为节点，或本身就是
                        // RuntimeBehavior 的值。
                        AstNodePtr behAST = nullptr;
                        AstNodePtr rhsNode = node->kids[1]->kids[0];
                        if (rhsNode->kind == "behavior") {
                            behAST = rhsNode;
                        } else if (!args.empty()) {
                            auto rb
                                = std::dynamic_pointer_cast<RuntimeBehavior>(
                                    args[0]);
                            if (rb) behAST = rb->get_astn();
                        }
                        rebind_method(cls.get(), field, behAST);
                        return rb::first_of(rb::empty_result());
                    }
                }
                // Not a method on an object: fall through to the normal
                // `=` dispatch (data attribute or any value's `=` method).
                // 非对象方法：落到常规 `=` 派发（数据属性或任意值的 `=` 方法）。
            }
            // Variable assignment via `.=`: the RHS is what the variable would
            // hold, so a constrained variable must keep satisfying its
            // constraint. Example: `-(io::OStream[io::OStream] out); out.=(3)`
            // must reject the Number `3`. The assignment itself still delegates
            // to the held value's `=` method (scalar copy / object receive), so
            // this only adds the constraint guard, preserving `.=(...)`'s
            // existing semantics for unconstrained and behavior-RHS cases.
            // 变量经 `.=` 赋值：右值即变量将要持有的值，故带约束变量须继续
            // 满足其约束。例如 `-(io::OStream[io::OStream] out); out.=(3)` 须
            // 拒收 Number `3`。赋值本身仍委托给所持有值的 `=` 方法（标量复制
            // / 对象接收），故此处仅追加约束守卫，保持 `.=(...)` 既有语义不变。
            if (node->value == "=" && node->kids[0]->kind == "name") {
                const std::string& vname = node->kids[0]->value;
                if (f.constraints.count(vname) && !f.constraints[vname].empty()
                        && !args.empty()
                        && !std::dynamic_pointer_cast<RuntimeBehavior>(
                               args[0])) {
                    check_constraint(f.constraints[vname], args[0]);
                }
            }
            auto res = invoke(
                receiverObj, node->value,
                std::make_shared<std::vector<RuntimeObjectPtr>>(
                    std::move(args))
            );
            return rb::first_of(res);
        }
        if (k == "selfcall") {
            // A bare `name(args)` is normally a self-dispatch to an instance
            // method and must be written `self.name(args)`. But if `name`
            // resolves to a local closure variable holding a behavior, it is
            // called directly (so a closure-defined `@clos << [...]; clos();`
            // works). Instance methods and bare data names keep the self. hint.
            // 裸 `name(args)` 通常是自派发到实例方法，须写成 `self.name(args)`；
            // 但若 name 解析为持有行为的局部闭包变量，则直接调用之（使闭包内
            // `@clos << [...]; clos();` 可用）。实例方法与裸数据名仍保留
            // self. 提示。
            RuntimeObjectPtr target = nullptr;
            if (f.locals.count(node->value)) {
                target = f.locals[node->value];
            } else if (f.outer && f.outer->count(node->value)) {
                target = (*f.outer)[node->value];
            }
            auto beh = std::dynamic_pointer_cast<RuntimeBehavior>(target);
            if (beh) {
                std::vector<RuntimeObjectPtr> args;
                for (auto& a : node->kids[0]->kids) {
                    args.push_back(eval_arg(f, a));
                }
                auto res = beh->call(
                    f.locals,
                    std::make_shared<std::vector<RuntimeObjectPtr>>(
                        std::move(args)));
                return rb::first_of(res);
            }
            interp_error(
                "InterException",
                "instance method '" + node->value
                    + "' must be called as 'self." + node->value
                    + "(...)' (a bare '" + node->value
                    + "(...)' is resolved as data, not a method call)"
            );
        }
        if (k == "access") {
            // Member attribute lookup, or a method reference wrapped as a
            // bound behavior. 成员属性查找，或包装为绑定行为的方法引用。
            auto recv = eval_expr(f, node->kids[0]);
            auto cls = std::dynamic_pointer_cast<RuntimeClass>(recv);
            if (cls) {
                const auto& attrs = cls->get_attributes();
                if (attrs.count(node->value)) {
                    return attrs.at(node->value);
                }
                const auto& methods = cls->get_methods();
                if (methods.count(node->value)) {
                    RuntimeObjectPtr bound = recv;
                    std::string mname = node->value;
                    rt_basic::NativeClosure clos =
                        [bound, mname](
                            InstanceMap&, InstanceListPtr paras
                        ) -> InstanceListPtr {
                            return invoke(bound, mname, paras);
                        };
                    return std::make_shared<RuntimeBehavior>(
                        Callable(clos, rb::make_sign(mname, {}, {}),
                                 rt_basic::BehavStateOBJ::NORMAL,
                                 {false, false})
                    );
                }
            }
            interp_error(
                "InterException",
                "object has no member '" + node->value + "'"
            );
        }
        if (k == "flow") {
            return exec_flow(f, node);
        }
        if (k == "behavior") {
            return make_closure(node, f);
        }
        if (k == "tuple") {
            // Build a std::Tuple from the element list.
            // 用元素列表构造 std::Tuple。
            auto t = ::stdRT.make("Tuple");
            if (!t) {
                interp_error("RuntimeException", "Tuple type unavailable");
            }
            invoke(t, ":=", eval_expr_list(f, node));
            return t;
        }
        interp_error("InterException", "cannot evaluate node kind '" + k + "'");
        return nullptr;          // unreachable: interp_error exits the process
    }

    // Evaluate an expression to a value *list* (used for multi-return
    // destructuring and tuple literals).
    // 把一个表达式求值为*列表*（用于多返回值解构与元组字面量）。
    inline InstanceListPtr eval_expr_list(Frame& f, AstNodePtr node) {
        if (!node) return rb::empty_result();
        const std::string& k = node->kind;
        if (k == "call") {
            auto receiverObj = eval_expr(f, node->kids[0]);
            std::vector<RuntimeObjectPtr> args;
            for (auto& a : node->kids[1]->kids) {
                args.push_back(eval_arg(f, a));
            }
            return invoke(
                receiverObj, node->value,
                std::make_shared<std::vector<RuntimeObjectPtr>>(
                    std::move(args))
            );
        }
        if (k == "selfcall") {
            // Bare `name(args)` is not a self-dispatch (see eval_expr).
            // 裸 `name(args)` 非自派发（见 eval_expr）。
            interp_error(
                "InterException",
                "instance method '" + node->value
                    + "' must be called as 'self." + node->value
                    + "(...)' (a bare '" + node->value
                    + "(...)' is resolved as data, not a method call)"
            );
        }
        if (k == "flow") {
            auto r = std::make_shared<std::vector<RuntimeObjectPtr>>();
            r->push_back(exec_flow(f, node));
            return r;
        }
        if (k == "tuple") {
            auto r = std::make_shared<std::vector<RuntimeObjectPtr>>();
            for (auto& e : node->kids) {
                r->push_back(eval_expr(f, e));
            }
            return r;
        }
        auto r = std::make_shared<std::vector<RuntimeObjectPtr>>();
        r->push_back(eval_expr(f, node));
        return r;
    }

    // ========================================================
    // Closures / 闭包
    // ========================================================

    // Turn an inline behavior literal into a RuntimeBehavior that captures
    // the current accessible scope (outer members overlaid by locals).
    // 把内联行为字面量变成 RuntimeBehavior，捕获当前可见作用域
    // （外层成员被局部变量覆盖）。
    inline RuntimeObjectPtr make_closure(AstNodePtr behavior, Frame& f) {
        InstanceMap captured;
        if (f.outer) captured = *f.outer;
        for (auto& kv : f.locals) {
            captured[kv.first] = kv.second;
        }
        // The mode lives on the inner sign node (behavior = [sign, block]).
        // 模式存于内部 sign 节点（behavior = [sign, block]）。
        std::string mode = (behavior->kids.empty())
            ? "->" : behavior->kids[0]->value;
        CallableSign sign = build_sign(behavior, "<behavior>");
        // Capture `self` as a weak_ptr so a stored closure (a class method, a
        // rebound method, or a closure-typed field) does not keep its owning
        // instance alive: that would be a reference cycle
        // (instance -> method -> closure -> instance) and leak. The instance
        // is always alive while the closure runs, so lock() yields the shared
        // pointer here. 以 weak_ptr 捕获 self：被存储的闭包（类方法、重绑
        // 方法或闭包类型字段）不会反向保活其所属实例，从而避免引用环
        // （实例 -> 方法 -> 闭包 -> 实例）造成泄漏。闭包执行时实例必然
        // 存活，故此处 lock() 可取回共享指针。
        std::weak_ptr<runtime::RuntimeClass> self_w(f.self);
        rt_basic::NativeClosure clos =
            [captured, behavior, sign, self_w](
                InstanceMap& /*env_ignored*/,
                InstanceListPtr paras
            ) mutable -> InstanceListPtr {
                return exec_behavior(captured, paras, behavior, sign, self_w.lock());
            };
        return std::make_shared<RuntimeBehavior>(
            Callable(clos, sign, state_from_mode(mode), {false, false})
        );
    }

    // ========================================================
    // Behavior execution / 行为执行
    // ========================================================

    // Execute a user-defined behavior (method or closure).
    // 执行用户定义行为（方法或闭包）。
    inline InstanceListPtr exec_behavior(
        InstanceMap& env,
        const InstanceListPtr& paras,
        AstNodePtr behavior,
        const CallableSign& /*sign*/,
        RuntimeClassPtr self
    ) {
        if (!behavior || behavior->kids.empty()) {
            interp_error("InterException", "exec_behavior on a malformed node");
        }
        // Recursion depth guard (force-interrupt on runaway self-recursion).
        // 递归深度护栏（对失控的自递归强制中断）。
        ++g_recursion_depth;
        if (g_recursion_depth > RECURSION_LIMIT) {
            --g_recursion_depth;
            interp_error(
                "RecursionLimitException",
                "recursion depth exceeded " + std::to_string(RECURSION_LIMIT)
                    + " (possible infinite self-call) - the whole call stack "
                      "was unwound and no value was produced"
            );
        }
        // A `behavior` node is [sign, block]; the mode lives on the sign node.
        // behavior 节点为 [sign, block]；模式存于 sign 节点。
        AstNodePtr signNode = behavior->kids[0];

        Frame f;
        f.outer = &env;
        f.self = self;
        f.mode = signNode->value;

        // Bind input parameters (by position).
        // 按位置绑定输入参数。
        if (signNode->kids.size() >= 1) {
            auto& params = signNode->kids[0]->kids;
            for (std::size_t i = 0; i < params.size(); ++i) {
                auto& p = params[i];
                if (p->isPlaceholder) continue;
                f.locals[p->name] = rb::para_at(paras, i);
                // Parameter constraint (e.g. `name[Addable]`): once the actual
                // argument is bound, verify it satisfies the constraint.
                // 参数约束（如 `名称[Addable]`）：实参绑定后立即校验约束。
                if (!p->constraint.empty()) {
                    check_constraint(p->constraint, f.locals[p->name]);
                }
            }
        }
        // Bind output parameters to fresh zero values.
        // 把输出参数绑定到全新的零值。
        if (signNode->kids.size() >= 2) {
            auto& outputs = signNode->kids[1]->kids;
            for (auto& o : outputs) {
                if (o->isPlaceholder) continue;
                std::string type = (o->mode == "type") ? o->value : "Object";
                auto z = ::stdRT.make(type);
                if (!z) z = ::stdRT.make("Object");
                z->give_name(o->name);
                f.locals[o->name] = z;
            }
        }

        exec_block(f, behavior->kids[1]);

        // Collect outputs in declaration order. `void` is no longer a keyword;
        // an empty output list `()` means "no return", and any ordinary name
        // is returned as-is. 按声明顺序收集输出。`void` 已不再是关键字；
        // 空输出列表 `()` 即表示「无返回」，其余普通名称原样返回。
        // 按声明顺序收集输出。
        auto result = std::make_shared<std::vector<RuntimeObjectPtr>>();
        if (signNode->kids.size() >= 2) {
            for (auto& o : signNode->kids[1]->kids) {
                if (o->isPlaceholder) continue;
                result->push_back(resolve(f, o->name));
            }
        }
        if (result->empty()) { --g_recursion_depth; return rb::empty_result(); }
        --g_recursion_depth;
        return result;
    }

    // ========================================================
    // Statements / 语句
    // ========================================================

    // Execute a block of statements.
    // 执行语句块。
    inline void exec_block(Frame& f, AstNodePtr block) {
        for (auto& stmt : block->kids) {
            exec_stmt(f, stmt);
        }
    }

    // Execute a single statement.
    // 执行单条语句。
    inline void exec_stmt(Frame& f, AstNodePtr stmt) {
        // Record the current evaluation locus for the diagnostic reporter.
        // 记录当前求值位置，供诊断上报器使用。
        diag::set_locus(diag::source_file(), stmt->line, stmt->col);
        if (stmt->kind == "vardef") {
            exec_vardef(f, stmt);
        } else if (stmt->kind == "methoddef") {
            // A method/closure declaration reached at runtime (inside a closure
            // body) defines a LOCAL closure variable bound to a behavior — it
            // is not an instance method. This mirrors how `-(behavior x)` would
            // bind a behavior locally.
            // 运行期遇到的“方法 / 闭包声明”（在闭包体内）定义的是一个**局部**
            // 闭包变量，绑定到行为——它并非实例方法。语义等价于把行为作为
            // 局部变量绑定。
            std::string mname = stmt->value;
            if (mname == "::" || mname == "~" || mname == "=:"
                || mname == ":=") {
                interp_error(
                    "InterException",
                    "special method '" + mname + "' cannot be declared "
                    "inside a closure body"
                );
            }
            if (f.locals.count(mname)) {
                interp_error(
                    "InterException",
                    "variable '" + mname
                        + "' is already declared in this scope"
                );
            }
            f.locals[mname] = make_closure(stmt->kids[0], f);
        } else {
            // Expression statement (flow / call / instantiation chain / ...):
            // evaluated for its side effects.
            // 表达式语句（流 / 调用 / 实例化链 / …）：为副作用而求值。
            eval_expr(f, stmt);
        }
    }

    // Run a class constructor with the given argument list. Honors a
    // user-defined `@::(...) -> (void)` if present; otherwise duck-types a
    // single-argument construction by flowing the argument into the instance
    // (so `-(std::Number(5) x)` works even without an explicit constructor).
    // 用给定实参列表运行类构造函数。若存在用户定义的 `@::(...) -> (void)`
    // 则调用之；否则对单参构造做鸭子式处理——把实参流入实例，
    // 使 `-(std::Number(5) x)` 即便没有显式构造函数也能工作。
    inline void call_constructor(
        Frame& f, RuntimeObjectPtr obj, AstNodePtr argsNode
    ) {
        auto list = std::make_shared<std::vector<RuntimeObjectPtr>>();
        if (argsNode) {
            for (auto& a : argsNode->kids) {
                list->push_back(eval_arg(f, a));
            }
        }
        auto cls = std::dynamic_pointer_cast<RuntimeClass>(obj);
        if (!cls) return;                       // primitives: nothing to build
        auto methods = cls->get_methods();
        auto found = methods.find("::");
        if (found != methods.end()) {
            // For a *user-defined* constructor (an AST behavior) the number of
            // supplied arguments must match the declared (non-placeholder)
            // parameter count; a mismatch is an immediate error (a constructor
            // must not silently drop or invent arguments).
            // 对*用户定义*构造函数（AST 行为），实参数量须与声明
            //（非占位）参数数量一致；不一致即为即时报错（构造函数不得
            // 静默丢弃或臆造参数）。
            if (found->second.is_user()) {
                const auto& inpara = found->second.get_sign().inpara;
                std::size_t expected = 0;
                for (const auto& pr : inpara) {
                    if (pr.first != "_") ++expected;
                }
                if (list->size() != expected) {
                    std::string tname = obj->selfname;
                    if (cls->get_prototype() && !cls->get_prototype()->name.empty()) {
                        tname = cls->get_prototype()->name;
                    }
                    interp_error(
                        "RuntimeException",
                        "constructor of type '" + tname + "' expects "
                            + std::to_string(expected) + " argument(s), but got "
                            + std::to_string(list->size())
                    );
                }
            }
            // The constructor's return list is always discarded: a constructor
            // builds the instance via self-members / output parameters and
            // never returns a value. 构造函数的返回列表始终被丢弃：构造函数
            // 经 self 成员 / 输出参数构建实例，从不返回值。
            found->second.call(cls->get_attributes(), list, cls);
            return;
        }
        // Duck-typed fallback: a single argument is received (":=") into the
        // instance, reusing the normal flow negotiation.
        // 鸭子式兜底：单实参经接收端 ":=" 流入实例，复用常规流协商。
        if (list->size() == 1) {
            flow_into(f, obj, (*list)[0]);
        } else if (!list->empty()) {
            interp_error(
                "RuntimeException",
                "type '" + obj->selfname
                    + "' has no constructor accepting "
                    + std::to_string(list->size()) + " arguments"
            );
        }
    }

    // Variable-definition statement, with optional initialization.
    // 变量定义语句，可带初始化。
    inline void exec_vardef(Frame& f, AstNodePtr node) {
        // Separate declarations from the (optional) initializer.
        // 把声明与（可选的）初始化器分开。
        std::vector<AstNodePtr> decls;
        AstNodePtr init = nullptr;
        for (auto& kid : node->kids) {
            if (kid->kind == "decl") {
                decls.push_back(kid);
            } else {
                init = kid;            // the initializer expression
            }
        }

        // Create each declared variable as a zero value (zero-value law).
        // 把每个声明变量创建为零值（零值法则）。
        for (auto& d : decls) {
            if (d->isPlaceholder) continue;          // `_` discards
            // Re-declaring a name already bound in THIS scope (a variable, an
            // input parameter, or an output parameter) is an error, not a
            // silent shadow. Input/output parameters are themselves variables
            // the interpreter pre-declares, so re-declaring them must fail.
            // 重新声明本作用域内已绑定的名字（变量、输入参数或输出参数）是
            // 错误而非静默覆盖。输入 / 输出参数本身就是解释器预先声明的变量，
            // 故重声明它们必须报错。
            if (f.locals.count(d->name)) {
                interp_error(
                    "InterException",
                    "variable '" + d->name
                        + "' is already declared in this scope"
                );
            }
            auto zero = ::stdRT.make(d->value);
            if (!zero) {
                interp_error(
                    "RuntimeException",
                    "Cannot declare unknown type '" + d->value + "'."
                );
            }
            zero->give_name(d->name);
            f.locals[d->name] = zero;
            f.constFlag[d->name] = d->isConst;
            // Constructor init: `-(Type(args) name)` builds the object from
            // the given arguments. 构造初始化：`-(类型(实参) 名)` 用给定实参造对象。
            if (!d->kids.empty()) {
                call_constructor(f, zero, d->kids[0]);
            }
            // Variable constraint: a constrained variable must hold a satisfying
            // value. The check is against the FINAL bound value, not the transient
            // zero: when an initializer is present the single-/multi-declaration
            // paths below re-check after binding, so we only verify here when the
            // declaration has NO initializer (its zero value is then the final
            // value). Verifying the bare zero would wrongly reject every
            // constrained declaration that is later filled by a flow.
            // 变量约束：带约束的变量必须持有满足约束的值。核查针对「最终绑定值」
            // 而非临时零值：存在初始化器时，下方单 / 多声明路径会在绑定后重新
            // 校验，故仅当「无初始化器」的声明在此核查（其零值即终值）。若直接
            // 核查裸零值，会误拒所有随后经流填充的约束声明。
            if (!d->constraint.empty()) {
                f.constraints[d->name] = d->constraint;
                if (!init) {
                    check_constraint(d->constraint, f.locals[d->name]);
                }
            }
        }

        if (!init) return;

        // Single declaration: evaluate the initializer as ONE value. If that
        // value is a std::Tuple, a scalar receiver binds to its first element
        // (tuple-first destructuring, spec), while a Tuple/Array/stream
        // receiver keeps the whole value.
        // 单一声明：把初始化器求值为「单个」值。若该值是 std::Tuple，标量接收
        // 方绑定到其首项（元组首项解构，文档），而 Tuple/Array/流接收方保留
        // 整体值。
        if (decls.size() == 1) {
            auto& d = decls[0];
            if (!d->isPlaceholder) {
                // A const variable may only be built through a constructor
                // (handled in the decl loop above), never (re)assigned by a
                // trailing flow statement `-(Type! x) << v`.
                // 常数变量只能经构造器（上方 decl 循环已处理）构建，
                // 不得被尾部流语句 `-(Type! x) << v` 赋值。
                if (d->isConst) {
                    interp_error(
                        "ConstException",
                        "cannot initialize const variable '" + d->name
                            + "' through a flow statement; use a constructor, "
                              "e.g. -(std::Number(0)! " + d->name + ")"
                    );
                }
                flow_into(f, f.locals[d->name], eval_expr(f, init));
                // Re-check the variable's constraint after the assignment.
                // 赋值后再次校验变量约束。
                if (!d->constraint.empty()) {
                    check_constraint(d->constraint, f.locals[d->name]);
                }
            }
            return;
        }

        // Multi-declaration (tuple destructuring, spec 5.4.3): evaluate the
        // initializer to a value *list* and bind each non-placeholder variable
        // to the corresponding element by position. If the initializer yielded
        // exactly one value that is a std::Tuple, its elements are used as the
        // list (so `(-(a), -(b)) << self.pair()` works).
        // 多声明（元组解构，文档 5.4.3）：把初始化器求值为*值列表*，按位置把
        // 每个非占位变量绑定到对应元素。若初始化器恰得到一个 std::Tuple 值，
        // 则以其元素作为列表（故 `(-(a), -(b)) << self.pair()` 生效）。
        auto initList = eval_expr_list(f, init);
        if (initList->size() == 1) {
            auto& only = (*initList)[0];
            if (auto tup = std::dynamic_pointer_cast<RuntimeClass>(only)) {
                if (tup->get_prototype()
                        && tup->get_prototype()->name == "Tuple") {
                    auto expanded =
                        std::make_shared<std::vector<RuntimeObjectPtr>>();
                    auto* attrs = rb::attributes_of(only);
                    std::size_t n = rb::container_size(*attrs);
                    for (std::size_t i = 0; i < n; ++i) {
                        auto fnd = attrs->find(rb::elem_key(i));
                        if (fnd != attrs->end()) {
                            expanded->push_back(fnd->second);
                        }
                    }
                    initList = expanded;
                }
            }
        }
        std::size_t idx = 0;
        for (auto& d : decls) {
            if (d->isPlaceholder) continue;
            // Same const rule as the single-declaration path (see above).
            // 与单声明路径相同的常数规则（见上）。
            if (d->isConst) {
                interp_error(
                    "ConstException",
                    "cannot initialize const variable '" + d->name
                        + "' through a flow statement; use a constructor, e.g. "
                          "-(std::Number(0)! " + d->name + ")"
                );
            }
            if (idx < initList->size()) {
                flow_into(f, f.locals[d->name], (*initList)[idx]);
                // Re-check the variable's constraint after the assignment.
                // 赋值后再次校验变量约束。
                if (!d->constraint.empty()) {
                    check_constraint(d->constraint, f.locals[d->name]);
                }
            } else {
                interp_error(
                    "InterException",
                    "too few values to destructure into '" + d->name + "'"
                );
            }
            ++idx;
        }
    }

    // ========================================================
    // Top-level definitions / 顶层定义
    // ========================================================

    // Define a $Class: build its prototype (members + methods) and register
    // it. Parent may be a class (inheritance) or a #Contract (satisfaction).
    // 定义 $Class：构建其原型（成员 + 方法）并登记。父类可为类（继承）
    // 或 #Contract（满足）。
    inline void define_class(AstNodePtr classdef) {
        std::string className = classdef->value;

        // Protect already-registered classes (e.g. native standard libraries
        // brought online by an import). Re-defining would wipe their native
        // methods, so we keep the first registration.
        // 保护已登记的类（如经 import 上线的原生标准库）。重定义会抹掉其原生
        // 方法，故保留首次登记。
        if (::stdRT.getcls(className)) {
            return;
        }

        std::string parentName;
        bool hasParent = (classdef->kids.size() >= 2);
        if (hasParent && classdef->kids[0]->kind == "name") {
            parentName = classdef->kids[0]->value;
        }
        AstNodePtr block = classdef->kids.back();   // kids.back() is the body

        // Resolve the base prototype.
        // 解析基类原型。
        ClsProtoPtr baseProto = nullptr;
        std::string contractName;
        if (!parentName.empty()) {
            if (contracts().count(parentName)) {
                // Implementing a contract: inherit the reserved Object methods.
                // 满足约束：继承 Object 的保留方法。
                baseProto = ::stdRT.getcls("Object");
                contractName = parentName;
            } else {
                baseProto = ::stdRT.getcls(parentName);
                if (!baseProto) {
                    interp_error(
                        "InterException",
                        "class '" + className
                            + "' extends unknown parent '" + parentName + "'"
                    );
                }
            }
        } else {
            baseProto = ::stdRT.getcls("Object");
        }
        if (!baseProto) {
            interp_error(
                "InterException",
                "cannot resolve base prototype for class '" + className + "'"
            );
        }

        auto proto = std::make_shared<ClsProto>(baseProto);

        for (auto& item : block->kids) {
            if (item->kind == "vardef") {
                // Member variables become zero-value attributes, then a written
                // constructor initializer (e.g. `-(std::Number(7) inner)`) is
                // applied — mirroring how a local `-(Type(args) name)` is built.
                // Previously the member initializer was silently dropped, so
                // every member stayed at its zero value regardless of what the
                // source wrote (a real correctness bug, not a zero-value-law
                // case: an *initialized* member must hold its initializer).
                // 成员变量先变为零值属性；随后应用所写的构造初始化器
                //（如 `-(std::Number(7) inner)`），与局部 `-(类型(实参) 名)` 的
                // 构建方式一致。此前成员初始化器被静默丢弃，无论源码写了什么都
                // 停留在零值（这是真正的正确性 bug，而非零值法则情形：*已初始化*
                // 的成员必须持有其初始化值）。
                for (auto& d : item->kids) {
                    if (d->kind != "decl" || d->isPlaceholder) continue;
                    auto zero = ::stdRT.make(d->value);
                    if (!zero) {
                        interp_error(
                            "RuntimeException",
                            "class '" + className
                                + "' has member of unknown type '"
                                + d->value + "'"
                        );
                    }
                    zero->give_name(d->name);
                    if (!d->kids.empty()) {
                        Frame f;
                        call_constructor(f, zero, d->kids[0]);
                    }
                    proto->set_attribute(d->name, zero);
                }
            } else if (item->kind == "methoddef") {
                // Method injection: wrap the behavior AST as a Callable.
                // 方法注入：把行为 AST 包装为 Callable。
                std::string mname = item->value;
                bool isC = item->isConst;
                bool isP = item->isPrivate;
                AstNodePtr rhs = item->kids[0];      // the behavior node
                CallableSign sign = build_sign(rhs, mname);
                Callable c(
                    rhs, sign, state_from_mode(rhs->value), {isC, isP}
                );
                proto->set_method(mname, c);
            }
        }

        register_proto(className, proto);

        // Validate contract satisfaction if claimed.
        // 若声明满足约束，则校验。
        if (!contractName.empty()) {
            auto inst = std::make_shared<RuntimeClass>(proto);
            if (!contracts()[contractName].validate(inst)) {
                interp_error(
                    "ContractException",
                    "class '" + className
                        + "' does not satisfy contract '" + contractName + "'"
                );
            }
        }
    }

    // Define a #Contract: collect its method signatures into a ClassContract.
    // 定义 #Contract：把方法签名收集为 ClassContract。
    inline void define_contract(AstNodePtr contractdef) {
        std::string cname = contractdef->value;
        AstNodePtr block = contractdef->kids.back();
        ClassContract cc;
        for (auto& signitem : block->kids) {
            std::string mname = signitem->value;
            AstNodePtr signNode = signitem->kids[0];   // (params) mode (outputs)
            cc.add_sign(build_sign(signNode, mname));
        }
        contracts()[cname] = cc;
    }

    // ========================================================
    // Standard-library import / 标准库导入
    // ========================================================

    // Import a standard library named `name`:
    //  1. Bring its C++ backend online (a no-op for pure-Synth-OOP libs).
    //  2. Interpret lib/<name>.synl if it exists, defining its classes /
    //     contracts. Any `$Program` class in a library is ignored.
    // 导入名为 name 的标准库：
    //  1. 上线其 C++ 底层（纯 Synth-OOP 库为空操作）。
    //  2. 若存在 lib/<name>.synl 则解释之，定义其中的类 / 约束。
    //     库中的 $Program 类被忽略。
    inline void import_library(const std::string& name) {
        // (1) Native backend (if registered).
        // （1）原生底层（若已注册）。
        rb::init_native_lib(name);

        // (2) Interpreted Synth-OOP face.
        // （2）解释型 Synth-OOP 形态。
        std::string path = rb::stdlib_dir() + "/" + name + ".synl";
        std::ifstream fin(path);
        if (!fin) {
            return;   // native-only lib, or a builtin-backed name (e.g. io)
        }
        std::stringstream ss;
        ss << fin.rdbuf();
        parser::Parser par(ss.str());
        AstNodePtr lib = par.parse_program();
        for (auto& item : lib->kids) {
            if (item->kind == "classdef") {
                // A library's $Program is documentation only and must not
                // become the program entry point.
                // 库的 $Program 仅作说明，不得成为程序入口。
                if (item->value == "Program") continue;
                define_class(item);
            } else if (item->kind == "contractdef") {
                define_contract(item);
            }
            // Nested import nodes inside a library are ignored (flat layout).
            // 库内嵌套的 import 节点忽略（库为扁平布局）。
        }
    }

    // ========================================================
    // Program entry / 程序入口
    // ========================================================

    // Parse and run a Synth OOP source program. Defines every top-level type,
    // then instantiates `$Program` (which runs its `@::` entry behavior).
    // `lib_dir` is the resolved path to the standard-library directory
    // (where lib/<name>.synl and lib/<name>.hpp live).
    // 解析并运行一段 Synth OOP 源码。定义全部顶层类型后实例化 `$Program`
    // （运行其 `@::` 入口行为）。`lib_dir` 为标准库目录的解析路径
    // （lib/<name>.synl 与 lib/<name>.hpp 所在处）。
    inline RuntimeObjectPtr run_program(
        const std::string& source, const std::string& lib_dir = "lib",
        const std::string& source_name = "<program>"
    ) {
        try {
        rb::init_builtins();
        rb::stdlib_dir() = lib_dir;
        rb::init_stdlibs();          // bring C++-backed standard libs online

        // Feed source text to the diagnostic reporter so runtime errors can
        // print the offending line (g++-style). The program is always the
        // entry point, so a fixed label is sufficient.
        // 把源文本交给诊断上报器，使运行时错误能打印出错行（类 g++）。
        // 程序恒为入口；source_name 为文件名（无文件时退化为 <program>）。
        {
            diag::source_file() = source_name.empty() ? "<program>" : source_name;
            std::vector<std::string> lines;
            std::stringstream ss(source);
            std::string ln;
            while (std::getline(ss, ln)) lines.push_back(ln);
            diag::source_lines() = std::move(lines);
        }

        parser::Parser par(source);
        AstNodePtr program = par.parse_program();

        AstNodePtr programClass = nullptr;
        for (auto& item : program->kids) {
            if (item->kind == "classdef") {
                define_class(item);
                if (item->value == "Program") {
                    programClass = item;
                }
            } else if (item->kind == "contractdef") {
                define_contract(item);
            } else if (item->kind == "import") {
                // The `&module;` statement resolves the library from the
                // standard-library directory.
                // `&module;` 语句从标准库目录解析该库。
                import_library(item->value);
            }
        }

        if (!programClass) {
            // No main program class: a runnable Synth-OOP source MUST define
            // `$Program` (its `@::` behavior is the entry point). Report this
            // as a hard error instead of silently doing nothing — an empty or
            // class-less source used to exit successfully with no output at
            // all, which hid the real problem from the user.
            // 无主程序类：可运行的 Synth-OOP 源码必须定义 `$Program`（其
            // `@::` 行为即入口）。此处硬性报错，而非静默无事发生——此前空
            // 源码 / 无 Program 类的源码会以「无输出、成功退出」收场，把
            // 真正的问题藏了起来。
            diag::set_locus(diag::source_file(), 1, 1);
            interp_error(
                "ProgramException",
                "program has no entry point: no '$Program' class is defined "
                "(a runnable source must define the main program class "
                "'$Program', whose '@::' constructor is the entry point; "
                "a file meant to be imported should not be run directly)"
            );
        }
        // Anchor the entry-point frame's locus to the $Program definition.
        // 将入口帧的位置锚定到 $Program 定义处。
        diag::set_locus(diag::source_file(), programClass->line, 1);
        return instantiate("Program");
        } catch (const rt_builtin::NativeError& e) {
            // A native method raised a runtime error. Report it with a
            // g++-style diagnostic using the interpreter-set locus and exit.
            // 原生方法抛出运行时错误：沿用解释器设置的位置，经 Thrower
            // 上报类 g++ 诊断并退出。
            Thrower.throwE("RuntimeException", e.what_msg);
            return {};
        }
    }

} // namespace interp

// Register the interpreter as the executor for user behavior AST nodes
// (fixes runtime.hpp TODO:103). Runs at static initialization of the TU that
// includes this header.
// 把解释器登记为用户行为 AST 节点的执行器（修好 runtime.hpp 的 TODO:103）。
// 于包含本头文件的编译单元静态初始化时生效。
namespace rt_basic {
    inline bool _interp_registered =
        (g_user_behavior_executor = interp::exec_behavior, true);
} // namespace rt_basic

#endif
