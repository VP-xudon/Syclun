# Synth OOP Language Documentation
**Version: v1.29**

> This is the official language documentation for Synth OOP, fully covering all syntax features up to the current version. This document is written entirely in clear, accessible, and friendly language. After reading it, you'll be able to write any valid Synth OOP program. This document is intended both for **compiler developers** and for anyone who wishes to learn more about the language: the syntax described herein takes precedence, and Appendix D provides compiler acceptance test cases.

> **⚠ Implementation-status note (v1.27):** Chapter XI, "The Poisoned Water Model and `_case`," describes a "poisoned water model" and a `_case` exception-catching method that **no longer exist in the current interpreter**. Errors are now raised **immediately at their source** and reported in a **g++-style diagnostic** (`file:line:col: error: Type: message`, with the offending source line, a `^~~~` caret, and a full execution stack). Runtime legality checks use the `&assert;` standard library's `Checker` object (`has_method` / `has_changed`). Chapter XI is retained only as historical design record — do not write new programs against it.

## The Reference Implementation: Syclun

This document specifies the **Synth-OOP** language (also written *Synth OOP*). The current reference implementation of that language is named **Syclun**.

**Syclun** is short for **Sy**nth-OOP **I**nterpreter **M**ade **o**f **C**PP **L**ang**u**age — an interpreter for the Synth-OOP language, written in C++. It is pronounced /ˈsɪklən/ (roughly "SIK-lun"). The letters come from that phrase: **Sy** ← **Sy**nth-OOP, **c** ← **C**PP (i.e. C++), **l** and **u** ← **L**ang**u**age, **n** ← I**n**terpreter.

Points that are easy to get wrong:

1. **Syclun is the interpreter, not the language.** The language is **Synth-OOP**; this document is its specification, independent of which language any implementation is written in. A different implementation would still be implementing Synth-OOP.
2. **CPP here means C++, not the C preprocessor.** The abbreviation *CPP* more commonly denotes the *C PreProcessor* (and the `.cpp` extension). Here it stands for **C++** — Syclun is entirely C++23 and depends on nothing but the standard library.
3. **It is a blend, not a strict acronym.** It was coined for pronounceability; do not expand it letter by letter.
4. **`synth` (the executable) ≠ Synth-OOP (the language).** Syclun builds an executable named `synth` (`build/synth.exe`). Synth-OOP source files use the extensions `.syn` (program), `.synl` (library interface), `.syni`.
5. **Appendix E records the current implementation, not the specification.** Appendix E describes the semantics Syclun actually adopted, including places where it narrows or clarifies earlier chapters. Where they conflict, Appendix E wins — but that is the state of the implementation, not necessarily the future direction of the language.

Related documents:

- Standard-library method signatures and the "how to add a library" checklist: [`Syclun标准库参考.md`](./Syclun标准库参考.md)
- Project introduction and the 30-second tour: [`../README.md`](../README.md)

## Revision History
- **v1.27**: **Retired the "Poisoned Water Model" and `_case`.** Errors are now raised immediately at the source and reported in a g++-style diagnostic (file:line:col + source caret + execution stack); runtime legality checks moved to the `&assert;` `Checker` (`has_method` / `has_changed`). `io` EOF reads and native-library bad input now raise an immediate `RuntimeException` instead of gracefully degrading to poison. Unit-test counts updated to 83 / 63 / 139.
- **v1.28**: **Retired the `void` keyword and made constraints runtime-enforced.** `void` is no longer a type / name / output / constraint token; writing it in any of those positions is now a syntax error — use an **empty `()`** for "no value" (e.g. `[() -> () {…}]` publishes nothing). `#Contract` definitions are validated by `ClassContract::validate` (compared by type, not by parameter name), and parameter (`x[Contract]`) / variable (`-(T[Contract] v)`) constraints are checked with `check_constraint` whenever the bound value is known (parameter at behavior entry; variable at declaration and after a flow `<<`). See `verify/philosophy/constraint_demo.syn`.
  - Added Appendix E, "Language Semantics", collecting the implementation-level notes moved out of `README.md`.
- **v1.29**: **Switched container/string indexing to 0-based and added the `sugar` standard library.** `Array`/`Tuple`/`String` `get`/`insert`/`remove` (and `String.slice`) now use 0-based indices (`arr.get(0)` is the first element). A new C++-backed `sugar` library provides `Infix`, an arithmetic-expression evaluator: `-(sugar::Infix("1+(2-3)*(3+5)") e); e.parse()` → `-7`; bind variables from a `std::Dict` via `e.env(dict)`; an unresolved variable raises a runtime error attributed to `parse`. The build is CMake-driven via `bash build.sh` (`bash build.sh --test` runs the full ctest suite: lexer / parser / runtimes).
- **v1.30**: **Fatal errors now exit non-zero; corrected the Chapter IX "checking timing" wording.** Fatal errors (syntax errors, runtime errors, constraint violations, …) now terminate the process with exit status `1` instead of `0`. Previously `synth bad.syn && cmd` ran `cmd` even after a failure, and CI could not detect it — which contradicted the project's "immediate, traceable errors" commitment. `&&`, `set -e`, and CI now see the failure correctly, while a successful run still returns `0`. Also corrected §9.8/§9.9 in the Chinese edition: the constraint "roll call" happens at **runtime** (the heading and a note previously said "compile time"). Added an exit-code assertion: `assert_parser` grows to 64 tests, one of which verifies that a rejected source **must** terminate with a non-zero status.
- **v1.26**: Removed the "contract" concept; added dedicated chapters for Constraints and the Poisoned Water Model.
  - Removed all "contract" wording: the three arrow levels are uniformly called behavior patterns (zero-side-effect / constant / non-constant pattern); the arrow itself is the "behavior-pattern arrow".
  - Chapter V renamed to "Behaviors and Their Patterns".
  - Constraints extracted from Chapter I into a new dedicated Chapter IX, expanded with "Constraints vs Classes" and "Checking Timing".
  - The Poisoned Water Model and `_case` became Chapter XI, expanded (poison vs negotiation degradation; the pattern choice of `_case` handler behaviors); former Chapters IX and X shifted to X and XI.
  - Added a "three kinds of immutability" comparison table; smoothed awkward explanations and strengthened links between similar concepts.
  - Explained why assignment can reuse the stream syntax: the reception function `:=` defaults to assigning itself directly from a same-typed value, and the publication function `=:` cooperates; the `std::Object` method table gained an assignment method `=` (`a.=(b)` equivalent to streaming assignment), mirrored in Appendix A.
  - Clarified that `void` is NOT a keyword: the language philosophy forbids empty return values, so an output parameter named `void` is recognized and auto-discarded by the system.
  - Updated in sync with the Chinese edition.
- **v1.25**: Copy-editing and structural polish.
  - Fixed a broken Markdown bold in Example 3 and a bare `ClassName()` identifier.
  - Restructured Revision History into bullet sub-items.
  - De-duplicated repeated explanations (the dot-vs-method-name note, the `std::Float` clarification).
  - Added a Terminology section; unified section headings to Title Case.
  - Reduced colloquial/literary wording and first-person voice.
  - Added backticks to code identifiers; added Appendix links in the Table of Contents.
- **v1.24**: Comprehensive syntax and logic review.
  - Removed the standalone specification file (its content is now incorporated into this document).
  - Converted all infix comparisons (`a > b`) into method calls (`a.>(b)`); likewise for infix arithmetic in comments.
  - Expanded Section 5.5 to cover comparison operators (`<` `>` `<=` `>=` `==` `!=`).
  - Unified terminology: "integer" → "number"; "function" → "method/behavior".
  - Removed a leftover unused variable in the `while_` example.
- **v1.23**: Section 2.3.2 rewritten as method-by-method function descriptions.
  - Replaced the concise method table with one code block per native object.
  - Each method is now described with Synth OOP's method-definition syntax and a function comment.
- **v1.22**: Removed `std::Float` remnants; clarified `std::Number` semantics.
  - `std::Number` now uniformly carries all numeric values; `std::Float` is replaced.
  - Added Section 2.3 "Built-in Methods of Native Objects".
  - Clarified in Section 2.3.1 that `std::Object` uniformly defines `::` / `~` / `=:` / `:=` / `_case` as const.
  - The former "The program itself is also an object" becomes Section 2.4.
  - Added a Section 6.3 note; fixed a stray leading quote in Section 3.1.
- **v1.21**: Renamed the native object type; redesigned `while_` / `repeat_`.
  - `std::Integer` uniformly renamed to `std::Number`.
  - `while_` now takes a loop-body behavior plus a condition-check behavior (condition-check after body).
  - `repeat_` is now a `std::Number` method whose value is the loop count.
  - Added the "behavior qualifier `@`" mechanism.
- **v1.20**: Fixed remaining "method exists independently of a class" issues.
  - Converted floating `@methodName << ...` definitions into class methods or behavior literals.
  - Replaced the free-function-call syntax `@name(args)` with instance method calls.
  - Fixed poison-water examples, the constraint example, and redundant inherited members.
  - Cleaned up terminology remnants (`const closure` → `const behavior`, etc.).
- **v1.19**: Clarified method binding and object instantiation.
  - Clarified in Section 6.1 that methods cannot exist independently of a class.
  - Clarified in Section 4.1 that instantiation must be `-(prototype-name variable-name)`.
  - Completed every bare `-(name)` declaration with its typed form.
- **v1.18**: Terminology unification and clarifications.
  - Replaced "closure" with "behavior"; named the arrow inside behaviors the "behavior pattern".
  - Clarified that the method name has no "dot" (`.` is only the call operator).
  - Added the constant-pattern implementation clarification.
  - Unified code block indentation to four spaces.
