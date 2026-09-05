// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <format>

#include "exception_throw.hpp"

#ifndef RUNTIME_HPP
#define RUNTIME_HPP

// Foward Declaration Round 1
namespace parser {
    class AstNode;
    using AstNodePtr = std::shared_ptr<AstNode>;
}
namespace runtime {
    class RuntimeObject;

    using RuntimeObjectPtr = std::shared_ptr<RuntimeObject>;
}
namespace rt_basic {
    class ClsProto;
    class Callable;

    using ClsProtoPtr = std::shared_ptr<ClsProto>;

    using InstanceMap = std::unordered_map<std::string, runtime::RuntimeObjectPtr>;
    using CallableMap = std::unordered_map<std::string, Callable>;
    using InstanceListPtr = std::shared_ptr<std::vector<runtime::RuntimeObjectPtr>>;
    using NativeClosure = std::function<InstanceListPtr(InstanceMap&, InstanceListPtr)>;
}

// Forward Declaration Round 2
namespace runtime {
    class RuntimeClass;
    using RuntimeClassPtr = std::shared_ptr<RuntimeClass>;
}



namespace rt_basic {
    struct CallableSign {
        std::string name;

        std::vector<std::pair<std::string, std::string>> inpara;
        std::vector<std::pair<std::string, std::string>> outpara;

        CallableSign() : name("<UNNAMED>"), inpara({}), outpara({}) {}
        CallableSign(std::string _name, std::vector<std::pair<std::string, std::string>> _inpara, std::vector<std::pair<std::string, std::string>> _outpara) {
            name = _name;
            inpara = _inpara;
            outpara = _outpara;
        }

        bool operator==(const CallableSign& oth) const {
            if (oth.name != name) {
                return false;
            }
            return (oth.inpara == inpara && oth.outpara == outpara);
        }
    };

    // User-behavior executor hook.
    // 用户行为执行钩子。
    //
    // The interpreter assigns a real implementation here so that
    // Callable::call can execute AST-node behaviors (the missing piece that
    // TODO:103 left as `return nullptr`). Declared as an inline std::function
    // so it can be assigned from interpreter.hpp without ODR issues in a
    // single-translation-unit build. Callable::call forwards the object's
    // attribute map (env), the argument list, the behavior AST node, and the
    // signature to it.
    // 解释器在此赋予真实实现，使 Callable::call 能执行 AST 行为节点
    // （TODO:103 原先留空的环节）。声明为 inline std::function，
    // 以便在单编译单元构建中于 interpreter.hpp 直接赋值而不触发 ODR 问题。
    // Callable::call 把对象的属性表（env）、实参列表、行为 AST 节点与
    // 签名一并转发给它。
    inline std::function<InstanceListPtr(
        InstanceMap&,
        const InstanceListPtr&,
        parser::AstNodePtr,
        const CallableSign&,
        runtime::RuntimeClassPtr
    )> g_user_behavior_executor = nullptr;

    // Signature-enforcement hook.
    // 签名约束钩子。
    //
    // builtin.hpp assigns the real implementation (enforce_sign) here so that
    // call_method can reject argument-type mismatches at the call boundary
    // without a circular include into builtin.hpp. Declared as an inline
    // std::function so it can be assigned from builtin.hpp in the single-TU
    // build without ODR issues. Returns true if the call may proceed.
    // builtin.hpp 在此赋予真实实现（enforce_sign），使 call_method 能在调用
    // 边界拒收类型不符的实参，而无需循环包含 builtin.hpp。声明为 inline
    // std::function 以便单编译单元构建中由 builtin.hpp 赋值且不触发 ODR。
    // 返回 true 表示放行本次调用。
    inline std::function<bool(
        const rt_basic::CallableSign&,
        const rt_basic::InstanceListPtr&
    )> g_sign_enforcer = nullptr;

