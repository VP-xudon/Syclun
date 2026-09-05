// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/async.hpp
//
// Standard library: async (C++-backed backend).
// 标准库：async（C++ 底层实现）。
//
// A modern async runtime built on top of std::async / std::future. The
// `$Reactor` runs a batch of closures with:
//   1. Lifecycle control   — cancellation (best-effort) and timeouts.
//   2. Concurrency control — max_concurrency limits (backpressure).
//   3. Fault tolerance     — per-task error isolation + Error objects.
//   4. Dynamic spawning    — spawn()/submit() return a `Task` handle.
//   5. Timers / scheduling — async_sleep() returns a non-blocking Task.
// `$Task` is a future-like handle; `$Error` carries failure info. The C++
// twin of lib/async.synl. Self-registered under "async".
// 基于 std::async / std::future 的现代异步运行时。`$Reactor` 以如下能力批量
// 执行闭包：① 生命周期控制（取消与超时）② 并发度控制（max_concurrency 背压）
// ③ 容错（逐任务异常隔离 + Error 对象）④ 动态派发（spawn/submit 返回 Task）
// ⑤ 定时器（async_sleep 返回非阻塞 Task）。`$Task` 为类 future 句柄，
// `$Error` 承载失败信息。以 "async" 自注册。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <optional>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_async {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // --------------------------------------------------------
    // Global task registry (for dynamically spawned Tasks).
    // 全局任务注册表（动态派发的 Task 句柄）。
    // --------------------------------------------------------
    struct TaskHandle {
        std::future<rt_basic::InstanceListPtr> fut;
        std::shared_ptr<std::atomic<bool>> cancelled;
    };
    static std::recursive_mutex        g_task_mux;
    static std::unordered_map<long long, TaskHandle> g_tasks;
    static long long         g_task_id = 0;

    // Build an Error object (kind + message).
    // 构造 Error 对象（kind + message）。
    inline RuntimeObjectPtr make_error(const std::string& kind, const std::string& msg) {
        auto err = ::stdRT.make("Error");
        auto* cls = dynamic_cast<RuntimeClass*>(err.get());
        if (cls) {
            auto& am = cls->get_attributes();
            am["kind"]    = rb::make_string(kind);
            am["message"] = rb::make_string(msg);
        }
        return err;
    }

    // Run a closure object on a thread, isolating any C++ exception into an
    // Error capsule so the reactor never crashes. A `NativeError` raised by a
    // native method (the retired poison-water path) is converted into that
    // capsule; other C++ exceptions are wrapped the same way.
    // 在线程上运行一个闭包对象，把任何 C++ 异常隔离为 Error 胶囊，
    // 使反应堆绝不崩溃。原生方法抛出的 NativeError（毒水退役后的错误路径）
    // 同样转为该胶囊；其余 C++ 异常同等处理。
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
    // acquire it and make progress. Returns the previous single-depth hold so it
    // can be restored afterwards. These are only ever called from a thread that
    // currently holds the GIL exactly once.
    // D6：阻塞等待时必须释放 GIL，使其他（工作线程）任务能取锁推进。返回
    // 先前的单一持锁深度以便事后恢复；仅当调用线程恰持锁 1 次时调用。
    inline int gil_release_for_wait() {
        rt_builtin::gil_mutex().unlock();
        return 1;
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

    // A `Task` object wrapping a running (or finished) future.
    // 包裹运行中（或已完成）future 的 `Task` 对象。
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
        std::future<rt_basic::InstanceListPtr> fut = std::async(
            std::launch::async,
            [fn, cancelledCp]() {
                if (cancelledCp->load()) {
                    return rb::list_of({make_error("cancelled", "task cancelled before start")});
                }
                return fn();
            }
        );
        {
            std::lock_guard<std::recursive_mutex> lk(g_task_mux);
            id = ++g_task_id;
            g_tasks[id] = TaskHandle{std::move(fut), cancelled};
        }
        return make_task(id);
    }

    // Wait for a task's future; returns its InstanceListPtr or an Error tuple
    // when it times out / was cancelled. `erase` removes the handle afterwards.
    // 等待任务 future；超时或已取消时返回 Error 元组。erase 为真时取走句柄。
    inline RuntimeObjectPtr await_task(
        long long id, long long timeoutMs, bool erase
    ) {
        std::future<rt_basic::InstanceListPtr> fut;
        std::shared_ptr<std::atomic<bool>> cancelled;
        {
            std::lock_guard<std::recursive_mutex> lk(g_task_mux);
            auto it = g_tasks.find(id);
            if (it == g_tasks.end()) {
                return make_result("error", make_error("unknown", "unknown task id"));
            }
            fut = std::move(it->second.fut);
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
        // A poison result from the closure becomes an "error" payload.
        // 闭包返回的毒水转为 "error" 负载。
        if (out && !out->empty()) {
                auto cap = rb::unwrap((*out)[0]);
                if (cap) {
                    if (rb::is_error(cap)) {
                        return make_result("error", cap);
                    }
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
                std::vector<std::future<rt_basic::InstanceListPtr>> running(n);
                std::vector<std::shared_ptr<std::atomic<bool>>> cancels(n);
                std::atomic<bool> globCancel{ rb::boolean_of(env["cancelled"]).value_or(false) };

                auto launch_one = [&](std::size_t idx) {
                    auto c = closures[idx];
                    auto cancel = std::make_shared<std::atomic<bool>>(false);
                    cancels[idx] = cancel;
                    running[idx] = std::async(std::launch::async,
                        [c, cancel, defTimeout, &mux, &cv, &active]() {
                            rt_basic::InstanceListPtr res;
                            if (cancel->load()) {
                                res = rb::list_of({make_error("cancelled", "task cancelled before start")});
                            } else if (defTimeout > 0) {
                                // Run with an inner timeout guard.
                                // 带内部超时守护地运行。
                                auto inner = std::async(std::launch::async,
                                    [&]() { return run_closure_isolated(c); });
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
                // Collect in order.
                // 按顺序回收。D6: release the GIL while joining the worker
                // futures so they can run; reacquire once all are done.
                // D6：回收工作线程 future 期间释放 GIL 使其得以运行；全部完成后重取。
                gil_release_for_wait();
                for (std::size_t idx = 0; idx < n; ++idx) {
                    if (!running[idx].valid()) continue;
                    auto out = running[idx].get();
                    if (globCancel.load() && idx >= next) {
                        results[idx] = make_result("cancelled",
                            make_error("cancelled", "reactor cancelled"));
                        continue;
                    }
                    if (out && !out->empty()) {
                        auto cap = rb::unwrap((*out)[0]);
                        if (cap) {
                            if (rb::is_error(cap)) {
                                results[idx] = make_result("error", cap);
                                continue;
                            }
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
                auto inner = std::async(std::launch::async,
                    [&]() { return run_closure_isolated(clos); });
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
                    auto cap = rb::unwrap((*out)[0]);
                    if (cap) {
                        if (rb::is_error(cap)) {
                            return rb::list_of({make_result("error", cap)});
                        }
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
                        // Mark cancelled so a still-running async thread exits
                        // early, then drop the handle (its future destructor
                        // joins the thread, guaranteeing reclamation).
                        // 先置取消标志让仍在跑的异步线程尽早退出，再丢弃句柄
                        // （future 析构会 join 线程，确保资源回收；工业化审计 D5）。
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
                auto m = rb::string_of(env["message"]);
                return rb::list_of({rb::make_string(m ? *m : "")});
            },
            rb::make_sign("message", {}, {{"text", "std::String"}})
        );
    }
    inline rt_basic::Callable method_error_kind() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                auto k = rb::string_of(env["kind"]);
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
            // D5: reclaim the g_tasks registry entry when the Task object is
            // collected. The destructor of the dropped future joins the thread.
            // D5：Task 对象被回收时取回 g_tasks 注册表条目（future 析构 join 线程）。
            proto->on_release = [](rt_basic::InstanceMap& env) {
                auto id = task_id(env);
                if (id) {
                    std::lock_guard<std::recursive_mutex> lk(g_task_mux);
                    auto it = g_tasks.find(static_cast<long long>(*id));
                    if (it != g_tasks.end()) {
                        it->second.cancelled->store(true);
                        g_tasks.erase(it);
                    }
                }
            };
            runtime::Prototypes p; p.regcls("Task", proto); ::stdRT.add_protos(p);
        }
        // Error / 错误
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            proto->set_attribute("kind",    rb::make_string(""));
            proto->set_attribute("message", rb::make_string(""));
            proto->set_method("message", method_error_message());
            proto->set_method("kind",    method_error_kind());
            runtime::Prototypes p; p.regcls("Error", proto); ::stdRT.add_protos(p);
        }
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("async", &init_async_stdlib), true);

} // namespace rt_lib_async
