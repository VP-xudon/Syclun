// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/async.hpp
//
// Standard library: async (C++-backed backend).
// 标准库：async（C++ 底层实现）。
//
// A modern async runtime built on top of detached worker threads and
// std::promise / std::future. The `$Reactor` runs a batch of closures with:
//   1. Lifecycle control   — cancellation (best-effort) and timeouts.
//   2. Concurrency control — max_concurrency limits (backpressure).
//   3. Fault tolerance     — per-task error isolation + Error objects.
//   4. Dynamic spawning    — spawn()/submit() return a `Task` handle.
//   5. Timers / scheduling — async_sleep() returns a non-blocking Task.
// `$Task` is a future-like handle; `$Error` carries failure info. The C++
// twin of lib/async.synl. Self-registered under "async".
//
// D6 threading model (industrialization audit): all Synth-OOP evaluation is
// serialized under the interpreter GIL. Workers acquire it inside
// run_closure_isolated; the main thread releases it around every blocking
// wait. All fire-and-forget launches go through launch_detached — NEVER
// std::async on the GIL-holding thread (see launch_detached's comment).
// 基于分离工作线程与 std::promise / std::future 的现代异步运行时。
// `$Reactor` 以如下能力批量执行闭包：① 生命周期控制（取消与超时）
// ② 并发度控制（max_concurrency 背压）③ 容错（逐任务异常隔离 + Error
// 对象）④ 动态派发（spawn/submit 返回 Task）⑤ 定时器（async_sleep 返回
// 非阻塞 Task）。`$Task` 为类 future 句柄，`$Error` 承载失败信息。
// 以 "async" 自注册。
//
// D6 线程模型（工业化审计）：全部 Synth-OOP 求值在解释器 GIL 下串行化。
// 工作线程在 run_closure_isolated 内取锁；主线程在每个阻塞等待期间放锁。
// 所有发射后不管的派发一律走 launch_detached——在持 GIL 线程上绝不可用
// std::async（见 launch_detached 注释）。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <optional>
#include <functional>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_async {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // --------------------------------------------------------
    // Global task registry (for dynamically spawned Tasks).
    // The future is a shared_future: await()/result() copy it, so the
    // registry entry stays valid for repeated, non-consuming queries and
    // concurrent waits (a std::future is move-only — moving it out of the
    // map left later result() calls holding an invalid future).
    // 全局任务注册表（动态派发的 Task 句柄）。future 用 shared_future：
    // await()/result() 复制它，注册表条目因此可重复、无消耗地查询，
    // 且支持并发等待（std::future 只能移动——把它移出注册表会让
    // 之后的 result() 拿到无效 future）。
    // --------------------------------------------------------
    struct TaskHandle {
        std::shared_future<rt_basic::InstanceListPtr> fut;
        std::shared_ptr<std::atomic<bool>> cancelled;
    };
    static std::recursive_mutex        g_task_mux;
    static std::unordered_map<long long, TaskHandle> g_tasks;
    static long long         g_task_id = 0;

    // Bounded registry: entries are reclaimed by await() / dispose(); tasks
    // that are NEVER awaited or disposed are swept oldest-first once the
    // registry reaches this cap. This replaces the old per-instance
    // on_release erasure, which was incompatible with value-copied handles:
    // a `-(async::Task t) << r.spawn(...)` flow drops the temporary handle
    // right after the copy, and erasing on that drop killed the LIVE task
    // (await then failed with "unknown task id"). Handles are ids, not
    // identities, so instance-level reclamation cannot know it holds the
    // last reference — the registry itself must bound its growth instead.
    // 有界注册表：条目由 await() / dispose() 回收；从不 await 也从不
    // dispose 的任务在注册表达到上限后按最旧优先清除。此举取代旧的
    // 逐实例 on_release 清除——后者与「值复制的句柄」不相容：
    // `-(async::Task t) << r.spawn(...)` 会在复制完成后立刻丢弃临时
    // 句柄，若在丢弃时清除注册表，等于杀死存活任务（await 随即报
    // "unknown task id"）。句柄是 id 而非身份，实例级回收无从得知
    // 自己是否最后一个引用——增长上限只能由注册表自身约束。
    static constexpr std::size_t kMaxTrackedTasks = 1024;

    // --------------------------------------------------------
    // Detached launch — the D6 deadlock killer.
    //
    // NEVER use std::async on the GIL-holding thread here. In libstdc++ the
    // blocking join of an async task does NOT live in the future's
    // destructor but in the shared state's (_Async_state_impl) — it runs
    // whenever the LAST reference to the shared state drops. If that last
    // drop happens on the main thread while it holds the GIL and the worker
    // still needs the GIL to finish, the main thread joins a thread that
    // waits for the lock its holder waits on: a textbook self-deadlock
    // (repro: with_timeout's timeout path hung forever). .share() does not
    // help — the rule follows the shared state, not the future handle.
    //
    // A detached thread owning its own promise has no join anywhere: the
    // thread fulfils the promise and dies on its own schedule; every
    // reference holder may drop at any time, GIL held or not. An abandoned
    // result is simply discarded when the thread eventually completes.
    //
    // 分离式派发——D6 死锁的终结者。
    //
    // 在持有 GIL 的线程上绝不可用 std::async。libstdc++ 中异步任务的
    // 阻塞 join 不在 future 析构里，而在共享状态（_Async_state_impl）
    // 中——只要共享状态的**最后一个引用**销毁就会执行。若最后一次
    // 销毁发生在仍持有 GIL 的主线程上、而工作线程还需要 GIL 才能结束，
    // 主线程就会 join 一个正在等它手中锁的线程：教科书级自死锁
    //（复现：with_timeout 的超时路径永久挂起）。.share() 无济于事——
    // 规则跟着共享状态走，不跟 future 句柄走。
    //
    // 自持 promise 的分离线程没有任何 join：线程自行完成 promise 并按
    // 自己的节奏结束；任何引用持有者都可以随时丢弃，无论是否持有
    // GIL。被弃置的结果在线程最终完成时自然丢弃。
    // --------------------------------------------------------
    inline std::shared_future<rt_basic::InstanceListPtr> launch_detached(
        std::function<rt_basic::InstanceListPtr()> fn
    ) {
        auto promise = std::make_shared<
            std::promise<rt_basic::InstanceListPtr>>();
        auto fut = promise->get_future().share();
        std::thread([fn = std::move(fn), promise]() mutable {
            promise->set_value(fn());
        }).detach();
        return fut;
    }

    // Build an Error object (kind + message).
    // 构造 Error 对象（kind + message）。
    inline RuntimeObjectPtr make_error(const std::string& kind, const std::string& msg) {
        auto err = ::stdRT.make("Error");
        auto* cls = dynamic_cast<RuntimeClass*>(err.get());
        if (cls) {
            auto& am = cls->get_attributes();
            // Hidden-capsule keys: the public names `kind`/`message` are the
            // METHODS (the documented API is `kind()` / `message()`), and
            // member lookup resolves attributes before methods — public
            // attribute keys would shadow them, so `err.kind()` would resolve
            // `err.kind` to a String and then fail to call it. `#` keys are
            // the same convention Task uses for its `#value` id capsule.
            // 隐藏胶囊键：公开名 `kind`/`message` 是**方法**（文档 API 为
            // `kind()` / `message()`），而成员查找先属性后方法——公开属性键
            // 会遮蔽方法，令 `err.kind()` 把 `err.kind` 解析成 String 再对
            // 其调用而报错。`#` 键与 Task 存放 id 的 `#value` 胶囊同一约定。
            am["#kind"]    = rb::make_string(kind);
            am["#message"] = rb::make_string(msg);
        }
        return err;
    }

    // Run a closure object on a thread, isolating any C++ exception into an
    // Error capsule so the reactor never crashes. A `NativeError` raised by a
    // native method (the retired poison-water path) is converted into that
    // capsule; other C++ exceptions are wrapped the same way. (Hard
    // interpreter errors — missing methods, type violations — take the
    // immediate fatal-diagnostic path by design (v1.30) and are NOT isolated
    // here: they terminate the process wherever they occur.)
    // 在线程上运行一个闭包对象，把任何 C++ 异常隔离为 Error 胶囊，
    // 使反应堆绝不崩溃。原生方法抛出的 NativeError（毒水退役后的错误路径）
    // 同样转为该胶囊；其余 C++ 异常同等处理。（解释器硬错误——方法缺失、
    // 类型违犯——按 v1.30 设计走即时致命诊断路径，不在此隔离：无论发生
    // 在何处都会终止进程。）
    inline rt_basic::InstanceListPtr run_closure_isolated(
        const RuntimeObjectPtr& clos
    ) {
        try {
            rt_basic::InstanceMap localEnv;
            // D6: hold the GIL while evaluating Synth-OOP code on a worker
            // thread, so it can never race the main thread's evaluation. Depth
            // is exactly one here (this is the worker's single entry), matching
            // the main thread's single GIL hold from run_program.
            // D6：在工作线程求值 Synth-OOP 代码时持有 GIL，使之绝不
            // 与主线程的求值发生竞争。此处深度恰为 1（工作线程唯一入口），
            // 与主线程 run_program 的单一持锁一致。
            rt_builtin::GILScope gil;
            auto out = rb::call_behavior(clos, localEnv, rb::empty_result());
            return out ? out : rb::empty_result();
        } catch (const rt_builtin::NativeError& e) {
            return rb::list_of({make_error("exception", e.what_msg)});
        } catch (const std::exception& e) {
            return rb::list_of({make_error("exception",
                std::string("task threw: ") + e.what())});
        } catch (...) {
            return rb::list_of({make_error("exception", "task threw an unknown exception")});
        }
    }

    // D6: a blocking wait must release the GIL so other (worker) tasks can
    // acquire it and make progress. These are only ever called from a thread
    // that currently holds the GIL exactly once.
    // D6：阻塞等待时必须释放 GIL，使其他（工作线程）任务能取锁推进。
    // 仅当调用线程恰持锁 1 次时调用。
    inline void gil_release_for_wait() {
        rt_builtin::gil_mutex().unlock();
    }
    inline void gil_reacquire_after_wait() {
        rt_builtin::gil_mutex().lock();
    }

    // Build a result tuple (status, payload) — the discriminated union used
    // by the batch Reactor. status ∈ {"ok","error","timeout","cancelled"}.
    // 构造结果元组 (status, payload) —— 批量反应堆用的判别联合。
    // status ∈ {"ok","error","timeout","cancelled"}。
    inline RuntimeObjectPtr make_result(const std::string& status, RuntimeObjectPtr payload) {
        return rb::make_tuple({rb::make_string(status), payload});
    }

    // A `Task` object wrapping a running (or finished) task.
    // 包裹运行中（或已完成）任务的 `Task` 对象。
    inline RuntimeObjectPtr make_task(long long id) {
        auto task = ::stdRT.make("Task");
        auto* cls = dynamic_cast<RuntimeClass*>(task.get());
        if (cls) {
            auto& am = cls->get_attributes();
            am["id"]        = rb::make_number(static_cast<double>(id));
            // Stash the registry id in #value so it survives publish/receive
            // (a `-(async::Task t) << r.spawn(...)` keeps its identity).
            // 把注册表 id 存进 #value，使之能经公布/接收存活
            // （`-(async::Task t) << r.spawn(...)` 仍保留身份）。
            am["#value"]    = rb::make_number(static_cast<double>(id));
            am["cancelled"] = rb::make_boolean(false);
        }
        return task;
    }

    // Resolve a Task's registry id, falling back to the #value capsule that
    // survives publish/receive. Returns nullopt when there is no id.
    // 解析 Task 的注册表 id，回退到经公布/接收存活的 #value 胶囊；
    // 无 id 时返回 nullopt。
    inline std::optional<long long> task_id(rt_basic::InstanceMap& env) {
        long long id = 0;
        auto it = env.find("id");
        if (it != env.end()) {
            auto v = rb::number_of(it->second);
            if (v) id = static_cast<long long>(*v);
        }
        if (id <= 0) {
            auto vit = env.find("#value");
            if (vit != env.end()) {
                auto v = rb::number_of(vit->second);
                if (v) id = static_cast<long long>(*v);
            }
        }
        if (id <= 0) return std::nullopt;
        return id;
    }

    // Spawn a lambda as a Task (used by spawn + async_sleep).
    // 把 lambda 派发为 Task（spawn 与 async_sleep 共用）。
    inline RuntimeObjectPtr spawn_lambda(
        std::function<rt_basic::InstanceListPtr()> fn
    ) {
        long long id = 0;
        std::shared_ptr<std::atomic<bool>> cancelled =
            std::make_shared<std::atomic<bool>>(false);
        auto cancelledCp = cancelled;
        auto fut = launch_detached(
            [fn, cancelledCp]() -> rt_basic::InstanceListPtr {
                if (cancelledCp->load()) {
                    return rb::list_of({make_error("cancelled", "task cancelled before start")});
                }
                return fn();
            }
        );
        {
            std::lock_guard<std::recursive_mutex> lk(g_task_mux);
            // Bounded registry: first sweep tasks that already FINISHED and
            // were never awaited (their results are no longer retrievable
            // once swept — but they were abandoned), then, if still at cap,
            // evict the oldest entries regardless of state. Evicted entries
            // hold only a shared_future: dropping it never joins or blocks —
            // the detached worker owns its own promise and finishes on its
            // own, its result simply discarded.
            // 有界注册表：先清除已完成却从未被 await 的任务（清除后其结果
            // 不再可取——但它们本就被弃置）；若仍达上限，则不论状态按最旧
            // 优先驱逐。被驱逐条目只持有 shared_future：丢弃它绝不 join
            // 或阻塞——分离的工作线程自持 promise、自行结束，结果直接
            // 丢弃。
            if (g_tasks.size() >= kMaxTrackedTasks) {
                for (auto git = g_tasks.begin(); git != g_tasks.end();) {
                    if (git->second.fut.wait_for(std::chrono::seconds(0)) ==
                        std::future_status::ready) {
                        git = g_tasks.erase(git);
                    } else {
                        ++git;
                    }
                    if (g_tasks.size() < kMaxTrackedTasks) break;
                }
                while (g_tasks.size() >= kMaxTrackedTasks) {
                    g_tasks.erase(g_tasks.begin());
                }
            }
            id = ++g_task_id;
            g_tasks[id] = TaskHandle{fut, cancelled};
        }
        return make_task(id);
    }

    // Wait for a task's future; returns its InstanceListPtr or an Error tuple
    // when it times out / was cancelled. `erase` removes the handle afterwards.
    // 等待任务 future；超时或已取消时返回 Error 元组。erase 为真时取走句柄。
    inline RuntimeObjectPtr await_task(
        long long id, long long timeoutMs, bool erase
    ) {
        // shared_future copy — non-consuming: the entry (if not erased)
        // stays valid for repeated result()/await() queries.
        // shared_future 复制——无消耗：条目（若未被 erase）仍可被
        // 后续的 result()/await() 重复查询。
        std::shared_future<rt_basic::InstanceListPtr> fut;
        std::shared_ptr<std::atomic<bool>> cancelled;
        {
            std::lock_guard<std::recursive_mutex> lk(g_task_mux);
            auto it = g_tasks.find(id);
            if (it == g_tasks.end()) {
                return make_result("error", make_error("unknown", "unknown task id"));
            }
            fut = it->second.fut;          // copy, never move / never consume
            cancelled = it->second.cancelled;
            if (erase) g_tasks.erase(it);
        }
        if (cancelled && cancelled->load()) {
            return make_result("cancelled", make_error("cancelled", "task was cancelled"));
        }
        bool timedOut = false;
        rt_basic::InstanceListPtr out;
        // D6: release the GIL during the blocking wait so a worker task (which
        // needs the GIL to evaluate) can run and finish. Reacquire afterwards.
        // D6：阻塞等待期间释放 GIL，使需要 GIL 来求值的工作线程能运行并结束；
        // 之后重新取回。
        gil_release_for_wait();
        if (timeoutMs > 0) {
            auto status = fut.wait_for(std::chrono::milliseconds(timeoutMs));
            if (status == std::future_status::timeout) timedOut = true;
            else out = fut.get();
        } else {
            out = fut.get();
        }
        gil_reacquire_after_wait();
        if (timedOut) {
            return make_result("timeout", make_error("timeout", "await timed out"));
        }
        // An Error result from the closure becomes an "error" payload.
        // Check the etag on the value itself: an Error is a plain object
        // (no `#value` capsule), so unwrap() here would only yield nullptr.
        // 闭包返回的 Error 转为 "error" 负载。直接对值本身做 etag 判定：
        // Error 是普通对象（无 `#value` 胶囊），unwrap() 在此只会返回
        // 空指针。
        if (out && !out->empty()) {
            auto val = (*out)[0];
            if (val && rb::is_error(val)) {
                return make_result("error", val);
            }
        }
        return make_result("ok", rb::make_tuple(out ? *out : std::vector<RuntimeObjectPtr>{}));
    }

    // ========================================================
    // $Reactor — batch runner with the five production dimensions.
    // $Reactor —— 具备五个生产级维度的批量执行器。
    // ========================================================

    // reactor.set(tasks) -> (void)
    // reactor.set(tasks) -> (void) 保存闭包元组。
    inline rt_basic::Callable method_reactor_set() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto tasks = rb::para_at(paras, 0);
                if (tasks) env["closures"] = tasks;
                return rb::empty_result();
            },
            rb::make_sign("set", {{"tasks", "std::Array"}}, {})
        );
    }

    // reactor.set_limit(max) -> (void)  — max_concurrency (0 = unlimited).
    // reactor.set_limit(max) -> (void) —— 并发上限（0 表示不限）。
    inline rt_basic::Callable method_reactor_set_limit() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto m = rb::number_of(rb::para_at(paras, 0));
                env["max_concurrency"] = rb::make_number(m ? *m : 0, true);
                return rb::empty_result();
            },
            rb::make_sign("set_limit", {{"max", "std::Number"}}, {})
        );
    }

    // reactor.set_timeout(ms) -> (void)  — per-task default timeout.
    // reactor.set_timeout(ms) -> (void) —— 每个任务的默认超时（毫秒）。
    inline rt_basic::Callable method_reactor_set_timeout() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto t = rb::number_of(rb::para_at(paras, 0));
                env["timeout"] = rb::make_number(t ? *t : 0, true);
                return rb::empty_result();
            },
            rb::make_sign("set_timeout", {{"ms", "std::Number"}}, {})
        );
    }

    // reactor.cancel() -> (void)  — best-effort cancellation flag.
    // reactor.cancel() -> (void) —— 尽力而为的取消标志。
    inline rt_basic::Callable method_reactor_cancel() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                env["cancelled"] = rb::make_boolean(true);
                return rb::empty_result();
            },
            rb::make_sign("cancel", {}, {})
        );
    }

    // reactor.start(timeout?) ~> (results)
    // Each result is a (status, payload) tuple; concurrency-limited with a
    // counting semaphore, per-task timeout, error isolation, cancellation.
    // 每个结果为 (status, payload) 元组；以计数信号量限制并发、带逐任务超时、
    // 异常隔离与取消。
    inline rt_basic::Callable method_reactor_start() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                // Optional global timeout argument.
                // 可选的全局超时参数。
                double argTimeout = 0;
                auto at = rb::number_of(rb::para_at(paras, 0));
                if (at) argTimeout = *at;

                auto maxC = rb::number_of(env["max_concurrency"]);
                std::size_t limit = maxC ? static_cast<std::size_t>(
                    std::max(0.0, *maxC)) : 0;
                auto defT = rb::number_of(env["timeout"]);
                long long defTimeout = defT ? static_cast<long long>(*defT) : 0;
                if (argTimeout > 0 && defTimeout == 0) defTimeout = (long long)argTimeout;

                auto tasksObj = env["closures"];
                auto* src = rb::attributes_of(tasksObj);
                std::vector<RuntimeObjectPtr> closures;
                if (src) {
                    std::size_t n = rb::container_size(*src);
                    for (std::size_t i = 0; i < n; ++i) {
                        auto it = src->find(rb::elem_key(i));
                        if (it != src->end() && it->second) closures.push_back(it->second);
                    }
                }

                std::size_t n = closures.size();
                std::vector<RuntimeObjectPtr> results(n, make_result("ok", rb::make_tuple({})));
                if (n == 0) return rb::list_of({rb::make_tuple(results)});

                // Counting-semaphore scheduler with backpressure. `active`
                // tracks in-flight tasks and is decremented by each worker on
                // completion, so the dispatcher can free a slot and launch the
                // next closure — no busy-wait, no deadlock.
                // 带背压的计数信号量调度器。`active` 记录在飞任务数，
                // 每个工作线程完成时自减，调度器即可腾出槽位启动下一个
                // 闭包——无忙等、无死锁。
                std::mutex mux;
                std::condition_variable cv;
                std::size_t active = 0;
                std::size_t next = 0;
                std::vector<std::shared_future<rt_basic::InstanceListPtr>> running(n);
                std::vector<std::shared_ptr<std::atomic<bool>>> cancels(n);
                std::atomic<bool> globCancel{ rb::boolean_of(env["cancelled"]).value_or(false) };

                auto launch_one = [&](std::size_t idx) {
                    auto c = closures[idx];
                    auto cancel = std::make_shared<std::atomic<bool>>(false);
                    cancels[idx] = cancel;
                    running[idx] = launch_detached(
                        [c, cancel, defTimeout, &mux, &cv, &active]() {
                            rt_basic::InstanceListPtr res;
                            if (cancel->load()) {
                                res = rb::list_of({make_error("cancelled", "task cancelled before start")});
                            } else if (defTimeout > 0) {
                                // Run with an inner timeout guard. The guard is
                                // a detached launch: on timeout the abandoned
                                // thread finishes on its own schedule and its
                                // result is discarded — the slot is freed
                                // immediately (no join, no blocked worker).
                                // 带内部超时守护地运行。守护为分离派发：超时
                                // 后被弃置的线程自行结束、结果直接丢弃——
                                // 槽位立即释放（无 join、无阻塞工作线程）。
                                auto inner = launch_detached(
                                    [c]() { return run_closure_isolated(c); });
                                if (inner.wait_for(std::chrono::milliseconds(defTimeout)) ==
                                    std::future_status::timeout) {
                                    res = rb::list_of({make_error("timeout", "task timed out")});
                                } else {
                                    res = inner.get();
                                }
                            } else {
                                res = run_closure_isolated(c);
                            }
                            {
                                std::lock_guard<std::mutex> lk(mux);
                                --active;
                            }
                            cv.notify_one();
                            return res;
                        });
                };

                // Dispatcher: keep at most `limit` (or all) tasks in flight.
                // 调度器：最多保持 `limit` 个（或全量）任务在飞。
                //
                // D6 (deadlock fix): the dispatcher's cv.wait blocks the main
                // thread while worker threads need the GIL to evaluate their
                // closures. With `limit < task count` the main thread used to
                // wait on `active` while STILL holding the GIL — workers could
                // never acquire it, `active` could never drop: a classic
                // self-deadlock (repro: 3 tasks + set_limit(2) hung forever).
                // Release the GIL for the whole dispatch + collect phase; only
                // Synth-OOP evaluation needs it, and this body merely launches
                // threads and builds plain result objects (stdRT.make only
                // reads the immutable prototype registry).
                // D6（死锁修复）：调度器的 cv.wait 会阻塞主线程，而工作线程
                // 恰恰需要 GIL 才能求值闭包。当 limit < 任务数时，主线程会在
                // **仍持有 GIL** 的情况下等待 active 下降——工作线程永远拿
                // 不到锁，active 永远不降，构成经典自死锁（复现：
                // 3 任务 + set_limit(2) 永久挂起）。故为整个调度 + 回收阶段
                // 释放 GIL；只有 Synth-OOP 求值需要 GIL，而本函数体仅负责
                // 派发线程与构造普通结果对象（stdRT.make 只读不可变的原型
                // 注册表）。
                gil_release_for_wait();
                while (next < n) {
                    if (globCancel.load()) break;
                    std::unique_lock<std::mutex> lk(mux);
                    cv.wait(lk, [&]() {
                        return (limit == 0 || active < limit) && next < n;
                    });
                    if (globCancel.load()) break;
                    std::size_t idx = next++;
                    ++active;
                    lk.unlock();
                    launch_one(idx);
                }
                // Collect in order. The GIL is already released (see the D6
                // note above); joining the worker futures here is exactly what
                // lets them run.
                // 按顺序回收。GIL 已在上方释放（见前述 D6 注释）；此处 join
                // 工作线程 future 正是使其得以运行的原因。
                for (std::size_t idx = 0; idx < n; ++idx) {
                    if (!running[idx].valid()) continue;
                    auto out = running[idx].get();
                    if (globCancel.load() && idx >= next) {
                        results[idx] = make_result("cancelled",
                            make_error("cancelled", "reactor cancelled"));
                        continue;
                    }
                    if (out && !out->empty()) {
                        // Same as await_task: check the etag directly; an
                        // Error has no `#value` capsule for unwrap() to find.
                        // 同 await_task：直接做 etag 判定；Error 没有
                        // unwrap() 能找到的 `#value` 胶囊。
                        auto val = (*out)[0];
                        if (val && rb::is_error(val)) {
                            results[idx] = make_result("error", val);
                            continue;
                        }
                    }
                    results[idx] = make_result("ok",
                        rb::make_tuple(out ? *out : std::vector<RuntimeObjectPtr>{}));
                }
                gil_reacquire_after_wait();
                // Remaining (not launched due to cancellation) become cancelled.
                // 因取消而未启动的任务记为 cancelled。
                for (std::size_t idx = next; idx < n; ++idx) {
                    results[idx] = make_result("cancelled",
                        make_error("cancelled", "reactor cancelled before launch"));
                }

                return rb::list_of({rb::make_tuple(results)});
            },
            rb::make_sign("start", {{"timeout", "std::Number"}}, {{"results", "std::Tuple"}})
        );
    }

    // reactor.with_timeout(task, ms) ~> (status, payload)
    // Run a single closure with a hard timeout.
    // 以硬超时运行单个闭包。
    inline rt_basic::Callable method_reactor_with_timeout() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto clos = rb::para_at(paras, 0);
                auto t = rb::number_of(rb::para_at(paras, 1));
                long long ms = t ? static_cast<long long>(*t) : 0;
                if (!clos) return rb::list_of({make_result("error",
                    make_error("badarg", "with_timeout requires a closure"))});
                // Detached launch, capturing the closure BY VALUE (a [&]
                // capture would dangle the moment this native body returns).
                // On the timeout path this method returns while the worker is
                // still running; the abandoned thread finishes on its own
                // schedule and its result is discarded. (The former
                // std::async version blocked on its shared state at return —
                // while holding the GIL the worker needed — and hung forever.)
                // 分离派发，按值捕获闭包（[&] 捕获在本函数体返回瞬间即
                // 悬垂）。超时路径会在 worker 仍在运行时直接返回；被弃置
                // 的线程自行结束、结果直接丢弃。（旧 std::async 版本会在
                // 返回销毁时于共享状态上阻塞——而 worker 需要的恰是本线程
                // 持有的 GIL——于是永久挂起。）
                auto inner = launch_detached(
                    [clos]() { return run_closure_isolated(clos); });
                if (ms > 0 && inner.wait_for(std::chrono::milliseconds(ms)) ==
                    std::future_status::timeout) {
                    return rb::list_of({make_result("timeout", make_error("timeout", "timed out"))});
                }
                // D6: release the GIL while joining the worker future.
                // D6：回收工作线程 future 期间释放 GIL。
                gil_release_for_wait();
                auto out = inner.get();
                gil_reacquire_after_wait();
                if (out && !out->empty()) {
                    // Same as await_task: check the etag directly; an Error
                    // has no `#value` capsule for unwrap() to find.
                    // 同 await_task：直接做 etag 判定；Error 没有 unwrap()
                    // 能找到的 `#value` 胶囊。
                    auto val = (*out)[0];
                    if (val && rb::is_error(val)) {
                        return rb::list_of({make_result("error", val)});
                    }
                }
                return rb::list_of({make_result("ok",
                    rb::make_tuple(out ? *out : std::vector<RuntimeObjectPtr>{}))});
            },
            rb::make_sign(
                "with_timeout",
                {{"task", "@"}, {"ms", "std::Number"}},
                {{"result", "std::Tuple"}})
        );
    }

    // reactor.spawn(task) ~> (Task)  — dynamic submission.
    // reactor.spawn(task) ~> (Task) —— 动态派发。
    inline rt_basic::Callable method_reactor_spawn() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto clos = rb::para_at(paras, 0);
                if (!clos) return rb::list_of({rb::native_error(
                    "reactor.spawn requires a closure")});
                RuntimeObjectPtr c = clos;
                auto task = spawn_lambda([c]() { return run_closure_isolated(c); });
                return rb::list_of({task});
            },
            rb::make_sign("spawn", {{"task", "std::Object"}}, {{"handle", "async::Task"}})
        );
    }

    // reactor.async_sleep(ms) ~> (Task)  — non-blocking timer.
    // reactor.async_sleep(ms) ~> (Task) —— 非阻塞定时器。
    inline rt_basic::Callable method_reactor_async_sleep() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto ms = rb::number_of(rb::para_at(paras, 0));
                long long msLL = ms ? static_cast<long long>(*ms) : 0;
                if (msLL < 0) msLL = 0;
                auto task = spawn_lambda([msLL]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(msLL));
                    return rb::empty_result();
                });
                return rb::list_of({task});
            },
            rb::make_sign("async_sleep", {{"ms", "std::Number"}}, {{"handle", "async::Task"}})
        );
    }

    // ========================================================
    // $Task — future-like handle / 类 future 句柄
    // ========================================================
    inline rt_basic::Callable method_task_await() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto id = task_id(env);
                if (!id) return rb::list_of({rb::native_error("task has no id")});
                auto t = rb::number_of(rb::para_at(paras, 0));
                long long timeout = t ? static_cast<long long>(*t) : 0;
                return rb::list_of({await_task(static_cast<long long>(*id), timeout, true)});
            },
            rb::make_sign("await", {{"timeout", "std::Number"}}, {{"result", "std::Tuple"}})
        );
    }
    inline rt_basic::Callable method_task_result() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto id = task_id(env);
                if (!id) return rb::list_of({rb::native_error("task has no id")});
                return rb::list_of({await_task(static_cast<long long>(*id), 0, false)});
            },
            rb::make_sign("result", {}, {{"result", "std::Tuple"}})
        );
    }
    inline rt_basic::Callable method_task_cancel() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto id = task_id(env);
                if (id) {
                    std::lock_guard<std::recursive_mutex> lk(g_task_mux);
                    auto it = g_tasks.find(static_cast<long long>(*id));
                    if (it != g_tasks.end()) it->second.cancelled->store(true);
                }
                env["cancelled"] = rb::make_boolean(true);
                return rb::empty_result();
            },
            rb::make_sign("cancel", {}, {})
        );
    }
    inline rt_basic::Callable method_task_is_done() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto id = task_id(env);
                bool done = true;
                if (id) {
                    std::lock_guard<std::recursive_mutex> lk(g_task_mux);
                    auto it = g_tasks.find(static_cast<long long>(*id));
                    if (it != g_tasks.end()) {
                        done = (it->second.fut.wait_for(std::chrono::seconds(0)) ==
                                std::future_status::ready);
                    }
                }
                return rb::list_of({rb::make_boolean(done)});
            },
            rb::make_sign("is_done", {}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_task_dispose() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto id = task_id(env);
                if (id) {
                    std::lock_guard<std::recursive_mutex> lk(g_task_mux);
                    auto it = g_tasks.find(static_cast<long long>(*id));
                    if (it != g_tasks.end()) {
                        // Mark cancelled so a still-running task exits early
                        // at its next check, then drop the registry entry.
                        // The detached worker owns its own promise, so
                        // dropping the shared_future never joins or blocks.
                        // 先置取消标志使仍在运行的任务在下一检查点尽早退出，
                        // 再丢弃注册表条目。分离的工作线程自持 promise，
                        // 丢弃 shared_future 绝不 join 或阻塞。
                        it->second.cancelled->store(true);
                        g_tasks.erase(it);
                    }
                }
                return rb::empty_result();
            },
            rb::make_sign("dispose", {}, {})
        );
    }

    // ========================================================
    // $Error — failure carrier / 失败载体
    // ========================================================
    inline rt_basic::Callable method_error_message() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto m = rb::string_of(env["#message"]);
                return rb::list_of({rb::make_string(m ? *m : "")});
            },
            rb::make_sign("message", {}, {{"text", "std::String"}})
        );
    }
    inline rt_basic::Callable method_error_kind() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto k = rb::string_of(env["#kind"]);
                return rb::list_of({rb::make_string(k ? *k : "")});
            },
            rb::make_sign("kind", {}, {{"text", "std::String"}})
        );
    }

    // ---- registration / 登记 ----
    inline void init_async_stdlib() {
        // Reactor / 反应堆
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            proto->set_attribute("closures",       rb::make_tuple({}));
            proto->set_attribute("max_concurrency", rb::make_number(0, true));
            proto->set_attribute("timeout",        rb::make_number(0, true));
            proto->set_attribute("cancelled",      rb::make_boolean(false));
            proto->set_method("set",         method_reactor_set());
            proto->set_method("set_limit",   method_reactor_set_limit());
            proto->set_method("set_timeout", method_reactor_set_timeout());
            proto->set_method("cancel",      method_reactor_cancel());
            proto->set_method("start",       method_reactor_start());
            proto->set_method("with_timeout", method_reactor_with_timeout());
            proto->set_method("spawn",       method_reactor_spawn());
            proto->set_method("submit",      method_reactor_spawn());
            proto->set_method("async_sleep", method_reactor_async_sleep());
            runtime::Prototypes p; p.regcls("Reactor", proto); ::stdRT.add_protos(p);
        }
        // Task / 任务句柄
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            proto->set_attribute("id",        rb::make_number(0, true));
            proto->set_attribute("cancelled", rb::make_boolean(false));
            proto->set_method("await",   method_task_await());
            proto->set_method("result",  method_task_result());
            proto->set_method("cancel",  method_task_cancel());
            proto->set_method("is_done", method_task_is_done());
            proto->set_method("dispose", method_task_dispose());
            // D5/D6: NO destructive on_release here. Task handles are
            // value-copied ids (`-(async::Task t) << r.spawn(...)` keeps only
            // the id capsule), so erasing the registry entry on instance
            // destruction would let a dropped TEMPORARY handle kill a live
            // task. Reclamation is done by await() / dispose() plus the
            // bounded-registry sweep in spawn_lambda (kMaxTrackedTasks).
            // D5/D6：此处**不做**破坏性 on_release。Task 句柄是被值复制的
            // id（`-(async::Task t) << r.spawn(...)` 只保留 id 胶囊），若在
            // 实例析构时清除注册表条目，被丢弃的**临时**句柄就会杀死存活
            // 任务。回收由 await() / dispose() 与 spawn_lambda 中的有界
            // 注册表清扫（kMaxTrackedTasks）承担。
            runtime::Prototypes p; p.regcls("Task", proto); ::stdRT.add_protos(p);
        }
        // Error / 错误
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            // No public `kind`/`message` ATTRIBUTES: they would shadow the
            // same-named methods (member lookup is attributes-first). Values
            // travel under the hidden `#kind`/`#message` capsules.
            // 不注册公开的 `kind`/`message` **属性**：会遮蔽同名方法
            // （成员查找属性优先）。值经隐藏的 `#kind`/`#message` 胶囊传递。
            proto->set_method("message", method_error_message());
            proto->set_method("kind",    method_error_kind());
            runtime::Prototypes p; p.regcls("Error", proto); ::stdRT.add_protos(p);
        }
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("async", &init_async_stdlib), true);

} // namespace rt_lib_async