    // Behavior execution modes. Shared by Callable (which stores it) and the
    // interpreter (which maps the `->` / `~>` / `=>` arrows onto it).
    // 行为执行模式。Callable（存储它）与解释器（把 `->` / `~>` / `=>` 箭头
    // 映射到它）共用。
    enum class BehavStateOBJ {
        NORMAL,
        CONST,
        STRICT
    };

    class Callable {
        BehavStateOBJ behav_state = BehavStateOBJ::NORMAL;
        bool isConst = false;
        bool isPrivate = false;
        CallableSign sign;
        std::variant<NativeClosure, parser::AstNodePtr> self = 
            NativeClosure([](InstanceMap& env, InstanceListPtr paras){
                runtime::RuntimeObjectPtr res = nullptr;
                return InstanceListPtr(std::make_shared<std::vector<runtime::RuntimeObjectPtr>>(std::vector<runtime::RuntimeObjectPtr>({})));
            });
            

        public:

        Callable() {
            sign = CallableSign();
        }
        Callable(NativeClosure _clos, CallableSign _sign, BehavStateOBJ _state, std::pair<bool, bool> _attr = {false, false}) : self{_clos}, sign(_sign), behav_state(_state), isConst(_attr.first), isPrivate(_attr.second) {}
        Callable(parser::AstNodePtr _astn, CallableSign _sign, BehavStateOBJ _state, std::pair<bool, bool> _attr = {false, false}) : self{std::variant<NativeClosure, parser::AstNodePtr>(_astn), }, sign(_sign), behav_state(_state), isConst(_attr.first), isPrivate(_attr.second) {}

        InstanceListPtr call(InstanceMap &env, const InstanceListPtr &paras, runtime::RuntimeClassPtr selfInst = nullptr) {
            if (std::holds_alternative<NativeClosure>(this->self)) {
                NativeClosure selfbody = std::get<NativeClosure>(self);
                if (behav_state == BehavStateOBJ::STRICT) {
                    InstanceMap newe = {};
                    return selfbody(newe, paras);
                } else if (behav_state == BehavStateOBJ::CONST) {
                    InstanceMap copy = env;
                    return selfbody(copy, paras);
                }
                return selfbody(env, paras);
            } else {
                parser::AstNodePtr selfbody = std::get<parser::AstNodePtr>(self);
                // User-defined behavior (AST node): hand execution to the
                // interpreter via the g_user_behavior_executor hook (set by
                // interpreter.hpp). The object's attribute map is the behavior's
                // lexical environment (members); the interpreter combines it
                // with locals / captured scope as needed.
                // 用户定义行为（AST 节点）：经 g_user_behavior_executor 钩子
                // 交给解释器执行（由 interpreter.hpp 赋值）。对象的属性表即
                // 该行为词法环境（成员）；解释器按需再叠加局部 / 捕获作用域。
                if (g_user_behavior_executor) {
                    return g_user_behavior_executor(
                        env, paras, selfbody, sign, selfInst
                    );
                }
                return nullptr;
            }
            return nullptr;
        }

        CallableSign get_sign() const {
            return sign;
        }
        std::pair<bool, bool> get_attr() const {
            return {isConst, isPrivate};
        }
        // True when this Callable wraps a user-defined behavior AST node
        // (as opposed to a native C++ closure). Used by the runtime to decide
        // whether to record an execution-stack frame.
        // 当此 Callable 包装的是用户定义行为 AST（而非原生 C++ 闭包）时为
        // true。runtime 据此判断是否记录执行栈帧。
        bool is_user() const {
            return std::holds_alternative<parser::AstNodePtr>(self);
        }
        // The wrapped user behavior AST node (nullptr for native closures).
        // Used when rebinding a method variable at runtime so the new binding
        // re-resolves its members against `self`.
        // 所包装的用户行为 AST（原生闭包为 nullptr）。用于运行时重绑方法
        // 变量，使新绑定能针对 `self` 重新解析成员。
        parser::AstNodePtr get_astn() const {
            return std::holds_alternative<parser::AstNodePtr>(self)
                ? std::get<parser::AstNodePtr>(self) : nullptr;
        }
    };