## Table of Contents
1. [Before We Begin: What Synth OOP Wants to Become](#i-before-we-begin-what-synth-oop-wants-to-become)
2. [Everything is an object](#ii-everything-is-an-object)
3. [Streaming Statements](#iii-streaming-statements)
4. [Object Instantiation Statements (Expressions)](#iv-object-instantiation-statements-expressions)
5. [Behaviors and Their Patterns](#v-behaviors-and-their-patterns)
6. [Methods](#vi-methods)
7. [Control Flow: if_, while_, repeat_](#vii-control-flow-if_-while_-repeat_)
8. [Class](#viii-class)
9. [Constraints](#ix-constraints)
10. [Module Import Statements and Namespaces](#x-module-import-statements-and-namespaces)
11. [The Poisoned Water Model and _case: Error Propagation and Rescue](#xi-the-poisoned-water-model-and-_case-error-propagation-and-rescue) **(retired — see v1.27 note)**
- [Appendix A: Syntax Symbol Quick Reference](#appendix-a-syntax-symbol-quick-reference)
- [Appendix B: On Computational Capability](#appendix-b-on-computational-capability)
- [Appendix C: Comparison Examples — Synth OOP vs C++](#appendix-c-comparison-examples--synth-oop-vs-c)
- [Appendix D: Compiler Self-Test Examples (Acceptance Tests)](#appendix-d-compiler-self-test-examples-acceptance-tests)
- [Appendix E: Language Semantics (Implementation Notes for v1.28)](#appendix-e-language-semantics-implementation-notes-for-v128)

## Terminology

The following terms have canonical spellings used consistently throughout this document:

| Term | Canonical Form | Notes |
| ---- | -------------- | ----- |
| behavior pattern | `behavior pattern` | The umbrella term for the three levels `=>` / `~>` / `->`; the arrow itself is the "behavior-pattern arrow" |
| zero-side-effect pattern | `zero-side-effect pattern` | The `=>` level: fully isolated, no side effects |
| constant pattern | `constant pattern` | The `~>` level: read-only access to the external environment (also called the read-only pattern) |
| non-constant pattern | `non-constant pattern` | The `->` level: read-write access to the external environment |
| Poisoned Water Model | `Poisoned Water Model` (retired) | Historical error-propagation mechanism (see v1.27 note); the current implementation uses immediate errors + execution stack instead |
| behavior | `behavior` | The language's term for a closure/function |
| method | `method` | A behavior bound to a class/object |
| constraint | `constraint` | Also called an interface (a method checklist); see Chapter IX |

## I. Before We Begin: What Synth OOP Wants to Become
In a nutshell: Grant the freedom of duck typing during compilation, while providing the safety of static typing at runtime.
Breaking it down:
- **When writing code**, you don't need to sprinkle type annotations everywhere or battle the duck typing negotiation mechanism — data flows wherever you want, effortlessly;
- **When running**, every data flow goes through strict protocol validation. If something goes wrong, it won't crash or produce "undefined behavior" — it degrades gracefully.
This "freedom + safety" is supported by five design principles: everything is an object, streaming polymorphism and intelligent negotiation, behavior patterns and constraints, zero-value rule and physical barrier, and AI-cooperation-ready design. All syntax described later is the concrete implementation of these five principles.
### Duck Typing and Constraint System

Synth OOP uses **duck typing**: whether an object is of a certain "type" depends not on its declaration but on whether it has the required methods.

#### Why There Is No Strict Typing

In Synth OOP, **the implementation of an object's method is a variable** — the variable holds a behavior. That is, methods can be arbitrarily modified and replaced. Since methods can be replaced at will, the concept of "type" becomes meaningless — **each object is its own type**.

#### Immutability of System Native Objects

Although user-defined objects' methods can be arbitrarily modified, **the methods of system-provided native objects (such as std::Number, std::String, std::Array, etc.) are const behaviors** — they are immutable. Therefore, you can create objects that are identical to system native objects and immutable based on this model.

#### Duck Constraints

Although Synth OOP uses duck typing, you can still explicitly declare what methods an object must have through **constraints**. A constraint is a "method checklist" (the role that other languages usually call an **interface**): any object that has every method signature on the checklist satisfies the constraint. This is fully in line with duck typing's idea of "recognizing objects by behavior" — it merely turns an implicit expectation into an explicit, compiler-checked requirement. Remember it in one sentence: **duck typing means you don't have to attach labels; constraints mean labels are there whenever you want them**.

> 💡 **A clarification for beginners: there is no "dot" in a method name — the dot is just the call operator**
> You'll often see `a.+(b)` in this document and may assume the method name is "`.+`" (dot-plus). It isn't: **the method name itself is just the symbol `+`**. That "dot" is the **call operator** — it means "call the method on this object". `a.+(b)` breaks down as: `a` (object) `.` (call) `+` (method name) `(b)` (arguments). So you declare a method as `@+ << ...` (the method name is `+`), and call it as `a.+(b)`. This is the same `.` you'd use in `a.add(b)` in other languages — the dot is always just the "call" action, never part of the method name.

The complete syntax of constraints — how to define them, using a class name directly as a constraint, constraint inheritance, and the behavior qualifier `@` inside the type-qualifier brackets `[]` — is covered in detail in Chapter IX.

Tip: If you've worked with C++, Java, or Python before, you'll find that Synth OOP absorbs the best of many languages while making some bold innovations. Don't worry — this document starts from zero, so no prior knowledge is required.
## II. Everything is an object
### 2.1 No "primitive types"
In many languages, `int` and `bool` are special "primitive types" that must be wrapped to become objects. Synth OOP doesn't make this binary distinction — **from numbers to business logic, everything is inherently an object**:

| Concept | Synth OOP Type |
| ---- | ---- |
| Number | std::Number |
| Boolean | std::Boolean |
| String | std::String |
| Array | std::Array |
| Dictionary | std::Dict |
| Tuple | std::Tuple |
| The Source of All Things | std::Object |

`std::` is a namespace — essentially the "last name" of these types, indicating they all belong to the Synth family.
The direct benefit of everything being an object: **All data inherently possesses behavioral capabilities**. Any piece of data can call methods, be chained together, or have behaviors injected into it — there's no gap between data and methods.
### 2.2 Native Objects: The foundation implemented by the compiler
In Synth OOP, all objects are native objects — they are not constructed from libraries but rather are directly built into the compiler as fundamental building blocks. This means there's no distinction in the language between "standard library types" and "native types"; every object is implemented directly by the compiler itself.
The complete list of native objects is as follows:

| Native Object | Description |
| ---- | ---- |
| std::Number | Number (numeric value), the language's built-in numeric object |
| std::Boolean | Boolean value, representing true or false |
| std::String | String, an immutable sequence of characters |
| std::Array | Array, equivalent to C++'s `std::vector`, dynamically resizable and modifiable |
| std::Dict | Dictionary, a data container with key-value pairs, supporting dynamic addition and removal of key-value pairs |
| std::Object | The root class for all types, serving as the base class for every other object |
| io::OStream | Output stream, equivalent to C++'s `ostream`, providing standard output capabilities |
| io::IStream | Input stream, equivalent to C++'s `istream`, providing standard input capabilities |
| std::Tuple | Tuple, an immutable sequence of values arranged by position, used for multiple return values and structured binding |

Note: `io::OStream` and `io::IStream` cannot be used directly as objects; you must first instantiate them (declare variables) before you can call their methods or use them in stream operations.

> ⚠️ This section is intended for compiler developers: all the native objects mentioned above are part of what the compiler needs to implement, not ordinary class libraries. See the next section (2.3) for their built-in method specifications.

### 2.3 Built-in Methods of Native Objects

All methods of native objects are **implemented directly by the compiler** (not library functions). The following lists their built-in methods (method name + parameters + function) for compiler developers to reference. For the method-definition syntax, see Section 6.1; the type-qualifier square brackets `[]` may contain the behavior qualifier `@` (see Chapter IX).

#### 2.3.1 The Root `std::Object` and Common Mechanisms

Any class that does not explicitly declare a parent class inherits from `std::Object` by default. Therefore, the constructor `::`, destructor `~`, publication function `=:`, reception function `:=`, assignment method `=`, are all defined on `std::Object`, and marked as **immutable** with `@!` (method variable const):

```text
$Object {
    @!:: << [() ~> () {}];                      // Constructor: called on object creation
    @!~ << [() ~> () {}];                       // Destructor: called on object destruction
    @!=: << [() ~> (result) { /* publish self value */ }];  // Publication: sender in a stream statement; by default cooperates with the receiver and publishes self as-is
    @!:= << [(value) -> () { /* receive value */ }];     // Reception: receiver in a stream statement; when value's type matches self, by default assigns self directly from value (the underlying source of streaming assignment)
    @= << [(value) -> () { /* assign self from value */ }]; // Assignment: a.=(b), equivalent to the a << b assignment usage
}
```

> **Note (v1.27):** Historically `std::Object` also defined an exception-handling method `_case` and an implicit `error` attribute (the "poison marker") used to propagate errors along the data flow. These are **retired** in the current implementation: errors are now raised immediately at the source and reported in a g++-style diagnostic (with execution stack), and runtime legality checks use the `&assert;` `Checker` (`has_method` / `has_changed`).

#### 2.3.2 Built-in Methods by Native Object (method-by-method function description)

Below, each native object's built-in methods are described one by one using Synth OOP's own method-definition syntax. The comment inside each method's braces describes the **specific function** it must implement.

**`std::Number` (number, numeric value type)**

```text
$Number {
    @+ << [(std::Number other) => (std::Number result) {
        // Addition: result = self.+(other)
    }];
    @- << [(std::Number other) => (std::Number result) {
        // Subtraction: result = self.-(other)
    }];
    @* << [(std::Number other) => (std::Number result) {
        // Multiplication: result = self.*(other)
    }];
    @/ << [(std::Number other) => (std::Number result) {
        // Division: result = self./(other); per IEEE 754, dividing by zero yields Infinity/NaN (the historical "poison" fallback is retired)
    }];
    @% << [(std::Number other) => (std::Number result) {
        // Modulo: result = self.%(other)
    }];
    @< << [(std::Number other) => (std::Boolean result) {
        // Less than: result = (self.<(other))
    }];
    @> << [(std::Number other) => (std::Boolean result) {
        // Greater than: result = (self.>(other))
    }];
    @<= << [(std::Number other) => (std::Boolean result) {
        // Less than or equal: result = (self.<=(other))
    }];
    @>= << [(std::Number other) => (std::Boolean result) {
        // Greater than or equal: result = (self.>=(other))
    }];
    @== << [(std::Number other) => (std::Boolean result) {
        // Equal: result = (self.==(other))
    }];
    @!= << [(std::Number other) => (std::Boolean result) {
        // Not equal: result = (self.!=(other))
    }];
    @to_string << [() => (std::String result) {
        // Convert self to a decimal string representation
    }];
    @repeat_ << [(body) -> (value) {
        // Loop: self is the loop count (Number), body is the loop-body behavior;
        // body is like [(state) -> (state)], its parameter format must equal its return format;
        // value is the value returned by the last execution of body
    }];
}
```

**`std::Boolean` (boolean value)**

```text
$Boolean {
    @if_ << [(true_branch, false_branch) -> (value) {
        // Conditional branch: execute true_branch when self is true, otherwise false_branch;
        // value is the output of the executed branch, both branches must output the same type
    }];
    @while_ << [(body, condition_check) -> (value) {
        // Loop: self is the initial condition (Boolean), body is the loop-body behavior, condition_check is the condition-check behavior (after body);
        // body is like [(state) -> (state)], its parameter format must equal its return format;
        // condition_check is like [(state) ~> (flag)], its parameter format must equal body's return format;
        // value is the value returned by the last execution of body
    }];
}
```

Parameter notes: `true_branch`, `false_branch`, `body`, and `condition_check` are all **behaviors**. `repeat_` does not belong here — it is a method of `std::Number`.

**`std::String` (string, immutable character sequence)**

```text
$String {
    @+ << [(std::String other) => (std::String result) {
        // Concatenation: result = self.+(other), returns a new string (original unchanged)
    }];
    @upper << [() => (std::String result) {
        // To uppercase, returns a new string
    }];
    @lower << [() => (std::String result) {
        // To lowercase, returns a new string
    }];
    @reverse << [() => (std::String result) {
        // Reverse character order, returns a new string
    }];
    @length << [() => (std::Number result) {
        // Return the character count
    }];
    @get << [(std::Number index) => (std::String result) {
        // Return the index-th character (index starts from 0)
    }];
    @contains << [(std::String sub) => (std::Boolean result) {
        // Whether self contains substring sub
    }];
    @slice << [(std::Number start, std::Number end) => (std::String result) {
        // Return the substring in the [start, end) range
    }];
}
```

**`std::Array` (array, equivalent to C++'s `std::vector`, dynamically resizable)**

```text
$Array {
    @push_back << [(value) -> () {
        // Append element value at the end (like vector::push_back)
    }];
    @get << [(std::Number index) ~> (value) {
        // Get an element by index, index starts from 0; may also be named query
    }];
    @size << [() ~> (std::Number result) {
        // Return the element count
    }];
    @pop_back << [() -> () {
        // Remove the last element
    }];
    @remove << [(std::Number index) -> () {
        // Delete the index-th element
    }];
    @insert << [(std::Number index, value) -> () {
        // Insert value at position index, shifting subsequent elements
    }];
    @clear << [() -> () {
        // Remove all elements
    }];
    @front << [() ~> (value) {
        // Return the first element
    }];
    @back << [() ~> (value) {
        // Return the last element
    }];
}
```

> `push_back` is a **formal built-in method** of Array (Array is equivalent to `std::vector`, dynamically resizable), not an abolished method.

**`std::Dict` (dictionary, key-value structure with dynamic add/remove)**

```text
$Dict {
    @get << [(key) ~> (value) {
        // Get a value by key; returns the zero value of that type when the key is absent (the historical "poison" fallback is retired)
    }];
    @set << [(key, value) -> () {
        // Set a key-value pair; overwrite if the key already exists
    }];
    @remove << [(key) -> () {
        // Delete the key-value pair for key
    }];
    @has << [(key) ~> (std::Boolean result) {
        // Whether key exists
    }];
    @size << [() ~> (std::Number result) {
        // Return the number of key-value pairs
    }];
    @keys << [() ~> (std::Array result) {
        // Return an array of all keys
    }];
    @values << [() ~> (std::Array result) {
        // Return an array of all values
    }];
}
```

**`std::Tuple` (tuple, immutable value sequence by position)**

```text
$Tuple {
    @get << [(std::Number index) ~> (value) {
        // Get an element by index, index starts from 0
    }];
    @make << [(elements) => (std::Tuple result) {
        // Construct a tuple from elements, where elements are arbitrary objects in positional order
    }];
    @size << [() ~> (std::Number result) {
        // Return the element count
    }];
}
```

> A tuple's **literal initialization** `-(std::Tuple t) << (10, "Alice", true)` and **structural decomposition** `-(std::Number q, std::Number r) << t` are implemented directly by the compiler, not method calls.

**`io::OStream` (output stream)**

```text
$OStream {
    @!:= << [(value) ~> () {
        // Reception function (constant pattern ~>): output value to standard output;
        // does not modify out's own state, so out may be declared constant
    }];
}
```

**`io::IStream` (input stream)**

```text
$IStream {
    @!=: << [() ~> (result) {
        // Publication function (const semantics): read from standard input and publish to result;
        // does not modify in's own state, so in may be declared constant
    }];
}
```

> Note: `std::Number` means "number", and `std::Float` has been replaced (see the clarification in Section 5.5). All objects above inherit from `std::Object` by default, so `::`, `~`, `=`, `=:`, `:=` are all automatically available with no need to re-implement them. (The historical `_case` method and `error` attribute are retired — see the v1.27 note.)

### 2.4 The program itself is also an object
The program's entry point `$Program` itself is an object instance:
- Compiler startup = instantiating `$Program`
- Program execution = the lifetime of `$Program`
This design eliminates the chaos of "global scope" — the program's startup has a clear boundary and context, everything has a master.
## III. Streaming Statements
Flow statements are the core action of Synth OOP: **they enable data to flow from one object to another**.
### 3.1 Two ways of writing, one meaning

```text
<object A> >> <object B>; // A flows into B
<object B> << <object A>; // Completely equivalent to the above
```

The two writing styles have exactly the same meaning; just choose the one that feels more natural to read.
### 3.2 Examples

```text
&io;
-(std::String txt);
-(io::OStream out);
-(io::IStream in);
out << "Yeah.";
out << txt;
in >> txt;
```

Tip: Note that here both out (output stream) and in (input stream) are declared at the same time, and the io namespace is imported via `&io;`. Since OStream and IStream are native objects, they can't be used directly — they must first be instantiated.
### 3.3 The Backstage of Mobility: Intelligent Negotiation
Data is not pushed unilaterally. Before each data transfer, the sender and receiver perform a "diplomatic handshake," automatically negotiating compatibility via a built-in protocol:
- **Negotiation succeeds**: data flows in smoothly;
- **Negotiation fails**: no implicit coercive conversion ever occurs (you'll never see the expression `"1" + 1` yielding `"11"`), the system degrades gracefully, returning a safe null object, and the program doesn't crash.
This is the key technical mechanism behind "freedom at compile time, safety at runtime": you just flow, and the protocol guarantees safety.
### 3.4 Special Semantics of the Out Stream: Writing to the Stream Does Not Modify Environment Variables
In Synth OOP, writing data to `io::OStream` (e.g., `out << "Hello"`) **is not a modification of environment variables** — it's rather a special kind of "communication" operation. To understand this, you need to grasp why the Out's receive function (`:=`) is special.
#### 3.4.1 The receiving function is const
The receive function `:=` of `io::OStream` is defined as a **constant pattern** (`~>`). No matter what object it receives, the receive function itself never modifies the state of the receiver — it simply "takes in" the data and performs the output action. In other words, `out << "Hello"` does not alter the `out` object itself; rather, it merely causes `out` to print `"Hello"` to standard output.
This is entirely different from typical built-in objects. For a regular `std::Number`, performing `x << 5` actually modifies the value of `x`. However, when you perform `out << "Hello"` on an `io::OStream`, the state of `out` remains unchanged.
#### 3.4.2 The Nature of Flow Statements
The underlying execution order of flow statements is:
1. **First, call the sender's publish function `=:`** — the sender "publishes" the value it wants to send out;
2. **Then, call the receiver's receive function `:=`** — the receiver "accepts" this value and performs the corresponding operation (for `OStream`, this means printing output).
These two steps are automatically completed via the intelligent negotiation protocol. The sender and receiver each fulfill their own responsibilities, without interfering with each other.

This also answers another question: **why assignment can directly reuse the stream-statement syntax**. Every native object's reception function `:=` carries a default implementation — **when the incoming value's type matches the receiver's own type, it simply assigns itself directly from that value**; at the same time, the sender's publication function `=:` cooperates by publishing itself as-is. Thus, between same-typed operands, `a << b` is naturally "assign b to a". You can also bypass the stream statement and explicitly call the assignment method as `a.=(b)`, which is fully equivalent — it just that the streaming form is more intuitive and better fits the language's "data flows" philosophy (see Section 4.3).
#### 3.4.3 Out can be declared as a constant
Precisely because the receiving function of Out does not modify itself, you can declare Out as a constant when you declare it:

```text
-(io::OStream! out);
out << "Hello"; // Legal! out is constant, but the write operation doesn't modify out itself
```

This is impossible with ordinary built-in objects — if you declare a `std::Number` as constant, any attempt to modify it will be intercepted by the compiler. However, `io::OStream` is special: its "writing" operation is essentially a side-effect action (output) rather than a state modification.

> Tip: The public function `=:` of `io::IStream` also has similar const semantics — reading the input does not modify the state of the `IStream` itself. Therefore, `IStream` can also be declared as constant.

## IV. Object Instantiation Statements (Expressions)
In Synth OOP, the statement that creates an object (that is, an instance of a class) is formally called an **object instantiation statement**. You might have encountered terms like "variable declaration" or "variable definition" in other languages, but in Synth OOP, every variable is itself an object; thus, "object instantiation" is a more accurate and fundamental term to use.
More importantly — **the object instantiation statement itself is also an expression**. Its value is precisely the newly created empty object. This means that declaring an object is no longer an isolated "action" — rather, it becomes part of a data flow, capable of participating in computations, method calls, and even directly feeding into the next object.
### 4.1 Standard Object Instantiation
The syntax format is: `-(type name variable name);`
This is consistent with the declaration style in most object-oriented languages, but there's one key difference: The hyphen `-` at the beginning of the line is immediately followed by a pair of parentheses `()`, inside which the type name and variable name are specified. These parentheses indicate that "this is an instantiation expression."

> 💡 **Beginner clarification: You MUST include the prototype name when instantiating**
> The parentheses must contain **both** `type name` and `variable name` — **you cannot write only the variable name** (you must not write `-(num)`). That's because an instantiation statement doesn't just "give a name to something that already exists" — it "creates a new object according to a prototype." The type name (also called the **prototype name**, e.g. `std::Number`, `Student`) determines which class gets instantiated and what methods and behaviors the new object has. If you write only a variable name without a prototype name, the compiler can't tell what you're instantiating; it can only fall back to creating a **system default empty object** — one with no methods and no usable state, so you can't operate on it normally. So remember: **for every object instantiation written with `-(...)`, the parentheses must be `-(prototype-name variable-name)` — the prototype name cannot be omitted.**

```text
-(std::Number a); // Declare and instantiate a std::Number object a
-(Student b); // Declare and instantiate a Student object b
```

Tip: The `-` prefix is a hallmark design feature of Synth OOP. It lets you instantly recognize that "this is an object instantiation statement." The parentheses `()` then clearly indicate to you that "a new object is being created here."
### 4.2 Declare and Assign
The syntax format is: `-(type name variable name) << value;`

```text
-(std::String text) << "Yahoo!";
```

### 4.3 Why `<<` Is Used for Assignment
In Synth OOP, assignment isn't about "equality"; rather, it's about "flow" — data flows into variables. This mechanism is the same as the stream statements introduced in Chapter III, and the direction feels intuitively natural: things on the right flow into the left.
You can think of `<<` as a pipeline: data starts from the right and flows along the pipeline into the variable on the left.
Note: **`=` is not an operator in Synth OOP** — the infix notation `a = b` is invalid. The reason assignment can borrow `<<` is that every native object's reception function `:=` defaults to assigning itself directly from the incoming value when "the other party's type matches its own", while the sender's publication function `=:` cooperates by publishing itself as-is (see Section 3.4.2 for details). You can also explicitly call the assignment method as `a.=(b)`, fully equivalent to the `a << b` assignment usage — but the streaming form better fits the language's flow philosophy, so `<<` is always recommended.
### 4.4 The Zero-Value Rule
The moment a variable is declared (and memory is allocated), it's automatically initialized to the zero value of its object. In other words, in Synth OOP's world, there are no garbage values, no dangling pointers, and no "undefined" states. There's no abyss of "nothingness" here — only a safe zero value.
Why is this important? Imagine you're working on a large-scale project: how frustrating it would be to debug if a variable happened to hold a random garbage value! The zero-value rule fundamentally eliminates these kinds of problems: as soon as you declare a variable, it has a definite, safe initial value.
### 4.5 Constant Markers: Append ! after the type name
Adding an exclamation mark (`!`) after a type name indicates that the variable is constant. Any attempt to modify its value will be rejected by the compiler at compile time (see Chapter XI for details).

```text
-(std::Number! frozen) << 7;
```

frozen is declared as a constant; any subsequent attempt to assign a value to frozen will trigger a compile-time error.
Note: The exclamation mark (`!`) immediately follows the type name and precedes the variable name — this is the only valid syntax for constant variables. The standalone `!!` suffix no longer exists.
### 4.6 Instantiation Expressions: Declaration as Data
This is the most important design feature of this chapter — **the object instantiation statement is an expression, whose value is the newly created empty object**. This means you can embed the declaration directly into any expression, allowing it to participate in computations, method calls, and stream operations:
**Used directly in stream statements:**

```text
&io;
-(io::OStream out);
-(std::String msg) << "Hello";
out << (-(std::String msg2) << "World"); // Declare msg2 and assign, expression value is msg2 object itself
out << msg;
```

In this example, `(-(std::String msg2) << "World")` is an expression whose value is the msg2 object (containing the string "World"). This value can be directly consumed by `out <<`.
**Directly calling the method:**

```text
-(std::String result) << ("hello").upper(); // If upper() is a String method
```

**Chain combination:**

```text
-(std::Number sum) << ((-(std::Number a) << 10).+( -(std::Number b) << 20));
```

Here, a (with a value of 10) and b (with a value of 20) are first declared and initialized. The expression evaluates to these two objects, then performs the addition operation, and finally assigns the result to sum.

> **Note (v1.27):** The old documentation described this with the "poisoned water model" — an object "carrying toxicity from the instant of creation, spreading along the data flow." That model is **retired**. Errors are now raised **immediately at the source** and reported as a g++-style diagnostic (with the offending source line and a full execution stack). For example, `a./(0)` (division by zero) follows IEEE 754 and yields `Infinity`/`NaN` directly (it does not interrupt), whereas a genuine semantic error (type mismatch, missing method, …) aborts immediately instead of "blending into the data flow."
## V. Behaviors and Their Patterns
### 5.1 What is Behavior
"Behavior" is the fundamental unit of executable logic in Synth OOP, written as a behavior enclosed in square brackets:

```text
[(input-parameters) behavior-pattern (outputs) { function-body }]
```

- The first parentheses: input parameters (a parameter list, essentially a tuple declaration whose order strictly matches the method signature)
- In the middle: the behavior pattern (`=>`, `~>`, or `->`, see Section 5.2)
- After the arrow: the output result (method signature + output parameter names)
- Inside the braces: the function body (what it actually does)

> 💡 **The `void` keyword has been retired — use an empty `()` for "no value".**
> Synth OOP's language philosophy **does allow a behavior to publish nothing**: simply write an **empty output list** — e.g. `[() -> () {…}]` or `[(x) -> () {…}]`. A behavior whose output list is empty publishes no value (so `out << aBehaviorReturningNothing` prints nothing), and every output it does list is published as-is. Writing `void` where a type, variable, output, or constraint is expected is now a **syntax error** (`'void' is not a type or name; use empty parentheses '()' …`), enforced in the parser.

> 💡 **A clarification for beginners: why is it called a "behavior"?**
> You may have seen the terms "closure" or "function" in other languages. In Synth OOP, **all functions are implemented as behaviors (i.e., closures), so there is no distinction between "function" and "closure"** — they are the same thing. There is therefore no need to separate them or introduce extra terms; **the whole thing is simply called a "behavior"**. A behavior is one unit of executable logic: it has inputs, outputs, and a function body. Whether it's an ordinary function, a method bound to an object, or a conditional branch passed to a control-flow construct, underneath it's all a behavior. Think of it simply: **in this language, there is only one kind of executable "unit", and it is called a behavior.**

A behavior is itself also an object: it can be **passed, composed, and lazily executed** just like data.
Think of a behavior as a "to-do note" — you don't have to execute it right away. You can put it in your pocket (pass it to another behavior), pin it to a wall (store it in a variable), or stick it on a calendar (bind it to an object as a method), and take it out to run whenever the time is right.

### 5.2 The Three Levels of Behavior Patterns
The arrow in the middle of a behavior is not just a syntactic decoration — it is the **behavior-pattern arrow**, which defines the behavior's external access rights: what it may see and what it may change are written right on the arrow. There are three levels of behavior patterns, arranged from strictest to most permissive:

| Behavior Pattern | Meaning |
| ---- | ---- |
| `=>` zero-side-effect pattern | Absolutely no access to any external entities — not even read access; zero side effects |
| `~>` constant pattern | Can access (read-only) the external environment but cannot modify it |
| `->` non-constant pattern | Can both access and modify the external environment |

The three levels are as follows:

- **`=>` (zero-side-effect pattern)**: The behavior is completely isolated from the external world — it cannot access any external variables or any global state; **even as a class method, it cannot access the class's members**. Its entire world consists only of its own parameters, local variables, and output parameters. This is the strictest behavior pattern, suitable for pure computation.
- **`~>` (constant pattern)**: The behavior can **access (read) the caller's environment variables**, but **cannot modify** them; as a class method it can access, but not modify, the class's members.
- **`->` (non-constant pattern)**: The behavior **can both access and modify the caller's environment variables**; as a class method it can access and modify the class's members.

| Behavior Type | Can Access External/Member Variables? | Can Modify External/Member Variables? |
| ---- | ---- | ---- |
| => (zero-side-effect pattern) | No | No |
| ~> (constant pattern) | Yes (read-only) | No |
| -> (non-constant pattern) | Yes | Yes |

> ⚠️ **A clarification for compiler developers and curious readers: the real implementation of the constant pattern (`~>`)**
> You might wonder — "since `->` can modify the caller's environment variables, how is `~>` really preventing modification?" There is a counter-intuitive implementation detail here:
> **At the compiler level, the constant pattern (`~>`) works like this: the behavior may access variables and may even "modify" them — but those modifications are silent and never written back to the outside world.** Specifically:
> - When a `~>` behavior runs, it receives **a copy of the caller's environment variables**;
> - Any modification the behavior makes to those environment variables **happens only on that copy, silently** — the change is visible within the behavior itself (later code inside the behavior sees the updated value), but it **never affects the outside world**;
> - Only modifications to the behavior's **own variables** (its own local variables, parameters, and output parameters) take effect normally within its own scope.
> In other words, `~>` is like **giving the behavior a snapshot of the environment**: read or write it however you like, and it will never pollute the caller's real environment. `->`, by contrast, has no such "isolation" — it reads and writes the real environment variables. This is the true meaning of "the constant pattern is read-only, the non-constant pattern is writable" at the implementation level.

The choice of behavior pattern is in the hands of the declarer: if you don't want the behavior to modify environment variables and only want it to read, choose the constant pattern `~>`; if you don't even want it to look outside, choose the zero-side-effect pattern `=>`.

Tip: The concept of a behavior pattern is somewhat similar to the const keyword used in C++ for member functions, but they are not the same thing. In Synth OOP, a behavior pattern is a declaration of the behavior's external access; it follows the behavior wherever it goes (behaviors can exist independently and don't necessarily have to be bound as methods). It's orthogonal to the "const" applied to method variables (see Section 6.2).
### 5.3 Examples
```text
&io;
-(io::OStream out);
[() -> () { out << "Yes."; }];
```

No input; the output list is empty `()` (the behavior publishes nothing) — the behavior's body streams a single sentence to `out`. It needs to access and write to the caller's environment variable `out`, so it signs the non-constant pattern `->`.

```text
[(a) ~> (sum) { sum << a.+(1); }]
```

Input a number `a`, and output `sum`. The behavior's body only computes and writes to the output. It signs the constant pattern `~>` (no external variables are read in this example, but `~>` allows reading). Note: assignments to output variables inside the behavior's body still use `<<`.

```text
[(a) => (sum) { sum << a.+(1); }]
```

With the same computation, signing the zero-side-effect pattern `=>` tells the caller: this behavior absolutely does not touch anything external — it is pure computation. This is the strongest guarantee you can give the caller.

### 5.4 Tuples
Tuples are native objects in Synth OOP, serving as immutable value sequence containers that are directly built into the language. They have been officially incorporated into the native object table (see Section 2.2), alongside std::Array, std::Dict, and others, and are directly implemented by the compiler.
#### 5.4.1 The Nature of Tuples
A tuple is a lightweight, immutable, position-based packaging mechanism for values. Its primary purpose is to group multiple values together, allowing them to be passed as a single unit through data streams and then unpacked at the receiving end according to their positional order.

| Dimension | Class | Dict | Tuple |
| ---- | ---- | ---- | ---- |
| Essence | Encapsulation of behavior + state | Collection of dynamic key-value pairs | Immutable sequence of values arranged by position |
| Access Method | Method name / attribute name | Key | Continuous number indices starting from 0, or structural decomposition |
| Mutability | Mutable (can modify members) | Mutable (can add or remove keys) | Absolutely immutable |
| Underlying Overhead | Virtual function table + member variables | Hash table | Contiguous memory array (extremely fast) |
| Creation Method | Instantiation expression `ClassName` | Literal `{...}` | Literal `(val1, val2)` or a method's multiple return values |
| Error handling (historical "Poisoned Water Model" retired) | Errors raised immediately at the source + g++-style diagnostic with execution stack | same | same |

#### 5.4.2 Initialization of Tuples
The initialization method for tuples is very similar to that of std::Array; the only difference is that parentheses `()` are used instead of square brackets `[]`:

```text
// Tuple literals: wrap multiple values in parentheses
-(std::Tuple point) << (10, 20); // tuple of numbers
-(std::Tuple mixed) << ("Alice", 25, true); // tuple of mixed objects
-(std::Tuple nested) << ((1, 2), (3, 4)); // nested tuples
```

The elements within a tuple can be of any mixed object, and the compiler will automatically infer the tuple's type. Once created, tuples are immutable — any attempt to modify the values inside a tuple will trigger a compile-time error.
#### 5.4.3 Structural Decomposition of Tuples
Tuples support **structural decomposition** syntax, which unpacks the values in a tuple by position — the mirror image of how a method with multiple return values is defined:

```text
// Method definition with multiple returns: (q, r) -> ...
$Math {
    @divide << [(a, b) -> (q, r) {
        q << a./(b);
        r << a.%(b);
    }];
}
// Instantiate Math and call the method on the instance
-(Math m);
// Decompose the tuple: split it apart with the same positional correspondence
-(std::Number q, std::Number r) << m.divide(10, 3); // take both
// Use the underscore _ as a placeholder to ignore unwanted values
-(std::Number q, _) << m.divide(10, 3); // quotient only
-(_, std::Number r) << m.divide(10, 3); // remainder only
```

When decomposing, **the method signatures must match exactly** — the type declared on the left must be identical to the type at the corresponding position in the tuple, and the compiler will perform method existence checking.

#### 5.4.4 Accessing Tuple Elements via Built-in Methods
In addition to structural decomposition, you can also access elements by index using the tuple's built-in method `get`:

```text
-(std::Tuple t) << (10, "Alice", true);
-(std::Number x) << t.get(0); // x = 10
-(std::String name) << t.get(1); // name = "Alice"
-(std::Boolean flag) << t.get(2); // flag = true
```

The types must match: `get` returns the original value at the corresponding position in the tuple, and the method signature of the variable receiving it must be consistent with that value type. If the method signatures don't match, the compiler will generate an error:

```text
-(std::Tuple t) << (10, "Alice", true);
-(std::Number name) << t.get(1); // Error! t.get(1) is String, cannot assign to Number
```

#### 5.4.5 Tuples as Method Arguments and Return Values
Tuple objects can be directly passed as method arguments or returned from methods:

```text
// Point class: make returns a tuple, print prints a tuple
&io;
-(io::OStream out);
$Point {
    @make << [(x, y) -> (result) {
        result << (x, y);
    }];
    @print << [(point) -> () {
        out << point.get(0);
        out << point.get(1);
    }];
}
// Usage
-(Point p);
p.print((10, 20)); // Directly pass a tuple literal
-(std::Tuple t) << p.make(5, 15); // Receive a tuple return value
p.print(t); // Pass a tuple to another method
```

#### 5.4.6 Tuple Chaining: Multiple return values are directly passed into another method
You can pass the multiple return values of a method directly as arguments to another method. This is because methods can be defined to accept either separate argument lists or a single tuple directly:

```text
// Define methods that accept separate arguments (in the Math class, alongside divide)
&io;
-(io::OStream out);
$Math {
    @divide << [(a, b) -> (q, r) {
        q << a./(b);
        r << a.%(b);
    }];
    @process << [(x, y) -> () {
        out << x;
        out << y;
    }];
}
-(Math m);
// Pass divide's multiple return values directly to process — aligned by position
m.process(m.divide(10, 3));
```

In this example, `m.divide(10, 3)` returns two values — the quotient and the remainder — which are passed to `m.process` as separate arguments. The formal parameters x and y of `m.process` each receive these two values. At the runtime level, when `m.process` is invoked, it directly receives two independent Number arguments, x and y, without requiring any additional wrapping or unboxing operations.
#### 5.4.7 Underlying Model
In Synth OOP, a behavior is essentially a runtime structure that takes a tuple as input and returns a tuple. The list of input parameters is, in essence, a declaration of the incoming tuple (with strict matching of both order and method signatures); the list of return values is, in essence, the outgoing tuple itself.
The brilliance of this design lies in its complete transparency of data flow. You don't need to hunt all over the code for return statements to figure out what a function is returning — you simply look at the names of the output parameters to trace exactly where their values come from and where they go. It's as intuitive as following the path of a pipe: water flows in from wherever it enters, and it flows out from wherever it exits.
### 5.5 Arithmetic and Comparison Operations as Method Calls
In Synth OOP, all arithmetic and comparison operations are method calls rather than infix operators as in traditional languages. This means:

| Operator | Method Call |
| ---- | ---- |
| + | a.+(b) |
| - | a.-(b) |
| * | a.*(b) |
| / | a./(b) |
| % | a.%(b) |
| < | a.<(b) |
| > | a.>(b) |
| <= | a.<=(b) |
| >= | a.>=(b) |
| == | a.==(b) |
| != | a.!=(b) |

This design is entirely consistent with the string concatenation syntax you're already familiar with (written as `.+(anotherString)` when called). The `+` method for Strings is used to concatenate strings, while the `+` method for `std::Number` is used for numeric addition — both share the same method name, "+", but operate on different types, with the duck typing negotiation mechanism automatically handling the dispatch.

> 💡 **Clarification: `std::Number` means "number", and `std::Float` has been replaced**
> This language no longer has a separate `std::Float` (floating-point type). `std::Number` uniformly carries **all numeric values, both integers and floating-point numbers** — a `std::Number` can be either an integer or a decimal. Therefore, for any numeric value, always use `std::Number`; do not use `Float` or the old term "floating-point type". The arithmetic methods (`+`, `-`, `*`, `/`, `%`) and comparison methods (`<`, `>`, `<=`, `>=`, `==`, `!=`) are all built-in methods of `std::Number` (see Section 2.3).
Why is it designed this way? This is fully consistent with the design principles you've learned earlier: "Everything is an object" and "All data inherently possesses behavioral capabilities." If + were merely an infix operator, then `1 + "hello"` would be syntactically valid (though it would result in a runtime method signature mismatch). However, when written as `1.+( "hello" )`, the caller `1` is an std::Number object, which simply doesn't have an overloaded + method that accepts a String argument. As a result, the compiler can immediately report an error at compile time.

```text
// Wrong: a + b (infix operator syntax)
-(std::Number a) << 10;
-(std::Number b) << 20;
-(std::Number sum) << a + b; // Compilation error!

// Correct: a.+(b) (method call syntax)
-(std::Number a) << 10;
-(std::Number b) << 20;
-(std::Number sum) << a.+(b); // Correct!
```

Note: In Synth OOP, the symbols +, -, *, /, %, <, >, <=, >=, ==, and != **are themselves method names**, but a method name is only effective when combined with the call operator `.` (e.g., `a.+(b)`). Any notation that treats them as infix operators (such as `a + b` or `a < b`) is invalid and will result in a compilation error. (On the distinction between the method name and the dot, see the clarification in Chapter I.)
### 5.6 Scope of Behaviors
A behavior can access variables outside its defining scope — this is known as "capturing external environment variables." **The permissions to access and modify them are entirely determined by the behavior pattern**:

- **Zero-side-effect pattern `=>`**: Completely isolated. It cannot access any external variables; when serving as a class method, it cannot even access the class's attributes. The behavior's world contains only its parameters, local variables, and output parameters.
- **Constant pattern `~>`**: It can **read** the caller's environment variables, but cannot modify them; when serving as a class method, it can read the class's members but not modify them.
- **Non-constant pattern `->`**: It can **read and modify** the caller's environment variables; when serving as a class method, it can read and modify the class's members.

```text
&io;
-(io::OStream out);
-(std::Number counter) << 0;
// Zero-side-effect pattern: even reading counter is not allowed (compile error)
// [() => (v) { v << counter; }];
// Constant pattern: can read counter, cannot modify any environment variable
[() ~> (snapshot) { snapshot << counter; }];
// Non-constant pattern: can read counter, and can also modify it
[() -> () { counter << counter.+(1); }];
```

```text
// Inside a class: the behavior pattern likewise restricts access to and modification of members
$Counter {
    -(std::Number value);
    // Constant-pattern method: can read the member, cannot modify it
    @peek << [() ~> (result) { result << value; }];
    // Non-constant-pattern method: can read and modify the member
    @inc << [() -> () { value << value.+(1); }];
}
```

### 5.7 Side Effects of Behaviors
The side effects of a behavior refer to the modifications the behavior makes to state outside its function body during execution. Synth OOP strictly constrains side effects through the three levels of behavior patterns, ensuring code predictability.

#### 5.7.1 Zero-Side-Effect Pattern Behavior: Absolute Isolation

When a behavior signs the zero-side-effect pattern `=>`, it is completely isolated from the external world:

- It cannot access any external variables (not even read them);
- It cannot access any global state;
- When serving as a class method, it cannot even access the class's attributes;
- Its only "input" is its parameters, and its only "output" is its output parameters.

The compiler enforces all of this — any code that attempts to touch anything external triggers a compile-time error.

#### 5.7.2 Constant Pattern Behavior: Read-only

When a behavior signs the constant pattern `~>`, it can read the caller's environment variables (and, within a class, the class's members), but cannot modify them. Any modification attempt triggers a compile-time error.

#### 5.7.3 Non-Constant Pattern Behavior: Readable and Writable

When a behavior signs the non-constant pattern `->`, it can read and modify the caller's environment variables; when serving as a class method, it can read and modify the class's members.

To summarize in one table:

| Behavior Kind | Can Access External/Member? | Can Modify External/Member? |
| -------- | ------------------- | ------------------- |
| `=>` (zero-side-effect pattern) | No | No |
| `~>` (constant pattern) | Yes (read-only) | No |
| `->` (non-constant pattern) | Yes | Yes |

Core idea: **The behavior pattern is the sole switch for side effects — three levels of trust are yours to choose.** When you see `=>`, the behavior is a pure computation isolated from the world; when you see `~>`, it only observes and never touches; when you see `->`, it may take action. Reasoning about program behavior becomes straightforward from here on out.
## VI. Methods
### 6.1 Declaration Method
A method is a behavior bound to an object. The syntax is highly fluent — simply pass a behavior into the method name:

```text
@methodName << [behavior];
```

> 💡 **Beginner clarification: Methods cannot exist independently of a class**
> **A method must be bound to some class (or some object); it cannot exist independently of a class.** Think of a method as an "ability attached to a class" — without a class to act as the carrier, there's nothing for the method to attach to. This is the same as how "member functions belong to a class" in C++/Java: a method name must be declared inside a class definition body like `$ClassName { ... }` (e.g. `$Counter { @inc << ...; }`), or be bound to a specific object. Writing a standalone `@methodName << [behavior];` with no class to host it is **illegal**.
> By contrast, **behaviors themselves may exist freely** — they are the units of executable logic and can be stored in variables, passed to behaviors, and used as arguments (see Section 5.1). But once a behavior is declared as a "method" via `@methodName << ...`, it must have an owning class/object. To remember it simply: **behaviors are free; methods belong to classes.**

### 6.2 Method Modifiers
First, a concept to clarify: **A method itself is a variable** (a behavioral variable, whose value is the behavior). The method modifiers apply to this variable, not to the inside of the behavior:

- `!` means **this method variable is const** — the method binding cannot be modified (another behavior cannot be streamed back into this method name).
- `#` means **private** (not exposed to the outside).

The two can be combined:

| Notation | Meaning |
| ---- | ---- |
| `@name << …;` | Ordinary method |
| `@!name << …;` | Method variable is const (cannot be rebound) |
| `@#name << …;` | Private method |
| `@!#name << …;` | Private and the method variable is const |

Note the distinction: **The behavior pattern is a property of the behavior**, describing the behavior's access permissions toward the outside (the three levels `=>` / `~>` / `->`); it travels with the behavior — a behavior can also exist freely without being bound as a method. **The method modifier `!` is the const-ness of the method variable**, governing "whether this method binding can be changed." The two are orthogonal and never interfere with each other. This only loosely resembles the `const` written before a code block in C++ — it is not the same concept.

> 💡 **Telling the three kinds of "immutability" apart at a glance**: Synth OOP has three read-only/immutable mechanisms that look alike but are completely independent, and beginners easily confuse them:
>
> | Marker | Where It's Written | What It Governs |
> | ---- | ---- | ---- |
> | `!` (after the type name) | `-(std::Number! x)` | The **variable's value** cannot be modified (Section 4.5) |
> | `@!` (before the method name) | `@!get << ...` | The **method binding** cannot be rebound (this section) |
> | `~>` (at the arrow) | `[() ~> (r) {...}]` | The **behavior's view of the external environment** is read-only (Section 5.2) |
>
> The three act on different targets, are mutually orthogonal, and can be freely combined. For example, an `@!` method can perfectly well sign the `->` pattern — the binding cannot be swapped, but the behavior's body may still modify members.

### 6.3 Four Reserved Method Names
There are four method names reserved by the language, each with a specific role:

| Method Name | Role | Description |
| ---- | ---- | ---- |
| :: | Constructor | Called when an object is created |
| ~ | Destructor | Called when an object is destroyed |
| =: | Publication function | Used to expose information externally |
| := | Reception function | Used to receive information from external sources |

=: and := are mirror images of each other — one publishes, the other receives, together forming a communication channel between objects.

> Note: These four reserved methods are **all defined on the `std::Object` base class** (the default parent class, see Section 2.3.1) and marked as const (immutable) with `@!`, making them globally uniform. Any class inherits them by default, with no need to declare them itself.
### 6.4 Examples

```text
$Example {
    @!:: << [() ~> () {}];
}
```

Declare a constructor (belonging to the `$Example` class), with the method variable carrying `!` (the binding cannot be rebound), and the behavior signing the constant pattern — no input, no output, and an empty body.

### 6.5 Traditional method calls are also expressions
In Synth OOP, a method call itself is also an expression, and its value is the result carried by the output parameters after the method has been executed. This means that method calls can appear wherever expressions are allowed — you can directly assign the return value of a method call to a variable, pass it to another method, or use it in an arithmetic operation:

```text
// A method call's result streams directly into a variable
-(std::Number result) << obj.getvalue();
// A method call nested inside an expression
-(std::Number sum) << obj.add(10).get();
// A method call's return value passed directly to another method
other.process(obj.compute());
// A method call used as the condition of if_
(obj.check()).if_(
    [() ~> (value) { value << 1; }],
    [() ~> (value) { value << 0; }]
);
```

This is entirely consistent with the method-call syntax you've encountered before in C++, Java, and other languages — method calls return a value that can continue to participate in the data flow. In Synth OOP, there's no strict distinction between "statements" and "expressions"; everything can be an expression, and everything can take part in data flow.
Tip: This design allows code to be combined as freely as mathematical formulas. You don't need to declare extra variables for intermediate results; method-call chains can be executed in one smooth, continuous flow, with data entering at one end and exiting at the other — all neatly and efficiently.
## VII. Control Flow: if_, while_, repeat_
In Synth OOP, control-flow constructs such as if, while, and repeat are neither rigid syntax keywords nor standalone global functions — they are **methods**. To distinguish them from reserved words, these method names uniformly carry an underscore suffix: `if_`, `while_`, and `repeat_`. Among them, `if_` and `while_` are methods of `std::Boolean` (boolean values), while `repeat_` is a method of `std::Number` (numbers).
Why is this design adopted? It means that control flow can be passed around, composed, and deferred just like data. You can store the branch behaviors of if_ in variables, pass them to other methods, or selectively decide whether to execute them on demand. This provides extraordinary flexibility.
The syntax for control-flow behaviors fully follows the behavior-definition pattern described in Chapter V: `[parameter-list behavior-pattern arrow (type return-name) { body }]`. The parameter list is essentially a tuple declaration, with the order strictly matching the method signatures — there's no tolerance whatsoever. There's no return keyword; instead, the return value is determined by the final values of the output parameters at the end of execution.
Branch and loop-body behaviors directly access the caller's environment variables (reading or reading/writing according to the behavior pattern), without relying on any dictionaries to pass context.
### 7.1 if_ statement
if_ is a method of std::Boolean. A boolean value calls `.if_()`, passing in a true branch behavior and a false branch behavior:

```text
condition.if_(true_behavior, false_behavior)
```

Here, condition is a `std::Boolean` expression, and `.if_()` is its method. if_ takes exactly two arguments: the true branch behavior and the false branch behavior. The condition itself is not an argument; rather, it's the boolean value that triggers the call to `.if_()`.
Parameter description:

| Parameter | Method Signature | Description |
| ---- | ---- | ---- |
| First | Behavior (true branch) | Executed when the condition is true; the output value represents the return value of this branch. |
| Second | Behavior (false branch) | Executed when the condition is false; the output value represents the return value of this branch. |

Return value: The return value of `.if_()` is the value of the output parameter value produced by the executed branch behavior.
Method constraint: The value outputs from both behaviors must be consistent. The compiler checks this constraint at compile time to ensure consistency.
Example 1 (using an expression as the condition):

```text
-(std::Number a) << 10;
-(std::Number b) << 20;
-(std::Number result) << (a.>(b)).if_(
    [() ~> (value) { value << a; }],
    [() ~> (value) { value << b; }]
);
```

In this example:
- `(a.>(b))` is a `std::Boolean` expression that calls the `.if_()` method;
- Both branch behaviors directly read the caller's environment variables `a` and `b` (constant pattern `~>` suffices since they are read-only);
- Both behaviors output `value` of object `std::Number`, satisfying the method constraint;
- `if_` returns the executed branch's `value`, which flows into `result` via `<<`.

**Example 2 (using a boolean constant as the condition):**

```text
-(std::Number result) << (true).if_(
    [() ~> (value) { value << 1; }],
    [() ~> (value) { value << 0; }]
);
```

Here, the boolean constant `true` is directly used as the condition, demonstrating that if_ can accept any std::Boolean expression — whether it's an expression, a constant, or even the return value of a method call.

**Example 3 (using the return value of a method call as the condition):**

```text
$Checker {
    @check << [() ~> (result) {
        -(std::Number x) << 5;
        -(std::Boolean cond) << (x.>(3));
        result << cond;
    }];
}
-(Checker c);
-(std::Number result) << c.check().if_(
    [() ~> (value) { value << 1; }],
    [() ~> (value) { value << 0; }]
);
```

### 7.2 while loop
`while_` is a method of `std::Boolean`. A boolean value calls `.while_()`, passing in a **loop-body behavior** and a **condition-check behavior** — note that the condition-check behavior comes **after** the loop-body behavior:

```text
condition.while_(body, condition_check)
```

- `condition` (the caller): a `std::Boolean`, the initial condition for entering the loop (if false, the loop ends immediately);
- `body` (first argument): the loop-body behavior. It receives a "loop state" `state` and returns a new `state` of the same type — **its parameter format must equal its return format**;
- `condition_check` (second argument): the condition-check behavior. It receives the `state` returned by `body` each round and returns a `std::Boolean` indicating whether to continue — **its parameter format must equal `body`'s return format**.

Parameter description:

| Parameter | Method Signature | Description |
| ---- | ---- | ---- |
| First | Behavior (loop body) `[(state) -> (state)]` | Receives state, returns a new state of the same type; parameter format must equal return format |
| Second | Behavior (condition check) `[(state) ~> (flag)]` | Receives state (i.e. body's return), returns a Boolean flag for whether to continue |

Execution flow:
1. Evaluate the caller `condition` (initial condition); if false, end immediately;
2. Execute the loop body `body`, obtaining its returned `state` (the initial `state` follows the zero-value rule);
3. Pass `state` to the condition-check behavior `condition_check`, obtaining `flag`;
4. If `flag` is true, use this round's `state` as the next round's `body` input and go back to step 2; otherwise end;
5. Return the value returned by the last execution of `body`.

> ⚠️ Compiler-internal checks: ① `body`'s "parameter format" must equal its "return format"; ② `condition_check`'s "parameter format" must equal `body`'s "return format". Both format-consistency checks are performed internally by the compiler; a mismatch is a compile error.

Example:

```text
-(std::Number result) << (true).while_(
    [(state) -> (next) {
        next << state.+(1); // loop body: receive state, return state+1
    }],
    [(state) ~> (flag) {
        flag << (state.<(10)); // condition check: use body's returned state to decide whether to continue
    }]
);
```

In this example:
- The initial condition `true` allows entering the loop, and `state` starts from the zero value `0`;
- The loop body receives `state` and returns `state+1` (parameter format `std::Number` == return format `std::Number`);
- The condition-check behavior receives the `state` returned by the body and checks `state.<(10)` to decide whether to continue;
- `while_` returns the last `state` returned by the loop body (here `10`).
### 7.3 repeat_ loop
`repeat_` is a method of `std::Number`. A number calls `.repeat_()`, passing in a **loop-body behavior** — the number itself represents the **loop count**:

```text
count.repeat_(body)
```

- `count` (the caller): a `std::Number`, the number of times the loop executes;
- `body` (the argument): the loop-body behavior. It receives a "loop state" `state` and returns a new `state` of the same type — **its parameter format must equal its return format**.

Parameter Description:

| Parameter | Method Signature | Description |
| ---- | ---- | ---- |
| First | Behavior (loop body) `[(state) -> (state)]` | Receives state, returns a new state of the same type; parameter format must equal return format |

Execution Flow:
1. `state` starts from the zero value;
2. Execute the loop body `body`, obtaining its returned `state`;
3. Use this round's `state` as the next round's `body` input, repeating until the body has executed `count` times;
4. Return the value returned by the last execution of `body`.

> ⚠️ Compiler-internal check: `body`'s "parameter format" must equal its "return format"; a mismatch is a compile error.

Example:

```text
-(std::Number result) << 5.repeat_(
    [(state) -> (next) {
        next << state.+(1); // loop body: receive state, return state+1
    }]
);
```

In this example:
- `5` is the loop count, and `state` starts from the zero value `0`;
- The loop body receives `state` and returns `state+1` (parameter format `std::Number` == return format `std::Number`);
- The loop body executes 5 times, and `repeat_` returns the last `state` returned by the loop body (here `5`).

### 7.4 Parameter Strictness
The arguments for if_, while_, and repeat_ are strict:

```text
// if_ requires exactly 2 arguments (true branch + false branch)
condition.if_(
    [() ~> (v) { v << 1; }],
    [() ~> (v) { v << 0; }]
);
// while_ requires exactly 2 arguments (loop body + condition check, condition check last)
condition.while_(
    [(state) -> (next) { next << state.+(1); }],
    [(state) ~> (flag) { flag << (state.<(10)); }]
);
// repeat_ requires exactly 1 argument (loop body); the loop count is the caller Number
5.repeat_([(state) -> (next) { next << state.+(1); }]);
```

Format-consistency requirements (checked internally by the compiler):
- For `while_` / `repeat_`, the loop-body behavior's **parameter format must equal its return format**;
- For `while_`, the condition-check behavior's **parameter format must equal the loop-body behavior's return format**.

## VIII. Class

### 8.1 Declaring a Class
Classes are declared with the `$` prefix. The syntax is:

```text
$<ClassName> [<ParentClass>] {
    -(member-signature memberName);
    @methodName << behavior;
}
```

The parent class in square brackets is optional. If omitted, the class inherits directly from `std::Object`.

### 8.2 The Parent Can Be a Type or an Instance
In Synth OOP, the parent position in `$<ClassName> [<Parent>]` can hold either a **type name** or an **instance**. This is one of the distinctive features of the Synth OOP class system.

- **A type name**: the new class inherits that type's structure and behavior — the traditional way of inheriting.
- **An instance**: the new class is "reverse-derived" from that instance; see Section 8.3 for details.

```text
$Animal {
    -(std::String name);
}
// Parent is a type: Dog inherits from Animal
$Dog [Animal] {
    -(std::Number age);
}
```

### 8.3 Deriving Classes from Instances (Reverse Derivation and Inheritance)
When the parent position holds an **instance**, Synth OOP performs a "reverse derivation":

1. **Infer the class configuration**: the compiler infers a class configuration from the instance's current member structure (member names and types);
2. **Use the instance's current values as initial values**: when the new class constructs objects, the instance's current member values serve as the initial values;
3. **The class template becomes the parent**: the new class's parent is the class template that the instance belongs to, so the full inheritance chain is preserved.

```text
$Student [Human] {
    -(std::Number age);
    -(std::String major);
    @:: << [(a, n, m) -> () {
        age << a;
        name << n; // name is inherited from Human
        major << m;
    }];
}
-(Student s);
s.name << "Alice";
s.age << 22;
s.major << "Computer Science";
// Derive the new class GraduateStudent from instance s
$GraduateStudent [s] {
    @addResearch << [(topic) -> () {
        name << name.+(" (Research: ").+(topic).+(")");
    }];
}
```

In this example, `$GraduateStudent [s]` is derived from the instance `s`. GraduateStudent automatically inherits the three members `name`, `age`, and `major`, and uses `s`'s current values ("Alice", 22, "Computer Science") as its initial values; its parent class is `Student` (which in turn inherits from `Human`), so the inheritance chain is fully preserved. In the `addResearch` method, string concatenation is done with the `.+(anotherString)` method (see the examples in Chapter XI and Appendix A).

## IX. Constraints

Chapter I stated that Synth OOP adopts duck typing: objects are recognized by "whether they have the required methods," not by their origin. A constraint (Constraint) is exactly that "required set of methods" written out explicitly as a checklist — in other languages, this role is usually called an "interface." This chapter fully explains the syntax and usage of constraints.

### 9.1 What a Constraint Is: A Checklist of Methods

The syntax of a constraint closely resembles a **class definition** (see Chapter VIII), except that it only describes method signatures and contains no implementation whatsoever:

```
#Addable {
    @+ << [(other) -> (result) {}];
}
```

Here, the `#` prefix means "this is a constraint," `@` marks a method declaration, and the behavior after `<<` writes out only the method signature (parameter `other`, behavior pattern `->`, output `result`) with the **body left empty (`{}`)** — because we only care about "what methods must exist and what they look like," not how they are implemented. It means: an object satisfying the `Addable` constraint must have a `+` method that takes one parameter `other` and returns one result `result`.

> 💡 **Strong-typing reassurance for beginners**: You may worry, "Is duck typing too loose? Will writing code feel unsteady?" Don't worry — **constraints are Synth OOP's "strong-typing armor."** Although the language is duck-typed, through the constraint mechanism you can still enjoy the reassurance of a strongly typed language: you can explicitly declare "this parameter must be something addable" or "this object must be able to print itself," and the compiler will check it for you. In other words, **duck typing gives you the freedom to write; constraints give you the confidence to use.** You can be as free as you like, and as strict as you like.

### 9.2 Constraints vs. Classes: Alike, but Not the Same Thing

The notation of a constraint is nearly identical to that of a class (`$Name {...}`), and beginners easily confuse them. One table makes the difference clear:

| Dimension | Class (`$`) | Constraint (`#`) |
| ---- | ---- | ---- |
| Purpose | Defines an object's structure and behavior: members + full method implementations | Only declares "which methods must exist": method signatures only, bodies left empty |
| Instantiable? | Yes: `-(Student s)` | No: constraints are only for qualification; you cannot write `-(Addable x)` |
| Carries state? | Has member variables | Has no members at all |
| Main use | The "blueprint" for building objects | The "acceptance checklist" for examining objects |

One sentence to remember: **a class is a blueprint; a constraint is an acceptance checklist.** The class tells you "how to build"; the constraint tells you "what the built thing must be able to do."

### 9.3 Defining a Constraint

A constraint can contain multiple method signatures. The syntax resembles a class definition, except that every method has only a signature, with an empty body (the braces do nothing):

```
#Printable {
    @print << [() -> () {}];
    @toString << [() -> (result) {}];
}
```

Constraints can be used to restrict the method requirements on a behavior's incoming and outgoing parameters.

### 9.4 Using Constraints in Behavior Parameters

Appending `[ConstraintName]` after a parameter name of a behavior (or method) restricts the incoming argument to satisfy that constraint:

```
[(a[Addable]) -> (result) {
    result << a.+(1);
}]
```

Here, `a` must satisfy the `Addable` constraint — that is, `a` must have a `+` method.

### 9.5 Filling In a Class Name as the Constraint: Automatic Matching of All Its Method Signatures

The constraint name does not have to be a newly defined `#Name`. **If you fill in a class name directly where a constraint is expected (e.g., `std::String`, `Student`), the compiler automatically recognizes it as a constraint — namely, "a requirement identical to every method signature of this class."** In other words, any object that "possesses exactly the same set of method signatures as this class" is considered to satisfy the constraint, regardless of whether it is actually an instance of that class.

For example: `[(s[std::String]) -> () {}]` means the incoming `s` must possess a set of method signatures exactly matching those of `std::String`. This means what you pass in does not have to be a `std::String` itself — as long as some object's method signatures are identical to `std::String`'s, it is accepted too.

> The benefit: you don't have to define a dedicated `#Name` constraint every time you need "something that behaves like a certain class" — just borrow the class name as the constraint, which is very convenient. This echoes the sentence at the beginning of this document — **duck typing lets you recognize objects by their behavior (method signatures), while classes and constraints are the templates that pin down these behavioral requirements.**

### 9.6 Using Other Constraints as Templates (Constraint Inheritance)

Constraints also support **inheritance** — just as classes may specify a parent class (see Section 8.2), a new constraint may specify a **parent constraint** at definition time, with the syntax `#NewConstraint [ParentConstraint] {...}`. The new constraint automatically inherits all method signatures of the parent constraint, then adds its own new signatures inside the braces:

```
#Addable {
    @+ << [(other) -> (result) {}];
}
#Comparable [Addable] {        // Comparable inherits all of Addable's signatures
    @< << [(other) -> (result) {}];
}
```

Thus, the `Comparable` constraint automatically includes Addable's `+` signature plus its own `<` signature. Any object satisfying `Comparable` necessarily satisfies `Addable`.

### 9.7 The Behavior Qualifier `@`

Inside the type-qualification square brackets `[]` of a parameter, besides a constraint name or class name, you may also fill in the special keyword `@`, meaning **this is a behavior (closure) qualifier** — the parameter must be a behavior:

```
[(handler[@]) -> () {
    // handler must be a behavior (closure), otherwise compilation fails
}]
```

If `@` is followed by a **behavior signature** (i.e., keeping the behavior's parameter list, behavior pattern, and return signature), then the parameter must be a behavior **possessing exactly that behavior signature**:

```
[(handler[@(x) -> (y)]) -> () {
    // handler must be a behavior whose signature must be (x) -> (y) (input x, output y)
}]
```

> The compiler performs two checks accordingly: ① the parameter **is a behavior**; ② the behavior's **signature matches**. This mechanism is mainly used by control-flow methods (such as `while_` / `repeat_`) to constrain the format of loop-body behaviors and condition-check behaviors (see Chapter VII).

### 9.8 When Checks Happen: A Runtime "Roll Call"

Constraint checking happens at **runtime** (when the constrained value is known). When the interpreter sees a qualification like `a[Addable]`, it performs a "roll call": it checks item by item whether `a`'s set of method signatures covers every item on the Addable checklist — one missing item, or one mismatched signature, means the constraint is not satisfied. Note that the criterion is **matching of the method-signature set**, and it has nothing to do with whether the object "is an instance of some class" — this is precisely the runtime realization of Chapter I's duck typing, "recognizing objects by their behavior."

> ✅ **Implementation status (current interpreter).** The parameter-level "roll call" above is now fully enforced at runtime: when a method parameter carries a constraint like `a[Addable]`, the interpreter checks the argument's method-signature set via `check_constraint` at behavior entry (compared by **type**, not parameter name); a variable constraint `-(T[Addable] v)` is checked at declaration (when it has no initializer) and again after a flow `<<` binds it. The check uses `ClassContract::validate`, which compares **parameter types (arity + in/out types)** only, so a real implementation (with freely-named parameters) still satisfies the contract. A value that fails the constraint raises `ConstraintException` (`value does not satisfy constraint '…'`) immediately. Constraint-related checks now land in three places: ① class-level validation (`define_class`); ② the parameter / variable runtime roll call (this section); ③ the native / built-in call-boundary signature check.

## X. Module Import Statements and Namespaces

### 10.1 Module Import Statements
Module import statements are used to bring in external modules or namespaces. The syntax is:

```text
&<moduleName>;
```

For example:

```text
&io; // Import the io namespace (OStream, IStream)
```

Once imported, the objects and methods under that namespace become available in the current scope. All examples that use `io::OStream` or `io::IStream` require importing `&io;` first.

> Note: The `$Program` object and all `std::` native objects can be used without explicit import; only external libraries need to be imported with `&`.

### 10.2 Namespaces: One File Is One Namespace
Synth OOP **does not provide a keyword for explicitly declaring namespaces** (there is no `namespace` block like C++ or `package` like Java). Instead, the division into namespaces is **automatic**: **every source file is automatically wrapped into its own namespace.**

This brings several benefits:
- **Natural isolation**: classes, constraints, and methods defined in different files do not interfere with one another by default — the same name can mean different things in two files without conflict. A namespace's name is usually the file name (without its extension).
- **Cross-file use requires import**: if you want to use a class defined in another file, you first import it with `&<that file's name>;`, then reference it as `<file name>::<name>` (e.g., after `&shapes;`, use `shapes::Vector`).
- **std / io are "built-in namespaces"**: `std::` and `io::` are the language's core namespaces, holding the native objects (`std::Number`, etc.) and the I/O streams (`io::OStream`, `io::IStream`) respectively. They can be thought of as two "files" provided by the compiler itself.

> Summary: **Namespace = file name.** You never need to write any "declare a namespace" syntax — the compiler automatically wraps each file into a namespace. To let another file access your contents, just `&file-name;` on that side to import it.
## XI. The Poisoned Water Model and _case: Error Propagation and Rescue

> **⚠ This chapter is retired (v1.27).** The "Poisoned Water Model" and the `_case` exception-catching method described here **no longer exist in the current interpreter**. Errors are now raised **immediately at the source** and reported in a **g++-style diagnostic**: `file:line:col: error: Type: message`, followed by the offending source line with a `^~~~` caret, and — for interpreter/runtime faults — a full **execution stack**. Color is on by default and can be disabled with `NO_COLOR=1`. Runtime legality checks use the `&assert;` standard library's `Checker` object (`has_method(target, name)` / `has_changed(target)`) instead of a propagated checked value.
>
> The original text below is retained **only as a historical design record** — do not write new programs against it. The current correct behavior is described in the v1.27 notes above and in the README's "g++-style errors" section.

You have already glimpsed the "poisoned water" several times — while learning instantiation expressions (Section 4.6) and division (Section 2.3). This chapter explains the (now-retired) mechanism from beginning to end, to help understand the language's evolution.

First, let's establish the big picture (historical perspective). Synth OOP once planned two safety cornerstones, one static and one dynamic:

- **The zero-value rule** (Section 4.4): guarantees objects are safe **at birth** — eliminating "uninitialized garbage values";
- **The Poisoned Water Model** (this chapter, retired): once hoped to guarantee objects are safe **after errors occur** — eliminating "unhandled runtime errors."

The zero-value rule gives every object a definite, safe value from its very first moment; the poisoned water model once aimed to ensure that once an error happens, it is neither silently lost nor does it crash the program. Together, the two were meant to supply the "safety" half of the "freedom while writing, safety while running" promise.

### 11.1 The Poisoned Water Model (Poison Water Model, retired)
Synth OOP's (old) error-reporting mechanism was called the "Poisoned Water Model." Its core idea: **every object carries its own "health status"; once an object becomes "poisoned" due to an error, that toxicity spreads along the data flow to every object that depends on it — until someone actively handles it.**

#### 11.1.1 The Implicit error Attribute (retired)
Every object once had an implicit `error` attribute. You could picture it as a "poison marker" inside the object:

- When the object is normal, `error` is empty (not poisoned);
- When the object has errored, `error` is set to an error-message string (poisoned).

For example, when `10./(0)` (division by zero) executes, the returned Number object's `error` attribute is set to "division by zero," and the object becomes a "poisoned object."

#### 11.1.2 The Contagion Model
The key of the Poisoned Water Model is "contagion" — the toxicity does not stay where it happened, but spreads along the data flow:

- If a poisoned object is assigned to another variable, the target variable becomes poisoned too;
- If a poisoned object participates in an operation, the result of the operation becomes poisoned too;
- If a poisoned object is passed into a behavior, the behavior's return value becomes poisoned too.

The rule of contagion is simple: **any operation that receives a poisoned object as input automatically produces poisoned output.** This way, the error message (the content of `error`) travels along the data flow and is never lost.

This means you never need to check for errors manually after every operation — the compiler/runtime automatically tracks the spread of toxicity. When you finally use a poisoned object (e.g., print it), the program reports the error instead of crashing.

#### 11.1.3 Example of the Poisoned Water Model

```text
&io;
-(io::OStream out);
-(std::Number a) << 10./(0); // a is a poisoned object, error = "division by zero"
-(std::Number b) << a.+(1); // b is poisoned too (contagion)
out << b; // printing b outputs the error message
```

In this example, `10./(0)` returns a poisoned Number, so `a` carries the toxicity from the moment of its declaration. The result `b` of the `a + 1` operation is infected as well. Finally, at `out << b`, the program gracefully reports the error rather than crashing.

#### 11.1.4 Poisoned Water vs. Negotiation-Failure Degradation: Two Different Kinds of "Error"
Beginners easily conflate the Poisoned Water Model with the "graceful degradation on negotiation failure" from Chapter III. They are two separate mechanisms handling different errors:

| Dimension | Negotiation-failure degradation (Section 3.3) | Poisoned Water Model (this section) |
| ---- | ---- | ---- |
| Trigger | Type incompatibility at streaming time (e.g., streaming a String into a Number variable) | Runtime computation error (e.g., division by 0) |
| Stage | The stream statement's smart-negotiation stage | The method-execution stage |
| Handling | Smooth degradation: the receiver keeps its zero value, no toxicity is produced (see D.8) | A poisoned object is produced, and the toxicity spreads along the data flow |
| Do you need to handle it? | No — the system bails out automatically | Up to you: let it propagate all the way, or detoxify in place with `_case` |

One sentence to tell them apart: **negotiation failure is "it can't be poured in"; poisoned water is "the computation went wrong, but it keeps flowing."** The former degrades automatically; the latter hands the choice to you (Section 11.2).

### 11.2 The _case Method (Rescue)
When poisoned water travels along the data flow, it eventually has to be "detoxified" somewhere. Synth OOP provides the `_case` function (its formal name) to implement exception catching and handling. `_case` is a built-in method that every object has; it takes a behavior as its argument, and that behavior is the exception-handling behavior.

#### 11.2.1 Basic Usage of _case
`_case` is invoked as: `object._case(behavior)`. The behavior takes one parameter (the exception message) and returns, via its output parameter, a value of the same type as the original object.

```text
-(std::Number result) << 10./(0)._case([
    (error) -> (value) {
        // This behavior is invoked only when the object is poisoned; error is the exception message
        value << 0; // return a default value to detoxify
    }
]);
```

#### 11.2.2 How _case Works
1. If the object calling `_case` is not poisoned (`error` is empty), the behavior is not invoked, and `_case` directly returns the original object's value;
2. If the object calling `_case` is poisoned, the behavior is invoked, with the `error` message passed in;
3. Inside the behavior, you can access the exception message and handle it as you wish;
4. The behavior must return a value through its output parameter, and that value's type must be the same as the type of the object `_case` was called on;
5. `_case` returns the value output by the behavior.

#### 11.2.3 Examples of _case

```text
&io;
-(io::OStream out);
// Basic usage: return a default value when the divisor is 0
-(std::Number result) << 10./(0)._case([
    (error) -> (value) {
        out << "Caught exception: ".+(error);
        value << 0; // return a default value
    }
]);
// Output: Caught exception: division by zero
// result holds the value 0
```

```text
// Chained calls: handle several operations in one place
-(std::Number q) << 10./(2); // q = 5
-(std::Number q2) << q./(0); // division by zero, q2 is poisoned
-(std::Number result) << q2._case([
    (error) -> (value) {
        out << "Chained exception: ".+(error);
        value << -1;
    }
]);
```

```text
// The object returned by _case must have method signatures consistent with the caller object
// The following code fails to compile: _case returns a String, but the caller is a Number
-(std::Number result) << obj._case([
    (error) -> (result) { // method signature mismatch
        result << "error";
    }
]);
```

#### 11.2.4 Pattern Choice of the _case Handler Behavior
Careful readers will notice that the `_case` handler behaviors in this chapter's examples all sign the non-constant pattern `->`. This is not a hard requirement — it's simply the nature of the scenario: handler behaviors usually need to output the error message (accessing and writing `out`) and to read surrounding state to decide the default value — all of which touch the external environment, so `->` is the most convenient. Of course, if your handler behavior only performs pure computation (say, always returning 0), signing the constant pattern `~>` or even the zero-side-effect pattern `=>` is perfectly legal. `_case` imposes no special rule on the handler behavior's pattern; everything follows the general rules of Chapter V.

#### 11.2.5 The Relationship between _case and the Poisoned Water Model
`_case` is the "antidote" to the Poisoned Water Model. The Poisoned Water Model is responsible for tracking and propagating errors, while `_case` is responsible for catching and handling errors at specified locations. The two cooperate as follows:
- The Poisoned Water Model ensures errors are never lost — toxicity travels along the data flow;
- `_case` ensures the program doesn't crash from errors — it catches toxicity at appropriate locations and recovers;
- If toxicity travels all the way to the program's end without being caught by `_case`, the program gracefully reports the error and exits.
This design lets you freely choose the granularity of error handling: check at every step (using `_case`), or let toxicity propagate to the top level for unified handling.
### 11.3 Comprehensive Example: Connecting Everything
The following complete program demonstrates all the core mechanisms described in this document: instantiation expressions, while_ loops, if_ branches, the three levels of behavior patterns, the Poisoned Water Model, and `_case` rescue.

```text
&io;
$Math {
    @divide << [(a, b) -> (q, r) {
        q << a./(b);
        r << a.%(b);
    }];
}
$Program {
    @:: << [() -> () {
        -(io::OStream out);
        -(Math m);
        -(std::Number total) << 0;
        // 1. repeat_ loop: sum 1 through 5
        // The loop body signs the non-constant pattern, directly modifying the caller's env var total
        5.repeat_([(state) -> (next) {
            next << state.+(1);
            total << total.+(next); // sum 1 through 5
        }]);
        // 2. if_ branch: check the size of total
        // Both branch behaviors only read env var total, so signing the constant pattern suffices
        -(std::String msg) << (total.>(10)).if_(
            [() ~> (value) { value << "big"; }],
            [() ~> (value) { value << "small"; }]
        );
        out << msg;
        // 3. Poison water and _case rescue: divide by 0 produces poison, caught locally
        -(std::Number safe) << m.divide(total, 0)._case([
            (error) -> (value) {
                out << "Caught exception: ".+(error);
                value << 0;
            }
        ]);
        out << safe;
    }];
}
```

Expected behavior: repeat_ accumulates 1 to 5 into total (getting 15); if_ checks `15 > 10` which is true, outputs `big`; `m.divide(15, 0)` produces poison water, `_case` catches it and outputs the error message, safe is cured to 0 and then output. The entire program doesn't crash — errors are handled gracefully.
## Appendix A: Syntax Symbol Quick Reference

| Symbol | Name | Usage |
| ---- | ---- | ---- |
| - | Instantiation expression prefix | `-(type name variable name)` declare and create object |
| () | Parentheses | Instantiation expression `-(type name variable name)`; tuple / positional passing `(val1, val2)` |
| << | Left stream | Assignment, stream statements, method binding (`@name << behavior`) |
| >> | Right stream | Stream statement in opposite direction, equivalent to `<<` |
| => | Zero-side-effect pattern | Absolutely no access to any external entities (not even class attributes), zero side effects |
| ~> | Constant pattern | Can access (read-only) caller's environment variables / class members, cannot modify |
| -> | Non-constant pattern | Can access and modify caller's environment variables / class members |
| [ ] | Behavior delimiter | `[(params) behavior-pattern arrow (result) { body }]` |
| @ | Method declaration | `@methodName << behavior;` |
| ! (after type) | Constant variable | `-(std::Number! x)` declare constant variable |
| ! (after @) | Method variable const | Method itself is a variable, `@!` means the binding cannot be rebound |
| # | Constraint prefix / private modifier | `#Addable {...}` declares a constraint; `@#` means private method, can be combined with `!` as `@!#` |
| #...[ ] | Constraint inheritance | `#new [parent] {...}` inherits all method signatures of the parent constraint |
| & | Module import | `&libraryName;` |
| :: | Constructor / namespace | `@::`; `std::` |
| ~ | Destructor | `@~` |
| =: | Publication function | `@=:` |
| := | Reception function | `@:=` |
| = | Assignment method | `a.=(b)` explicit assignment, equivalent to the streaming assignment `a << b` |
| _ | Decomposition placeholder | `-(type, _)` ignore unwanted return values |

Note: `=` is not an operator in Synth OOP — the infix `a = b` is invalid; however, the assignment method `a.=(b)` can be called explicitly and is fully equivalent to the streaming assignment `a << b` (see Section 4.3). All arithmetic and comparison operations are method calls, e.g., `a.+(b)`, `a.-(b)`, `a.*(b)`, `a./(b)`, `a.%(b)`, and `a.<(b)`, `a.>(b)`, `a.<=(b)`, `a.>=(b)`, `a.==(b)`, `a.!=(b)`. See Section 5.5 for details.
## Appendix B: On Computational Capability
Synth OOP is Turing complete. All three ingredients are present:
1. **Conditional branching**: if_ is a method of boolean values, so the ability to choose is established;
2. **Unbounded looping**: while_ is a method of boolean values (receiving a loop body and a condition-check behavior), repeat_ is a method of numbers (receiving a loop count), the body is a behavior, and non-constant patterns allow state modification;
3. **Unbounded storage**: Containers like std::Array can grow infinitely, and the zero-value rule ensures storage is always available.
Branching + looping + unbounded storage = Turing complete. Even if future versions impose additional restrictions on zero-side-effect `=>` or constant `~>` behaviors (such as termination requirements), the language as a whole remains complete.
## Appendix C: Comparison Examples — Synth OOP vs C++
C++ is chosen for comparison for two reasons: Synth OOP's `<<` / `>>` streaming syntax shares the same origin as C++'s iostream; C++ has a mature const system, making it easy to contrast with behavior-pattern semantics.
### C.1 Variables and Output

```cpp
// C++
#include <iostream>
#include <string>
int main() {
    std::string text = "Yahoo!";
    std::cout << text << std::endl;
    return 0;
}
```

```text
// Synth OOP
&io;
$Program {
    @:: << [() -> () {
        -(std::String text) << "Yahoo!";
        -(io::OStream out);
        out << text;
    }];
}
```

Key comparison points:
- C++ uses `=` for initialization; Synth OOP uses `<<` to flow data into variables (`=` is not an operator in Synth OOP);
- C++'s entry point is the global function main; Synth OOP has no global scope — the entry point is the `$Program` object — the compiler instantiates it at startup, execution is its lifetime, and entry logic is written in its constructor behavior.
### C.2 Pure Functions: const Guaranteed by Signature

```cpp
// C++: Pure functions rely on programmer discipline, compiler doesn't enforce
int add3(int a) {
    return a + 3;
}
```

```text
// Synth OOP: Sign the zero-side-effect pattern; the compiler guarantees the behavior never touches the external world
[(a) => (sum) {
    sum << a.+(3);
}]
```

### C.3 const Member Functions vs Behavior Patterns

```cpp
// C++: const is written as a suffix after the parameter list
class Counter {
    int value = 0;
    public:
    int get() const { return value; }
    void inc() { value = value + 1; }
};
```

```text
// Synth OOP: Whether it's const is written on the arrow
$Counter {
    -(std::Number value);
    @get << [() ~> (result) {
        result << value;
    }];
    @inc << [() -> () {
        value << value.+(1);
    }];
}
```

Key comparison: C++'s const is a suffix modifier of member functions; Synth OOP's behavior pattern lives directly on the behavior — `~>` is read-only, `->` can modify members, `=>` is completely isolated — read/write intent is clear at a glance.
### C.4 Multiple Return Values

```cpp
// C++: Need to borrow pair / tuple / struct
#include <utility>
std::pair<int, int> divmod(int a, int b) {
    return {a / b, a % b};
}
```

```text
// Synth OOP: Multiple return values implemented through multiple output parameters, tuples are the language's internal underlying mechanism
$Math {
    @divide << [(a, b) -> (q, r) {
        q << a./(b);
        r << a.%(b);
    }];
}
// Instantiate and call, then decouple the multiple return values — strictly matched by position
-(Math m);
-(std::Number q, std::Number r) << m.divide(10, 3);
```

### C.5 Control Flow

```cpp
// C++: if is a keyword
if (a > b) {
    std::cout << "a wins";
}
```

```text
// Synth OOP: if_ is a method of boolean values, branch behaviors directly access caller's environment variables
-(std::Number a) << 10;
-(std::Number b) << 20;
-(std::Number result) << (a.>(b)).if_(
    [() ~> (value) { value << a; }],
    [() ~> (value) { value << b; }]
);
```

Key comparison: C++'s control flow is a syntax structure; Synth OOP's control flow is a method of boolean values — it can be passed around, composed, and deferred just like data. if_ itself has a return value, returning the value of the output parameter `value` from the executed branch. Branch behaviors directly access the caller's environment variables through behavior patterns, without needing any extra parameter passing.
### C.6 Instantiation Expressions

```cpp
// C++: Declaration and assignment are two steps
int a = 10;
std::cout << a;
```

```text
// Synth OOP: Declaration is an expression, value is the newly created object
&io;
-(io::OStream out);
-(std::Number a) << 10;
out << (-(std::Number b) << 20); // b's declaration expression value directly participates in flow
```

Key comparison: C++'s declaration is a statement (statement), which produces no value; Synth OOP's declaration is an expression (expression), which inherently has a value and can directly participate in data flow.
### C.7 Tuple Chaining

```cpp
// C++: Need to borrow std::tie / std::forward_as_tuple
auto [x, y] = std::tie(a, b);
```

```text
// Synth OOP: Tuples are language built-in constructs, can be transparently passed as method arguments
$Math {
    @divide << [(a, b) -> (q, r) {
        q << a./(b);
        r << a.%(b);
    }];
    @process << [(x, y) -> () { /* receive two independent arguments */ }];
}
-(Math m);
m.process(m.divide(10, 3));
```

Key comparison: C++ needs to borrow tuple / pair and other utility types to package multi-return values; Synth OOP's tuples are language built-in constructs, represented syntactically with `(...)` directly, and can be transparently passed as method arguments without any extra wrapping or unwrapping operations.
## Appendix D: Compiler Self-Test Examples (Acceptance Tests)
This appendix is a set of acceptance tests for compiler developers. The syntax is basically frozen — please use these test cases to verify the compiler: each group provides expected results — either expected output or expected compilation error. If all pass, it means the compiler correctly implements the currently frozen syntax.
### D.1 Zero-Value Initialization

```text
&io;
$Program {
    @:: << [() -> () {
        -(std::Number num);
        -(io::OStream out);
        out << num;
    }];
}
```

Expected: Output `0`. Variables are automatically zero-value initialized at the moment of declaration.
### D.2 Streaming Assignment and Output

```text
&io;
$Program {
    @:: << [() -> () {
        -(std::String s) << "Yeah.";
        -(io::OStream out);
        out << s;
    }];
}
```

Expected: Output `Yeah.`
### D.3 Instantiation Expressions (Declaration is an Expression)

```text
&io;
$Program {
    @:: << [() -> () {
        -(io::OStream out);
        // The declaration expression itself has a value, can directly participate in stream operations
        out << (-(std::String msg) << "Hello, World!");
    }];
}
```

Expected: Output `Hello, World!`. The instantiation expression `(-(std::String msg) << "Hello, World!")`'s value is the msg object itself, which can be directly consumed by `out <<`.
### D.4 Compile-Time Interception of the Constant Pattern (Edge Case)

```text
$Program {
    @:: << [() -> () {
        -(std::Number outer) << 1;
        [() ~> () { outer << 2; }]; // ~> behavior tries to modify external variable outer
    }];
}
```

Expected: **Compilation failure**. `~>` behaviors can read external variables, but attempting to modify `outer` violates the constant pattern — the compiler must emit an error. This group verifies whether the "read-only" check of the constant pattern is truly effective.
### D.5 Compile-Time Interception of the Zero-Side-Effect Pattern (Edge Case)

```text
$Program {
    @:: << [() -> () {
        -(std::Number outer) << 1;
        [() => (v) { v << outer; }]; // => behavior not even allowed to read outer
    }];
}
```

Expected: **Compilation failure**. `=>` behaviors absolutely cannot access any external entities — merely reading `outer` must also produce an error. This group verifies whether the complete isolation of the zero-side-effect pattern is truly effective.
### D.6 Constant Interception (Edge Case)

```text
$Program {
    @:: << [() -> () {
        -(std::Number! frozen) << 7;
        frozen << 8; // Attempting to modify a constant
    }];
}
```

Expected: **Compilation failure**. Modifying a constant marked with `!` — the compiler rejects it.
### D.7 Non-Constant Pattern Accessing Class Members

```text
$Counter {
    -(std::Number value);
    @inc << [() -> () {
        value << value.+(1);
    }];
}
```

Expected: Compilation success. `->` behaviors inside a class as methods can access and modify members.
### D.8 Graceful Degradation on Negotiation Failure (Edge Case)

```text
&io;
$Program {
    @:: << [() -> () {
        -(std::Number x) << "not a number";
        -(io::OStream out);
        out << x;
    }];
}
```

Expected: Program doesn't crash. Stream negotiation fails, graceful degradation, x retains zero value, outputs `0`. This group verifies whether "no implicit coercive conversion, graceful degradation" is implemented.
### D.9 Multiple Return Values (Multiple Output Parameters) Parsing

```text
$Math {
    @divide << [(a, b) -> (q, r) {
        q << a./(b);
        r << a.%(b);
    }];
}
$Program {
    @:: << [() -> () {
        // Directly decouple multi-return values — strictly matched by position
        -(Math m);
        -(std::Number q, std::Number r) << m.divide(10, 3);
    }];
}
```

Expected: Compilation success, tuple creation and structural binding decomposition parsed correctly.
### D.10 Tuple Chaining: Multiple Return Values Directly Passed to Another Method

```text
$Math {
    @divide << [(a, b) -> (q, r) {
        q << a./(b);
        r << a.%(b);
    }];
    // Define a method that accepts separate parameters
    @process << [(x, y) -> () {
        // x is quotient, y is remainder
    }];
}
$Program {
    @:: << [() -> () {
        // Pass divide's multiple return values directly to process — aligned by position
        -(Math m);
        m.process(m.divide(10, 3));
    }];
}
```

Expected: Compilation success, tuple chaining (position alignment) parsed correctly.
### D.11 Class and Inheritance Parsing

```text
$Human {
    -(std::String name);
}
$Student [Human] {
    -(std::Number age);
}
```

Expected: Compilation success, inheritance chain Human → Student correctly established.
### D.12 Parent as Instance (Edge Case)

```text
$Student [Human] {
    -(std::Number age);
    -(std::String major);
    @:: << [(a, n, m) -> () {
        age << a;
        name << n; // name is inherited from Human
        major << m;
    }];
}
-(Student s);
s.name << "Alice";
s.age << 22;
s.major << "Computer Science";
$GraduateStudent [s] {
    @addResearch << [(topic) -> () {
        name << name.+(" (Research: ").+(topic).+(")");
    }];
}
```

Expected: Compilation success, new class GraduateStudent derived from instance s's current state, automatically inheriting members name="Alice", age=22, major="Computer Science" and their values, with parent class Student (which in turn inherits Human).
### D.13 Reserved Method Names Parsing

```text
$Channel {
    @:: << [() -> () {}];
    @~ << [() -> () {}];
    @=: << [() -> (result) { result << "published"; }];
    @:= << [(msg) -> () {}];
}
```

Expected: Compilation success, four reserved method names `::`, `~`, `=:`, `:=` all parsed correctly.
### D.14 Instantiation Expressions Participating in Method Calls

```text
$Calculator {
    -(std::Number value);
    @add << [(x) -> () {
        value << value.+(x);
    }];
    @get << [() ~> (result) {
        result << value;
    }];
}
$Program {
    @:: << [() -> () {
        -(Calculator c);
        // Instantiation expression directly calls method
        (-(Calculator c2)).add(10).get();
    }];
}
```

Expected: Compilation success, instantiation expression return value can directly call methods, forming chain calls.
### D.15 Tuple Decomposition Placeholder `_`

```text
$Math {
    @divide << [(a, b) -> (q, r) {
        q << a./(b);
        r << a.%(b);
    }];
}
$Program {
    @:: << [() -> () {
        // Only care about quotient, use _ as placeholder for remainder — strictly matched by position
        -(Math m);
        -(std::Number q, _) << m.divide(10, 3);
    }];
}
```

Expected: Compilation success, `_` placeholder correctly ignores unwanted return values.
### D.16 Control Flow and Environment Variables

```text
&io;
$Program {
    @:: << [() -> () {
        -(io::OStream out);
        // repeat_ loop body receives state and returns a new state of the same type
        -(std::Number last) << 3.repeat_(
            [(state) -> (next) {
                next << state.+(1); // loop body: state accumulates from 0 to 3
            }]
        );
        out << last;
        // if_ branch behaviors only read env var, so signing the constant pattern suffices
        -(std::Number picked) << (last.>(2)).if_(
            [() ~> (value) { value << last; }],
            [() ~> (value) { value << 0; }]
        );
        out << picked;
    }];
}
```

Expected: Output `3` and `3`. repeat_ executes 3 times (state accumulates from 0 to 3), returning the last loop-body state (3); if_ checks `last.>(2)` which is true, returning the true branch's value.
### D.17 Edge Case Test Summary

| Test Case | Verification Target | Expected |
| ---- | ---- | ---- |
| D.4 | Constant pattern read-only check | Compilation error |
| D.5 | Zero-side-effect pattern complete isolation | Compilation error |
| D.6 | `!` constant interception | Compilation error |
| D.8 | Graceful degradation on negotiation failure | No crash, output zero value |
| D.12 | Derive class from instance | Compilation success, derived from instance state |
| D.14 | Instantiation expression chain calls | Compilation success |
| D.15 | Tuple decomposition placeholder | Compilation success |
| D.16 | Control flow access/modify environment variables | Output 3 and 3 |

### D.18 Tuple Objectification Related Tests
#### D.18.1 Tuple Literal Initialization

```text
$Program {
    @:: << [() -> () {
        -(std::Tuple t) << (10, "Alice", true);
    }];
}
```

Expected: Compilation success, tuple literal correctly created and assigned.
#### D.18.2 Tuple Structural Decomposition

```text
$Program {
    @:: << [() -> () {
        -(std::Tuple t) << (10, "Alice", true);
        -(std::Number x) << t.get(0);
        -(std::String name) << t.get(1);
        -(std::Boolean flag) << t.get(2);
    }];
}
```

Expected: Compilation success, tuple elements accessed correctly by index.
#### D.18.3 Tuple Decomposition Type Mismatch (Edge Case)

```text
$Program {
    @:: << [() -> () {
        -(std::Tuple t) << (10, "Alice", true);
        -(std::Number name) << t.get(1); // t.get(1) is String, cannot assign to Number
    }];
}
```

Expected: **Compilation failure**. `t.get(1)` returns String type, receiving variable declared as Number — method signature mismatch.
#### D.18.4 Tuples as Method Arguments and Return Values

```text
&io;
$Point {
    @make << [(x, y) -> (result) {
        result << (x, y);
    }];
    @print << [(point) -> () {
        out << point.get(0);
        out << point.get(1);
    }];
}
$Program {
    @:: << [() -> () {
        -(io::OStream out);
        -(Point p);
        -(std::Tuple t) << p.make(5, 15);
        p.print(t);
    }];
}
```

Expected: Compilation success, tuples correctly passed as method arguments and return values.
### D.19 Syntax Points Discovered During Test Case Review
During the process of organizing test cases, the following syntax points that are not yet defined were exposed, to be filled in subsequent versions:
1. The complete syntax for deriving classes from instances (D.12) — this has been fully defined in Sections 8.2 and 8.3, compiler developers can refer to the implementation directly.
Note: Object instantiation syntax (Chapter IV) and control flow call syntax (Chapter VII) are fully defined in the main text — compiler developers can refer to the implementation directly, no need to wait for subsequent versions. Tuple creation and decomposition syntax is fully defined in Section 5.4 and is no longer listed in the pending list. std::Array and std::Dict initialization methods (`[...]` and `{...}` literals) are not yet fully defined in the main text and remain to be filled in subsequent versions.

---

## Appendix E: Language Semantics (Implementation Notes for v1.28)

> Moved out of `README.md` (2026-08-29). This appendix records the
> language-level semantics **as the current interpreter implements them**
> — decisions that narrow or clarify the specification in `doc/`. Where
> this appendix and an earlier chapter disagree, this appendix wins.
>
> Standard-library method signatures live in
> [`Syclun标准库参考.md`](./Syclun标准库参考.md).

- **Colon principle (lexer-enforced).** Outside of the constructor `::`, a
  bare `:` is **never** allowed. The only legal colon sequences are `::`
  (namespace / constructor, e.g. `maths::Maths`, `Program::@::`), `=:`
  (publish), and `:=` (receive). Any other `:` is rejected at lex time with
  `Unexpected ':' (colons are only allowed in '::', '=:', or ':=').`
- **Tuple-first destructuring (single receiver).** When a *single* scalar
  variable is bound to a tuple — via the `=` method or a flow `<<`/`>>` — the
  variable receives the tuple's **first** element. Container/stream receivers
  (`Array`, `Tuple`, etc.) keep the whole value, so an `async` Reactor can
  still return a tuple-of-tuples intact.
- **0-based indexing.** `Array`/`Tuple`/`String` numbering and indexing start
  at **0**, not 1. `arr.get(0)` is the first element; `arr.remove(0)` removes
  it; a `structs` `Queue`'s `pop()` returns the FIFO front.
- **`void` is no longer a keyword — use empty `()` for "no value".** `void`
  was retired as a type / name / output / constraint token. To express "no
  return" or "no output" write an **empty parameter/return list** — e.g.
  `-> ()` or `() -> ()`. A behavior whose return list is empty publishes no
  value (so `out << behaviorReturningNothing` prints nothing), and every output
  it does list is published as-is. Writing `void` where a type, variable, output,
  or constraint is expected is now a **syntax error**:
  `'void' is not a type or name; use empty parentheses '()' …`. The rule is
  enforced in `parser.hpp` (it rejects `void` in type, variable, output, and
  constraint positions).

- **Self method calls must use `self.` (bare names mean data).** An instance
  method MUST be invoked through `self.name(args)` — e.g. `self.greet()`,
  `self.squared(6)`. A bare `name(args)` is now an **error**: it is resolved as
  DATA (a local variable / member), never as a hidden self-dispatch, which keeps
  the data/behavior boundary clean. The `self` keyword refers to the current
  instance (`self.n` reads a member). The constructor `@::` also has a `self` and
  may call the instance's own methods. (Previously a bare `name(args)` silently
  dispatched to self; that ambiguity is gone.)

- **Re-declaring a variable is an error (not silent).** Declaring a name that
  already exists in the current scope — including the input/output parameters the
  interpreter pre-declares for a behavior — raises
  `InterException: variable '<x>' is already declared in this scope`. Top-level
  variable definitions are still rejected by the parser
  (`Top level only allows module import ...`).

- **A class definition may end with an optional `;`.** `$Class { … }` and
  `$Class { … };` are both accepted — a class definition is a statement.

- **Constructor init syntax `-(Type(args)! name)`.** A variable can be built
  directly from constructor arguments: `-(std::Number(5)! n)` creates a const
  `std::Number` whose value is `5`; `-(std::String("hello")! s)` builds a
  `String` "hello"; `-(std::Array((1,2,3))! a)` fills an `Array` from a tuple.
  The implementation invokes the class's constructor: a class with its own
  `@::(args) -> ()` runs that; otherwise a single argument is duck-typed
  into the instance through the normal flow negotiation (so every existing class
  gets a working single-arg constructor for free). Object's no-op default `::`
  is intentionally *not* inherited by derived types, so constructor arguments
  are never silently dropped. Each core scalar / collection class
  (`std::Number`, `std::Boolean`, `std::String`, `std::Array`, `std::Dict`,
  `std::Tuple`) now ships an explicit `@::(value) -> ()` constructor that
  reuses its proven `:=` receive closure, so `-(std::Number(5)! n)` is exactly
  equivalent to constructing the object and then receiving `5`.

- **Const variables: build via constructor, never via flow.** A const variable
  (`!(...)`) may only be initialized by a constructor (`-(std::Number(5)! n)`);
  assigning it through a trailing flow (`-(std::Number! n) << 5`) raises a
  `ConstException`, and a const member may not be overwritten by a flow inside a
  behavior. This fixes the earlier bug where `-(std::Number! int); outer << int;`
  printed a stream of zeros.

- **No assignment to rvalues.** The left side of a flow (`<<`) or of the assign
  method (`.=`) must be a variable, never an expression result. `1.+(1) << 0` and
  `1.+(1).=(0)` are both `InterException`s — the left side is a constant rvalue,
  not a variable.

- **IEEE 754 float semantics + `NaN`/`Infinity` keywords.** Division by zero
  yields `+Infinity` (or `-Infinity` for a negative numerator) and `0/0` (and
  other indeterminate forms) yields `NaN` — no poison. `NaN` and `Infinity`
  (also `inf`) are keywords that parse to real `std::Number` instances carrying
  the IEEE non-finite value (exactly like `13` is a `std::Number`), so they can
  be stored, compared, and printed. Comparison against `Infinity`/`NaN` follows
  IEEE rules (`NaN` compares false).

- **Recursion guard.** A runaway self-recursion is force-interrupted at
  `RECURSION_LIMIT` (default 1000; the native stack is enlarged to 32 MB at link
  time so the guard fires cleanly) and raises `RecursionLimitException`; the
  whole call stack is unwound and no value is produced. This replaces a stack
  overflow / hang.

- **g++-style diagnostics with a full execution stack.** Semantic errors
  (undefined names, wrong self-call, const/rvalue violations, recursion limit,
  type-mismatch at a call boundary, missing method, …) raise *immediately* at
  the source rather than silently propagating a "poisoned" value. Each error is
  printed in a g++-style format:
  ```
  build/_case.syn:5:1: error: RuntimeException: Method '_case' not found.
      | 1._case();
      | ^~~~
  note: execution stack:
      1. in '@::' (constructor of 'Program') [build/_case.syn:3:1]
  ```
  The execution stack lists every user method / constructor frame on the call
  path, so the failure is traceable instead of a bare crash. Color is enabled by
  default and can be turned off with `NO_COLOR=1` (the diagnostic text is
  unchanged). Arithmetic faults (divide by zero) follow IEEE 754 and never raise.

- **Argument-type constraint violations raise an immediate `TypeException`.**
  Feeding an argument whose runtime type disagrees with a method's declared
  parameter type is rejected *at the call boundary* with
  `TypeException: Argument type mismatch for method '…': expected '…', got '…'`
  — there is no longer any silent "poison water" degradation. The check is
  wired through `rt_basic::g_sign_enforcer` / `enforce_sign` and covers every
  native / built-in method (flow & lifecycle methods, `std::Object` / `@` / `...`
  parameters, and the heterogeneous `value` tag are exempt). A missing method
  raises `RuntimeException: Method '…' not found` immediately.
