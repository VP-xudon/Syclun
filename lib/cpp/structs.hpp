// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/structs.hpp
//
// Standard library: structs (C++-backed backend).
// 标准库：structs（C++ 底层实现）。
//
// A small but coherent collection of data structures, replacing the old
// pure-Synth-OOP `queue` library:
//   Queue  — FIFO sequence / 先进先出队列
//   Stack  — LIFO sequence / 后进先出栈
//   Tree   — binary search tree (Numbers) / 二叉搜索树
//   Map    — associative key→value store / 关联映射
//   Graph  — undirected weighted graph with BFS + shortest path / 图
//
// State for each instance lives in a process-wide registry keyed by a stable
// instance id (assigned lazily on first use), so the C++ methods never fight
// the interpreter's attribute model. Self-registered under "structs".
// 每个实例的状态存于进程级注册表（按惰性分配的实例 id 索引），故 C++ 方法
// 不与解释器的属性模型冲突。以 "structs" 自注册。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <algorithm>
#include <sstream>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_structs {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // --------------------------------------------------------
    // Shared state registries / 共享状态注册表
    // --------------------------------------------------------
    struct VecState {
        std::vector<RuntimeObjectPtr> items;
    };
    struct TreeNode {
        double key = 0.0;
        RuntimeObjectPtr value;
        std::shared_ptr<TreeNode> left, right;
    };
    struct TreeState {
        std::shared_ptr<TreeNode> root;
        std::size_t count = 0;
    };
    struct MapState {
        std::map<std::string, RuntimeObjectPtr> entries;
    };
    struct GraphState {
        std::unordered_set<std::string> nodes;
        std::unordered_map<std::string, std::vector<std::pair<std::string, double>>> adj;
        std::size_t edges = 0;
    };

    static std::recursive_mutex g_st_mux;
    static long long  g_st_id = 0;
    static std::unordered_map<long long, VecState>   g_queues;
    static std::unordered_map<long long, VecState>   g_stacks;
    static std::unordered_map<long long, TreeState>  g_trees;
    static std::unordered_map<long long, MapState>   g_maps;
    static std::unordered_map<long long, GraphState> g_graphs;

    // Read a string key for Map / Graph nodes (Number or String).
    // 读取 Map/Graph 节点的字符串键（Number 或 String）。
    inline std::string key_text(const RuntimeObjectPtr& o) {
        if (!o) return "";
        auto s = rb::string_of(o);
        if (s) return *s;
        auto n = rb::number_of(o);
        if (n) return std::to_string(static_cast<long long>(*n));
        auto cap = rb::unwrap(o);
        if (cap) return cap->selfname;
        return "?";
    }

    // Lazily obtain a stable id for this instance (stored back on `env`).
    // 惰性取得本实例的稳定 id（回写至 env）。
    inline long long instance_id(rt_basic::InstanceMap& env) {
        long long id = 0;
        auto it = env.find("id");
        if (it != env.end()) {
            auto v = rb::number_of(it->second);
            if (v) id = static_cast<long long>(*v);
        }
        if (id <= 0) {
            std::lock_guard<std::recursive_mutex> lk(g_st_mux);
            id = ++g_st_id;
            env["id"] = rb::make_number(static_cast<double>(id));
        }
        return id;
    }

    // Build an Array runtime object from a vector (0-based, ELEM_PREFIX keys).
    // 由 vector 构造 Array 运行时对象（0 基、ELEM_PREFIX 键）。
    inline RuntimeObjectPtr build_array(const std::vector<RuntimeObjectPtr>& items) {
        auto arr = ::stdRT.make("Array");
        auto* cls = dynamic_cast<RuntimeClass*>(arr.get());
        auto& am = cls->get_attributes();
        for (std::size_t i = 0; i < items.size(); ++i) {
            am[rb::elem_key(i)] = items[i];
        }
        rb::set_container_size(am, items.size());
        return arr;
    }

    // ========================================================
    // Queue (FIFO) / 队列
    // ========================================================
    inline rt_basic::Callable method_queue_push() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                auto& st = g_queues[instance_id(env)];
                st.items.push_back(rb::para_at(paras, 0));
                return rb::empty_result();
            },
            rb::make_sign("push", {{"item", "std::Object"}}, {})
        );
    }
    inline rt_basic::Callable method_queue_pop() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                auto& st = g_queues[instance_id(env)];
                if (st.items.empty()) {
                    return rb::list_of({rb::native_error("queue.pop: empty queue")});
                }
                auto front = st.items.front();
                st.items.erase(st.items.begin());
                return rb::list_of({front});
            },
            rb::make_sign("pop", {}, {{"item", "std::Object"}})
        );
    }
    inline rt_basic::Callable method_queue_peek() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                auto& st = g_queues[instance_id(env)];
                if (st.items.empty()) {
                    return rb::list_of({rb::native_error("queue.peek: empty queue")});
                }
                return rb::list_of({st.items.front()});
            },
            rb::make_sign("peek", {}, {{"item", "std::Object"}})
        );
    }
    inline rt_basic::Callable method_queue_size() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_number(
                    static_cast<double>(g_queues[instance_id(env)].items.size()))});
            },
            rb::make_sign("size", {}, {{"n", "std::Number"}})
        );
    }
    inline rt_basic::Callable method_queue_empty() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_boolean(
                    g_queues[instance_id(env)].items.empty())});
            },
            rb::make_sign("empty", {}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_queue_clear() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                g_queues[instance_id(env)].items.clear();
                return rb::empty_result();
            },
            rb::make_sign("clear", {}, {})
        );
    }
    inline rt_basic::Callable method_queue_to_array() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({build_array(g_queues[instance_id(env)].items)});
            },
            rb::make_sign("to_array", {}, {{"arr", "std::Array"}})
        );
    }

    // ========================================================
    // Stack (LIFO) / 栈
    // ========================================================
    inline rt_basic::Callable method_stack_push() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                g_stacks[instance_id(env)].items.push_back(rb::para_at(paras, 0));
                return rb::empty_result();
            },
            rb::make_sign("push", {{"item", "std::Object"}}, {})
        );
    }
    inline rt_basic::Callable method_stack_pop() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                auto& st = g_stacks[instance_id(env)];
                if (st.items.empty()) {
                    return rb::list_of({rb::native_error("stack.pop: empty stack")});
                }
                auto back = st.items.back();
                st.items.pop_back();
                return rb::list_of({back});
            },
            rb::make_sign("pop", {}, {{"item", "std::Object"}})
        );
    }
    inline rt_basic::Callable method_stack_top() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                auto& st = g_stacks[instance_id(env)];
                if (st.items.empty()) {
                    return rb::list_of({rb::native_error("stack.top: empty stack")});
                }
                return rb::list_of({st.items.back()});
            },
            rb::make_sign("top", {}, {{"item", "std::Object"}})
        );
    }
    inline rt_basic::Callable method_stack_size() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_number(
                    static_cast<double>(g_stacks[instance_id(env)].items.size()))});
            },
            rb::make_sign("size", {}, {{"n", "std::Number"}})
        );
    }
    inline rt_basic::Callable method_stack_empty() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_boolean(
                    g_stacks[instance_id(env)].items.empty())});
            },
            rb::make_sign("empty", {}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_stack_clear() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                g_stacks[instance_id(env)].items.clear();
                return rb::empty_result();
            },
            rb::make_sign("clear", {}, {})
        );
    }
    inline rt_basic::Callable method_stack_to_array() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({build_array(g_stacks[instance_id(env)].items)});
            },
            rb::make_sign("to_array", {}, {{"arr", "std::Array"}})
        );
    }

    // ========================================================
    // Tree (binary search tree over Numbers) / 二叉搜索树
    // ========================================================
    inline void bst_insert(std::shared_ptr<TreeNode>& root, double k, RuntimeObjectPtr v) {
        if (!root) {
            root = std::make_shared<TreeNode>();
            root->key = k; root->value = v;
            return;
        }
        if (k < root->key) bst_insert(root->left, k, v);
        else if (k > root->key) bst_insert(root->right, k, v);
        else root->value = v;   // update existing
    }
    inline bool bst_contains(const std::shared_ptr<TreeNode>& root, double k) {
        if (!root) return false;
        if (k == root->key) return true;
        return k < root->key ? bst_contains(root->left, k)
                             : bst_contains(root->right, k);
    }
    inline double bst_min(const std::shared_ptr<TreeNode>& root) {
        auto n = root; while (n && n->left) n = n->left; return n ? n->key : 0.0;
    }
    inline double bst_max(const std::shared_ptr<TreeNode>& root) {
        auto n = root; while (n && n->right) n = n->right; return n ? n->key : 0.0;
    }
    inline std::shared_ptr<TreeNode> bst_remove(
        std::shared_ptr<TreeNode> node, double k, bool& removed
    ) {
        if (!node) { removed = false; return node; }
        if (k < node->key) node->left = bst_remove(node->left, k, removed);
        else if (k > node->key) node->right = bst_remove(node->right, k, removed);
        else {
            removed = true;
            if (!node->left) return node->right;
            if (!node->right) return node->left;
            auto succ = node->right;
            while (succ->left) succ = succ->left;
            node->key = succ->key; node->value = succ->value;
            bool r2; node->right = bst_remove(node->right, succ->key, r2);
        }
        return node;
    }
    inline void bst_collect(
        const std::shared_ptr<TreeNode>& node,
        std::vector<RuntimeObjectPtr>& out, const std::string& order
    ) {
        if (!node) return;
        if (order == "pre")  out.push_back(node->value);
        bst_collect(node->left, out, order);
        if (order == "in")   out.push_back(node->value);
        bst_collect(node->right, out, order);
        if (order == "post") out.push_back(node->value);
    }

    inline rt_basic::Callable method_tree_insert() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto v = rb::para_at(paras, 0);
                auto k = rb::number_of(v);
                if (!k) {
                    return rb::list_of({rb::native_error(
                        "tree.insert requires a numeric value")});
                }
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                auto& st = g_trees[instance_id(env)];
                bst_insert(st.root, *k, v);
                st.count += 1;   // approximate count (insertions)
                return rb::empty_result();
            },
            rb::make_sign("insert", {{"value", "std::Number"}}, {})
        );
    }
    inline rt_basic::Callable method_tree_contains() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto k = rb::number_of(rb::para_at(paras, 0));
                if (!k) {
                    return rb::list_of({rb::native_error(
                        "tree.contains requires a numeric value")});
                }
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_boolean(
                    bst_contains(g_trees[instance_id(env)].root, *k))});
            },
            rb::make_sign("contains", {{"value", "std::Number"}}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_tree_min() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_number(
                    bst_min(g_trees[instance_id(env)].root))});
            },
            rb::make_sign("min", {}, {{"value", "std::Number"}})
        );
    }
    inline rt_basic::Callable method_tree_max() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_number(
                    bst_max(g_trees[instance_id(env)].root))});
            },
            rb::make_sign("max", {}, {{"value", "std::Number"}})
        );
    }
    inline rt_basic::Callable method_tree_remove() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto k = rb::number_of(rb::para_at(paras, 0));
                if (!k) {
                    return rb::list_of({rb::native_error(
                        "tree.remove requires a numeric value")});
                }
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                bool removed = false;
                auto& st = g_trees[instance_id(env)];
                st.root = bst_remove(st.root, *k, removed);
                if (removed && st.count > 0) st.count -= 1;
                return rb::list_of({rb::make_boolean(removed)});
            },
            rb::make_sign("remove", {{"value", "std::Number"}}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_tree_size() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_number(
                    static_cast<double>(g_trees[instance_id(env)].count))});
            },
            rb::make_sign("size", {}, {{"n", "std::Number"}})
        );
    }
    inline rt_basic::Callable method_tree_empty() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_boolean(
                    g_trees[instance_id(env)].count == 0)});
            },
            rb::make_sign("empty", {}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_tree_inorder() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::vector<RuntimeObjectPtr> out;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                    bst_collect(g_trees[instance_id(env)].root, out, "in");
                }
                return rb::list_of({build_array(out)});
            },
            rb::make_sign("inorder", {}, {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_tree_preorder() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::vector<RuntimeObjectPtr> out;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                    bst_collect(g_trees[instance_id(env)].root, out, "pre");
                }
                return rb::list_of({build_array(out)});
            },
            rb::make_sign("preorder", {}, {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_tree_postorder() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::vector<RuntimeObjectPtr> out;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                    bst_collect(g_trees[instance_id(env)].root, out, "post");
                }
                return rb::list_of({build_array(out)});
            },
            rb::make_sign("postorder", {}, {{"arr", "std::Array"}})
        );
    }

    // ========================================================
    // Map (associative key→value) / 关联映射
    // ========================================================
    inline rt_basic::Callable method_map_put() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto key = rb::para_at(paras, 0);
                auto val = rb::para_at(paras, 1);
                if (!key) {
                    return rb::list_of({rb::native_error("map.put requires a key")});
                }
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                g_maps[instance_id(env)].entries[key_text(key)] = val;
                return rb::empty_result();
            },
            rb::make_sign(
                "put", {{"key", "std::Object"}, {"value", "std::Object"}}, {})
        );
    }
    inline rt_basic::Callable method_map_get() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto key = rb::para_at(paras, 0);
                if (!key) {
                    return rb::list_of({rb::native_error("map.get requires a key")});
                }
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                auto& e = g_maps[instance_id(env)].entries;
                auto it = e.find(key_text(key));
                if (it == e.end()) {
                    return rb::list_of({rb::native_error("map.get: key not found")});
                }
                return rb::list_of({it->second});
            },
            rb::make_sign("get", {{"key", "std::Object"}}, {{"value", "std::Object"}})
        );
    }
    inline rt_basic::Callable method_map_has() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto key = rb::para_at(paras, 0);
                if (!key) {
                    return rb::list_of({rb::native_error("map.has requires a key")});
                }
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_boolean(
                    g_maps[instance_id(env)].entries.count(key_text(key)) > 0)});
            },
            rb::make_sign("has", {{"key", "std::Object"}}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_map_remove() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto key = rb::para_at(paras, 0);
                if (!key) {
                    return rb::list_of({rb::native_error("map.remove requires a key")});
                }
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                auto& e = g_maps[instance_id(env)].entries;
                bool ok = e.erase(key_text(key)) > 0;
                return rb::list_of({rb::make_boolean(ok)});
            },
            rb::make_sign("remove", {{"key", "std::Object"}}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_map_keys() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::vector<RuntimeObjectPtr> out;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                    for (auto& kv : g_maps[instance_id(env)].entries) {
                        out.push_back(rb::make_string(kv.first));
                    }
                }
                return rb::list_of({build_array(out)});
            },
            rb::make_sign("keys", {}, {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_map_values() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::vector<RuntimeObjectPtr> out;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                    for (auto& kv : g_maps[instance_id(env)].entries) {
                        out.push_back(kv.second);
                    }
                }
                return rb::list_of({build_array(out)});
            },
            rb::make_sign("values", {}, {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_map_size() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_number(
                    static_cast<double>(g_maps[instance_id(env)].entries.size()))});
            },
            rb::make_sign("size", {}, {{"n", "std::Number"}})
        );
    }
    inline rt_basic::Callable method_map_empty() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_boolean(
                    g_maps[instance_id(env)].entries.empty())});
            },
            rb::make_sign("empty", {}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_map_clear() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                g_maps[instance_id(env)].entries.clear();
                return rb::empty_result();
            },
            rb::make_sign("clear", {}, {})
        );
    }

    // ========================================================
    // Graph (undirected weighted) / 无向带权图
    // ========================================================
    inline rt_basic::Callable method_graph_add_node() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto id = rb::para_at(paras, 0);
                if (!id) {
                    return rb::list_of({rb::native_error("graph.add_node requires an id")});
                }
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                g_graphs[instance_id(env)].nodes.insert(key_text(id));
                return rb::empty_result();
            },
            rb::make_sign("add_node", {{"id", "std::Object"}}, {})
        );
    }
    inline rt_basic::Callable method_graph_add_edge() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto a = rb::para_at(paras, 0), b = rb::para_at(paras, 1);
                if (!a || !b) {
                    return rb::list_of({rb::native_error(
                        "graph.add_edge requires two endpoints")});
                }
                double w = 1.0;
                auto wv = rb::number_of(rb::para_at(paras, 2));
                if (wv) w = *wv;
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                auto& g = g_graphs[instance_id(env)];
                std::string sa = key_text(a), sb = key_text(b);
                g.nodes.insert(sa); g.nodes.insert(sb);
                g.adj[sa].push_back({sb, w});
                g.adj[sb].push_back({sa, w});
                g.edges += 1;
                return rb::empty_result();
            },
            rb::make_sign(
                "add_edge",
                {{"a", "std::Object"}, {"b", "std::Object"}, {"weight", "std::Number"}}, {})
        );
    }
    inline rt_basic::Callable method_graph_has_node() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto id = rb::para_at(paras, 0);
                if (!id) {
                    return rb::list_of({rb::native_error("graph.has_node requires an id")});
                }
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_boolean(
                    g_graphs[instance_id(env)].nodes.count(key_text(id)) > 0)});
            },
            rb::make_sign("has_node", {{"id", "std::Object"}}, {{"ok", "std::Boolean"}})
        );
    }
    inline rt_basic::Callable method_graph_neighbors() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto id = rb::para_at(paras, 0);
                if (!id) {
                    return rb::list_of({rb::native_error("graph.neighbors requires an id")});
                }
                std::vector<RuntimeObjectPtr> out;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                    auto& g = g_graphs[instance_id(env)];
                    auto it = g.adj.find(key_text(id));
                    if (it != g.adj.end()) {
                        for (auto& e : it->second) out.push_back(rb::make_string(e.first));
                    }
                }
                return rb::list_of({build_array(out)});
            },
            rb::make_sign("neighbors", {{"id", "std::Object"}}, {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_graph_node_count() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_number(
                    static_cast<double>(g_graphs[instance_id(env)].nodes.size()))});
            },
            rb::make_sign("node_count", {}, {{"n", "std::Number"}})
        );
    }
    inline rt_basic::Callable method_graph_edge_count() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_number(
                    static_cast<double>(g_graphs[instance_id(env)].edges))});
            },
            rb::make_sign("edge_count", {}, {{"n", "std::Number"}})
        );
    }
    // BFS order of node ids (or shortest path when goal is supplied).
    // BFS 节点访问序（给定 goal 时即为最短路径）。
    inline std::vector<std::string> graph_bfs(
        GraphState& g, const std::string& start, const std::string& goal
    ) {
        std::vector<std::string> order;
        if (g.nodes.find(start) == g.nodes.end()) return order;
        std::unordered_map<std::string, std::string> parent;
        std::unordered_set<std::string> seen;
        std::queue<std::string> q;
        q.push(start); seen.insert(start);
        while (!q.empty()) {
            auto cur = q.front(); q.pop();
            order.push_back(cur);
            if (!goal.empty() && cur == goal) break;
            auto it = g.adj.find(cur);
            if (it != g.adj.end()) {
                for (auto& e : it->second) {
                    if (seen.insert(e.first).second) {
                        parent[e.first] = cur;
                        q.push(e.first);
                    }
                }
            }
        }
        if (!goal.empty()) {
            if (parent.find(goal) == parent.end() &&
                goal != start) return {};   // unreachable
            std::vector<std::string> path;
            for (std::string c = goal; ; c = parent[c]) {
                path.push_back(c);
                if (c == start) break;
            }
            std::reverse(path.begin(), path.end());
            return path;
        }
        return order;
    }
    inline rt_basic::Callable method_graph_bfs() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto id = rb::para_at(paras, 0);
                if (!id) {
                    return rb::list_of({rb::native_error("graph.bfs requires a start id")});
                }
                std::vector<RuntimeObjectPtr> out;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                    for (auto& s : graph_bfs(
                            g_graphs[instance_id(env)], key_text(id), "")) {
                        out.push_back(rb::make_string(s));
                    }
                }
                return rb::list_of({build_array(out)});
            },
            rb::make_sign("bfs", {{"start", "std::Object"}}, {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_graph_shortest_path() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto a = rb::para_at(paras, 0), b = rb::para_at(paras, 1);
                if (!a || !b) {
                    return rb::list_of({rb::native_error(
                        "graph.shortest_path requires start and goal")});
                }
                std::vector<RuntimeObjectPtr> out;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                    for (auto& s : graph_bfs(
                            g_graphs[instance_id(env)],
                            key_text(a), key_text(b))) {
                        out.push_back(rb::make_string(s));
                    }
                }
                return rb::list_of({build_array(out)});
            },
            rb::make_sign(
                "shortest_path",
                {{"start", "std::Object"}, {"goal", "std::Object"}},
                {{"arr", "std::Array"}})
        );
    }

    // ---- registration / 登记 ----
    inline void init_structs_stdlib() {
        // Queue / 队列
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            proto->set_method("push",     method_queue_push());
            proto->set_method("pop",      method_queue_pop());
            proto->set_method("peek",     method_queue_peek());
            proto->set_method("size",     method_queue_size());
            proto->set_method("empty",    method_queue_empty());
            proto->set_method("clear",    method_queue_clear());
            proto->set_method("to_array", method_queue_to_array());
            runtime::Prototypes p; p.regcls("Queue", proto); ::stdRT.add_protos(p);
        }
        // Stack / 栈
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            proto->set_method("push",     method_stack_push());
            proto->set_method("pop",      method_stack_pop());
            proto->set_method("top",      method_stack_top());
            proto->set_method("size",     method_stack_size());
            proto->set_method("empty",    method_stack_empty());
            proto->set_method("clear",    method_stack_clear());
            proto->set_method("to_array", method_stack_to_array());
            runtime::Prototypes p; p.regcls("Stack", proto); ::stdRT.add_protos(p);
        }
        // Tree / 二叉搜索树
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            proto->set_method("insert",    method_tree_insert());
            proto->set_method("contains",  method_tree_contains());
            proto->set_method("min",       method_tree_min());
            proto->set_method("max",       method_tree_max());
            proto->set_method("remove",    method_tree_remove());
            proto->set_method("size",      method_tree_size());
            proto->set_method("empty",     method_tree_empty());
            proto->set_method("inorder",   method_tree_inorder());
            proto->set_method("preorder",  method_tree_preorder());
            proto->set_method("postorder", method_tree_postorder());
            runtime::Prototypes p; p.regcls("Tree", proto); ::stdRT.add_protos(p);
        }
        // Map / 关联映射
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            proto->set_method("put",      method_map_put());
            proto->set_method("get",      method_map_get());
            proto->set_method("has",      method_map_has());
            proto->set_method("remove",   method_map_remove());
            proto->set_method("keys",     method_map_keys());
            proto->set_method("values",   method_map_values());
            proto->set_method("size",     method_map_size());
            proto->set_method("empty",    method_map_empty());
            proto->set_method("clear",    method_map_clear());
            runtime::Prototypes p; p.regcls("Map", proto); ::stdRT.add_protos(p);
        }
        // Graph / 图
        {
            auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
            proto->set_method("add_node",      method_graph_add_node());
            proto->set_method("add_edge",      method_graph_add_edge());
            proto->set_method("has_node",      method_graph_has_node());
            proto->set_method("neighbors",     method_graph_neighbors());
            proto->set_method("node_count",    method_graph_node_count());
            proto->set_method("edge_count",    method_graph_edge_count());
            proto->set_method("bfs",           method_graph_bfs());
            proto->set_method("shortest_path", method_graph_shortest_path());
            runtime::Prototypes p; p.regcls("Graph", proto); ::stdRT.add_protos(p);
        }
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("structs", &init_structs_stdlib), true);

} // namespace rt_lib_structs