    class ClsProto {
        InstanceMap attributes;
        CallableMap methods;

        public:
        // Authoritative, namespace-qualified type name of this prototype
        // (e.g. "Number", "io::OStream", "re::Pattern"). Set by Prototypes::
        // regcls so the runtime can resolve a concrete object's type key for
        // signature enforcement without a circular include into builtin.hpp.
        // 本原型的权威（可含命名空间）类型名，由 Prototypes::regcls 设置，
        // 使 runtime 能在不循环包含 builtin.hpp 的前提下解析对象类型标签，
        // 供签名约束使用。
        std::string name = "";

        // Optional release hook: invoked with the instance's attribute map
        // when the instance is destroyed. Native libraries use this to reclaim
        // their process-wide state (keyed by the instance id) so that long-running
        // programs do not accumulate unreclaimed entries (industrial-audit D5).
        // 可选释放钩子：实例销毁时以其属性表为参数调用。原生库借此回收其
        // 进程级状态（按实例 id 索引），使长时程序不累积未回收项（工业化审计 D5）。
        std::function<void(InstanceMap&)> on_release;

        ClsProto() = default;
        ClsProto(const ClsProtoPtr &_prototype) {
            if (_prototype) {
                attributes = _prototype->attributes;
                // Do NOT inherit Object's no-op default `::` (construct)
                // method into derived types. A derived type must fall through
                // to its own user-defined `@::` constructor, or to the
                // duck-typed fallback in call_constructor, instead of silently
                // running Object's no-op constructor — which would drop the
                // constructor arguments (e.g. `-(std::Number(5) n)` left n=0).
                // Only the Object prototype itself keeps the default `::`.
                // 不把 Object 的默认无操作 `::`（构造）方法继承给派生类型。
                // 派生类型必须落到自身的 `@::` 或 call_constructor 的鸭子式
                // 兜底，而非静默运行 Object 的无操作构造——那样会丢弃构造
                // 实参（如 `-(std::Number(5) n)` 曾令 n=0）。仅 Object 原型
                // 自己保留默认 `::`。
                for (auto& kv : _prototype->methods) {
                    if (kv.first == "::" && _prototype->name == "Object") {
                        continue;
                    }
                    methods[kv.first] = kv.second;
                }
            } else {
                Thrower.throwE("InterException", "Nullptr when build ClsProto.");
            }
        }
        ClsProto(const runtime::RuntimeClassPtr &_prototype);
        //We will define that later because RuntimeClass has only a foward declaration.
        //Normal declaration without method informations to compiler.

        // Bind / override a method (generic: system objects and user-defined
        // types are treated identically).
        // 绑定 / 覆盖方法（通用：系统对象与用户自定义类型一视同仁）。
        // `@!` (method-variable const) is recorded as a Callable attribute in
        // the signature, for the interpreter to check when user code rebinds a
        // method; the runtime layer performs no runtime guard — host-level
        // bindings (builtin bootstrapping, C++ acceptance tests) are trusted
        // operations (spec 6.2 / 2.2).
        // @!（方法变量 const）作为 Callable 属性记录在签名中，供解释器在
        // 处理用户代码的方法重绑时核查；runtime 层不做运行时守卫——宿主层
        // （builtin 引导、C++ 验收）的绑定是受信操作（文档 6.2 / 2.2）。
        void set_method(const std::string &name, const Callable &apply) {
            methods[name] = apply;
        }

        // Bind / override a member attribute (generic, no guard at the
        // runtime layer — same convention as set_method).
        // 绑定 / 覆盖成员属性（通用，runtime 层不做守卫，与 set_method 同规）。
        void set_attribute(const std::string &name, const runtime::RuntimeObjectPtr &value) {
            attributes[name] = value;
        }

        const InstanceMap& get_attributes() const {
            return attributes;
        }
        const CallableMap& get_methods() const {
            return methods;
        }
    };

    class ClassContract {
        std::vector<CallableSign> signs = {};

        public:
        ClassContract() {}
        ClassContract(const std::vector<CallableSign>& _signs) : signs(_signs) {}

        void add_sign(CallableSign _nsign) {
            signs.push_back(_nsign);
        }

        bool validate(runtime::RuntimeClassPtr _cls);
    };

} // namespace rt_basic

namespace runtime {
    class RuntimeObject {
        public:
        std::string selfname = "<Unknown>";

        RuntimeObject() = default;
        virtual ~RuntimeObject() = default;

        void give_name(const std::string &_name) {
            selfname = _name;
        }
        std::string get_etag() {
            // TODO: Add filename, object creation pos...
            return selfname;
        }
        virtual bool is_behav() {
            return false;
            // In this interpreter there is a 0% probability of a pure RuntimeObject appearing.
        }
    };

    class RuntimeClass : public RuntimeObject,
                          public std::enable_shared_from_this<RuntimeClass> {
        rt_basic::ClsProtoPtr prototype = nullptr;

        rt_basic::InstanceMap attributes;
        rt_basic::CallableMap methods;

        public:

        // v1.30 const state: `obj.#()` freezes the object. Setting it is the
        // only operation `#` offers and it can never be undone — there is no
        // way out of the const state once an object is frozen.
        // v1.30 常数状态：`对象.#()` 冻结该对象。`#` 只提供「设置」这一操作，
        // 且永不可撤销——对象一旦冻结便无法退出常数状态。
        bool const_state = false;

        // v1.30 private attributes, added at runtime by
        // `obj:-(Type v) << init;`. A private attribute is visible only to the
        // object itself: reading or writing it is allowed only while one of the
        // object's own methods runs (self == this). Everything else must go
        // through a method injected with `obj:@method << [behavior];`.
        // v1.30 私有属性，由 `对象:-(类型 变量) << 初值;` 在运行期添加。
        // 私有属性仅对对象自身可见：只有在该对象自身的方法运行时
        //（self == this）才允许读写。其余场合必须经
        // `对象:@方法 << [行为];` 注入的方法进行。
        std::unordered_set<std::string> private_attrs;

        // Set when any method of this instance is (re)bound at runtime
        // (e.g. via `obj.method.=(beh)` / `obj.method << beh`). Exposed to
        // the `Checker` standard library as `has_changed()`.
        // 当本实例的任一方法在运行时被（重）绑定（如 `obj.method.=(beh)` /
        // `obj.method << beh`）时置位，供标准库 `Checker` 的 `has_changed()`
        // 读取。
        bool methods_dirty = false;

        bool is_const_state() const { return const_state; }
        // Freeze the object. Idempotent by design: there is no clearing API.
        // 冻结对象。设计为幂等：不提供任何解除接口。
        void set_const_state() { const_state = true; }

        void set_private_attr(const std::string& name) {
            private_attrs.insert(name);
        }
        bool is_private_attr(const std::string& name) const {
            return private_attrs.find(name) != private_attrs.end();
        }

        RuntimeClass(rt_basic::ClsProtoPtr _prototype) : prototype(_prototype) {
            if (_prototype) {
                attributes = prototype->get_attributes();
                methods = prototype->get_methods();
            } else {
                Thrower.throwE("InterException", "Nullptr when build RuntimeClass.");
            }
        }

        // Instance teardown: fire the prototype's release hook (if any) so native
        // libraries can free their process-wide state keyed by this instance.
        // Runs only the instance's own attribute map, which is still alive during
        // the destructor body. Built-in types leave `on_release` unset, so this
        // is a cheap null-check for them (industrial-audit D5).
        // 实例析构：触发原型的释放钩子（若有），供原生库回收按本实例索引的
        // 进程级状态。仅使用实例自身仍存活的属性表。内置类型不设置
        // `on_release`，故对它们只是一个廉价的空指针检查（工业化审计 D5）。
        ~RuntimeClass() {
            if (prototype && prototype->on_release) {
                prototype->on_release(attributes);
            }
        }

        // Same convention as ClsProto::set_method: generic binding, no @!
        // guard at the runtime layer. 与 ClsProto::set_method 同规：通用绑定，不做 @! 守卫。
        void set_method(const std::string &name, const rt_basic::Callable &apply) {
            methods[name] = apply;
        }
        void set_attribute(const std::string &name, const RuntimeObjectPtr &value) {
            attributes[name] = value;
        }

        // Prototype identity accessor: selfname may be renamed by
        // Runtime::defobj, so interpreter-side type checks need the
        // authoritative prototype pointer.
        // 原型身份访问器：selfname 会被 Runtime::defobj 重命名，
        // 解释器侧的类型身份核查等场合需要权威的原型指针。
        rt_basic::ClsProtoPtr get_prototype() const {
            return prototype;
        }

        // Generic method dispatch: look up by name and invoke. Argument
        // count/type constraints, as well as @! (no rebind) / @# (private)
        // access control, are all carried by the signature (CallableSign)
        // and Callable attributes, and are checked by the compiler /
        // interpreter before the call — the runtime layer performs no
        // runtime guard and only dispatches. The only runtime error here
        // is a method that does not exist (a poison object is published).
        // 方法派发（通用）：按名查找并调用。参数的数量与类型
        // 约束、@!（不可重绑）与 @#（私有）的访问控制均由签名
        // （CallableSign）与 Callable 属性承载，编译器 / 解释器
        // 在调用前核查——runtime 层不做运行时守卫，只负责派发。
        // 唯一运行时错误：方法本身不存在（毒水公布）。
        rt_basic::InstanceListPtr call_method(const std::string &name, rt_basic::InstanceListPtr para) {
            auto found = methods.find(name);
            if (found == methods.end()) {
                // Immediate, duck-typed error: a missing method is a hard
                // failure, not a silently-propagating poison object (the
                // poison-water model is retired).
                // 即时、鸭子式错误：方法缺失是硬性失败，而非静默传播的
                // 毒水对象（毒水模型已退役）。
                Thrower.throwE("RuntimeException",
                    "Method '" + name + "' not found.", {get_etag()});
                return {}; // unreachable: Thrower.throwE terminates the process
            }
            // Runtime signature enforcement: if an enforcer is installed,
            // reject argument-type mismatches at the call boundary before
            // invoking the body. A violation is now an immediate, duck-typed
            // runtime error — no more silent poison propagation.
            // 运行时签名约束：若已安装约束器，在调用体前于调用边界拒收
            // 类型不符的实参。约束违犯现为即时的鸭子式运行时错误
            //（不再静默传播毒水）。
            //
            // Record an execution-stack frame for user-defined (AST) methods
            // so runtime errors surface a real call chain with line numbers
            // instead of placeholder strings like "<Interpreter>".
            // 为用户定义（AST）方法记录执行栈帧，使运行时错误呈现带行号
            // 的真实调用链，而非 <Interpreter> 之类的占位符。赋值 / 流协商
            // 符（= =: :=）不计入，以保持栈可读。
            bool pushed_frame = false;
            if (found->second.is_user()
                    && name != "=" && name != "=:" && name != ":=") {
                diag::call_stack().push_back({
                    (name == "::")
                        ? "in '@::' (constructor)"
                        : ("in method '" + name + "'"),
                    diag::source_file(),
                    diag::cur_locus().line,
                    diag::cur_locus().col
                });
                pushed_frame = true;
            }
            if (rt_basic::g_sign_enforcer) {
                rt_basic::g_sign_enforcer(found->second.get_sign(), para);
            }
            auto res = found->second.call(this->attributes, para, shared_from_this());
            if (pushed_frame) diag::call_stack().pop_back();
            return res;
        }

        const rt_basic::InstanceMap& get_attributes() const {
            return attributes;
        }
        // Non-const overload: the interpreter needs a mutable reference to the
        // object's attribute map when running a constructor / method that
        // writes members.
        // 非常量重载：解释器在运行会写入成员的构造 / 方法时需要可变的
        // 属性表引用。
        rt_basic::InstanceMap& get_attributes() {
            return attributes;
        }
        const rt_basic::CallableMap& get_methods() const {
            return methods;
        }

        bool is_behav() {
            return false;
        }
    };

    class RuntimeBehavior : public RuntimeObject {
        rt_basic::Callable body;

        public:

        RuntimeBehavior(rt_basic::Callable _body) : body(_body) {}

        rt_basic::InstanceListPtr call(rt_basic::InstanceMap& env, rt_basic::InstanceListPtr para) {
            return body.call(env, para);
        }

        bool is_behav() {
            return true;
        }
        parser::AstNodePtr get_astn() const {
            return body.get_astn();
        }
    };

    class Prototypes {
        std::unordered_map<std::string, rt_basic::ClsProtoPtr> protos;

        public:
        Prototypes() {}

        void operator+=(const Prototypes& oth) {
            for (auto p : oth.protos) {
                protos[p.first] = p.second;
            }
        }

        void regcls(const std::string &name, const rt_basic::ClsProtoPtr & _proto) {
            protos[name] = _proto;
            // Stamp the authoritative type name onto the prototype so that
            // signature enforcement can resolve an object's type key.
            // 把权威类型名烙印到原型上，供签名约束解析对象类型标签。
            if (_proto) _proto->name = name;
        }
        rt_basic::ClsProtoPtr getcls(const std::string &name) {
            if (protos.find(name) == protos.end()) {
                return nullptr; // WARN: Throwing Task will give to Interpreter;
            }
            return protos[name];
        }
    };

    class Runtime {
        Prototypes protos;
        std::unordered_map<std::string, RuntimeObjectPtr> objs;

        public:
        Runtime(){}
        Runtime(Prototypes _protos) : protos(_protos) {}

        void add_protos(Prototypes _protos) {
            protos += _protos;
        }
        void defobj(const std::string &name, RuntimeObjectPtr _obj) {
            _obj->give_name(name);
            objs[name] = _obj;
        }
        RuntimeObjectPtr getobj(const std::string &name) {
            if (objs.find(name) == objs.end()) {
                return nullptr; // WARN: Throwing Task will give to Interpreter;
            }
            return objs[name];
        }

        // Public prototype lookup (used by the interpreter when resolving a
        // class's base type / contract parent). Returns nullptr if the type
        // is not registered. Falls back to the unqualified name after the
        // last "::" so that source written as `std::Number` / `io::OStream`
        // still resolves to the prototypes registered as `Number` / `OStream`.
        // 公开的原型查找（解释器解析类的基类 / 约束父类型时使用）。
        // 未注册类型返回 nullptr。若全称查找失败，回退到最后一个 "::"
        // 之后的非限定名，使源码中的 `std::Number` / `io::OStream` 仍能
        // 解析到注册为 `Number` / `OStream` 的原型。
        rt_basic::ClsProtoPtr getcls(const std::string &name) {
            auto p = protos.getcls(name);
            if (p) return p;
            // rfind("::") locates the last two-character SEPARATOR.
            // find_last_of() takes a character SET, so find_last_of("::") only
            // ever matches a single ':' — it yields the right answer for
            // "ns::Type" by luck, but mis-strips lone-colon names ("Type:"
            // would collapse to an empty string). rfind is the correct API
            // for the intent, and the offset becomes +2 (skip both colons).
            // rfind("::") 定位最后一个双字符分隔符。find_last_of 取的是
            // 「字符集」，故 find_last_of("::") 只会匹配单个 ':'——对
            // "ns::Type" 只是碰巧得到正确结果，却会错切单冒号名（"Type:"
            // 会被切成空串）。rfind 才是表达此意图的正确接口，偏移相应为
            // +2（跳过两个冒号）。
            auto pos = name.rfind("::");
            if (pos != std::string::npos) {
                return protos.getcls(name.substr(pos + 2));
            }
            return nullptr;
        }

        // Generic instantiation: create an object by a registered type name
        // (spec chapter 1: a type *is* a registered prototype). System objects
        // and user-defined types are treated identically — builtin registers
        // std::Object / Number / ..., user code registers its own types, and
        // make can create any of them.
        // 通用实例化：按注册的类型名创建对象（文档第一章：
        // 类型即注册的原型）。系统对象与用户自定义类型一视
        // 同仁——builtin 把 std::Object / Number / ... 注册进来，
        // 用户代码把自己定义的类型注册进来，make 都能创建。
        RuntimeObjectPtr make(const std::string &type_name) {
            auto proto = getcls(type_name);   // full name, then unqualified
            if (!proto) {
                return nullptr; // WARN: unregistered type -> interpreter throws
                                // 未注册类型，交解释器抛出
            }
            auto instance = std::make_shared<RuntimeClass>(proto);
            instance->give_name(type_name);
            return instance;
        }
        // Instantiate from a known prototype pointer (low-level: used
        // internally by builtin convenience factories such as make_number;
        // same code path as make).
        // 按原型指针实例化（底层：已知原型的场合，如 builtin
        // 的 make_number 等便捷工厂内部调用，与 make 同一通路）。
        RuntimeObjectPtr make(const rt_basic::ClsProtoPtr &proto) {
            if (!proto) {
                return nullptr;
            }
            return std::make_shared<RuntimeClass>(proto);
        }
    };
} // namespace runtime

// Guard comment: this definition is placed here to avoid redefining
// class RuntimeClass (circular include resolution).
// 说明：此定义放在此处以避免重复定义 class RuntimeClass（循环依赖解法）。
namespace rt_basic {

    inline ClsProto::ClsProto(const runtime::RuntimeClassPtr &_prototype) {
        if (_prototype) {
            attributes = _prototype->get_attributes(); // Safely!
            methods = _prototype->get_methods();       // Safely!
        } else {
            Thrower.throwE("InterException", "Nullptr when build ClsProto.");
        }
    }


    // Validate that the given class implements every sign in this contract
    // (name + in/out parameter types must match exactly). Used by the
    // interpreter when a type claims to satisfy a constraint.
    // 校验给定类是否实现了本约束中的全部签名（方法名与输入输出参数
    // 类型须完全一致）。解释器在“某类型声明满足某约束”时调用。
    bool ClassContract::validate(runtime::RuntimeClassPtr _cls) {
        auto methods = _cls->get_methods();
        for (auto sign : signs) {
            auto res = methods.find(sign.name);
            if (res == methods.end()) {
                return false;
            }
            const auto& have = res->second.get_sign();
            if (have.name != sign.name) {
                return false;
            }
            // Compare parameter TYPES only, not names. Contract signatures
            // list types (e.g. `(std::Number, std::Number)`) while a concrete
            // implementation is free to name its parameters
            // (e.g. `(std::Number x, std::Number y)`). Comparing names would
            // make every real method fail to satisfy its contract, so we
            // match on arity + types instead.
            // 仅比较参数「类型」，不比较名称。契约签名只列类型，
            // 而具体实现可自由命名参数；若比较名称，则任何真实方法都无法
            // 满足契约，故改按「数量 + 类型」匹配。
            if (have.inpara.size() != sign.inpara.size()) {
                return false;
            }
            for (std::size_t i = 0; i < sign.inpara.size(); ++i) {
                if (have.inpara[i].second != sign.inpara[i].second) {
                    return false;
                }
            }
            if (have.outpara.size() != sign.outpara.size()) {
                return false;
            }
            for (std::size_t i = 0; i < sign.outpara.size(); ++i) {
                if (have.outpara[i].second != sign.outpara[i].second) {
                    return false;
                }
            }
        }
        return true;
    }

} // End of rt_basic namespace (Circular dependency resolution)

#endif