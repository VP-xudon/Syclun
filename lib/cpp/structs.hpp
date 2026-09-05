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
#include <limits>
#include <cstdio>

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
        int height = 1;   // AVL balance factor field (D9)
    };
    struct TreeState {
        std::shared_ptr<TreeNode> root;
        std::size_t count = 0;
    };
    struct MapState {
        std::map<std::string, RuntimeObjectPtr> entries;   // serialized key -> value
        std::map<std::string, RuntimeObjectPtr> keyobjs;   // serialized key -> original key object
    };
    struct GraphState {
        std::unordered_set<std::string> nodes;
        std::unordered_map<std::string, std::vector<std::pair<std::string, double>>> adj;
        std::size_t edges = 0;
    };

    static std::recursive_mutex g_st_mux;
    static long long  g_st_id = 0;
    static long long  g_obj_key_id = 0;   // stable identity counter for object keys (D1)
    static std::unordered_map<long long, VecState>   g_queues;
    static std::unordered_map<long long, VecState>   g_stacks;
    static std::unordered_map<long long, TreeState>  g_trees;
    static std::unordered_map<long long, MapState>   g_maps;
    static std::unordered_map<long long, GraphState> g_graphs;

    // Read a string key for Graph nodes (Number or String).
    // 读取 Graph 节点的字符串键（Number 或 String）。
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

    // --------------------------------------------------------
    // D1 fix: typed Map key serialization.
    // 键按「类型标签 + 值」规范化，杜绝 Number 1 与 String "1" 碰撞、
    // 浮点被截断、同类对象塌缩为同一键。
    //   N:<double>  Number (%.17g, round-trips exactly)
    //   S:<text>   String
    //   O:<id>     Object (stable per-instance identity)
    //   n:         null
    // --------------------------------------------------------
    inline std::string map_key(const RuntimeObjectPtr& o) {
        if (!o) return "n:";
        auto s = rb::string_of(o);
        if (s) return "S:" + *s;
        auto n = rb::number_of(o);
        if (n) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.17g", *n);
            return "N:" + std::string(buf);
        }
        auto* cls = dynamic_cast<RuntimeClass*>(o.get());
        if (cls) {
            auto& am = cls->get_attributes();
            long long oid = 0;
            auto it = am.find("__mapkey");
            if (it != am.end()) {
                auto v = rb::number_of(it->second);
                if (v) oid = static_cast<long long>(*v);
            }
            if (oid == 0) {
                oid = ++g_obj_key_id;
                am["__mapkey"] = rb::make_number(static_cast<double>(oid));
            }
            return "O:" + std::to_string(oid);
        }
        return "?:";
    }

    // Read an already-assigned instance id from an attribute map (no assignment).
    // 从属性表读取已分配的实例 id（不分配）。
    inline long long id_from(const rt_basic::InstanceMap& env) {
        auto it = env.find("id");
        if (it != env.end()) {
            auto v = rb::number_of(it->second);
            if (v) return static_cast<long long>(*v);
        }
        return 0;
    }

    // Per-type state reclamation (D5): erase the registry entry for an id.
    // 按类型的状态回收（D5）：删除某 id 的注册表项。
    inline void erase_queue(long long id) { std::lock_guard<std::recursive_mutex> lk(g_st_mux); g_queues.erase(id); }
    inline void erase_stack(long long id) { std::lock_guard<std::recursive_mutex> lk(g_st_mux); g_stacks.erase(id); }
    inline void erase_tree (long long id) { std::lock_guard<std::recursive_mutex> lk(g_st_mux); g_trees.erase(id); }
    inline void erase_map  (long long id) { std::lock_guard<std::recursive_mutex> lk(g_st_mux); g_maps.erase(id); }
    inline void erase_graph(long long id) { std::lock_guard<std::recursive_mutex> lk(g_st_mux); g_graphs.erase(id); }

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
    // D9 fix: the BST was naive and degenerated to a linked list under sorted
    // input (O(n) lookups, stack blowups). It is now an AVL tree: every
    // mutation rebalances, so height stays O(log n) and no path-through
    // recursion can exceed the guard depth on a large tree.
    // D9 修复：朴素 BST 在有序插入时退化成链表（O(n) 查找、爆栈）。
    // 改为 AVL 树，每次变更都再平衡，高度恒为 O(log n)。
    inline int bst_height(const std::shared_ptr<TreeNode>& n) {
        return n ? n->height : 0;
    }
    inline int bst_bfactor(const std::shared_ptr<TreeNode>& n) {
        return n ? bst_height(n->left) - bst_height(n->right) : 0;
    }
    inline void bst_fixup(std::shared_ptr<TreeNode>& n) {
        if (!n) return;
        int l = bst_height(n->left), r = bst_height(n->right);
        n->height = 1 + (l > r ? l : r);
    }
    inline std::shared_ptr<TreeNode> bst_rotate_right(std::shared_ptr<TreeNode> y) {
        auto x = y->left;
        auto t = x->right;
        x->right = y; y->left = t;
        bst_fixup(y); bst_fixup(x);
        return x;
    }
    inline std::shared_ptr<TreeNode> bst_rotate_left(std::shared_ptr<TreeNode> x) {
        auto y = x->right;
        auto t = y->left;
        y->left = x; x->right = t;
        bst_fixup(x); bst_fixup(y);
        return y;
    }
    inline std::shared_ptr<TreeNode> bst_balance(std::shared_ptr<TreeNode> n) {
        bst_fixup(n);
        int bf = bst_bfactor(n);
        if (bf > 1) {
            if (bst_bfactor(n->left) < 0) n->left = bst_rotate_left(n->left);
            return bst_rotate_right(n);
        }
        if (bf < -1) {
            if (bst_bfactor(n->right) > 0) n->right = bst_rotate_right(n->right);
            return bst_rotate_left(n);
        }
        return n;
    }
    inline std::shared_ptr<TreeNode> bst_insert(
        std::shared_ptr<TreeNode> n, double k, RuntimeObjectPtr v, bool& inserted
    ) {
        if (!n) {
            inserted = true;
            auto m = std::make_shared<TreeNode>();
            m->key = k; m->value = v; m->height = 1;
            return m;
        }
        if (k < n->key)       n->left  = bst_insert(n->left,  k, v, inserted);
        else if (k > n->key)  n->right = bst_insert(n->right, k, v, inserted);
        else { n->value = v; inserted = false; return n; }   // update existing
        return bst_balance(n);
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
    inline std::shared_ptr<TreeNode> bst_remove_min(
        std::shared_ptr<TreeNode> n, double& minKey
    ) {
        if (!n->left) { minKey = n->key; return n->right; }
        n->left = bst_remove_min(n->left, minKey);
        return bst_balance(n);
    }
    inline std::shared_ptr<TreeNode> bst_remove(
        std::shared_ptr<TreeNode> node, double k, bool& removed
    ) {
        if (!node) { removed = false; return node; }
        if (k < node->key)       node->left  = bst_remove(node->left,  k, removed);
        else if (k > node->key)  node->right = bst_remove(node->right, k, removed);
        else {
            removed = true;
            if (!node->left)  return node->right;
            if (!node->right) return node->left;
            double succKey = 0;
            node->right = bst_remove_min(node->right, succKey);
            node->key = succKey;
            return bst_balance(node);
        }
        return bst_balance(node);
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
    // D9: observability + ordered-range helpers (also let callers detect
    // degradation via `height`).
    // D9：可观测性 + 有序区间查询（height 亦可观测退化）。
    inline RuntimeObjectPtr bst_lower_bound(
        const std::shared_ptr<TreeNode>& root, double k
    ) {
        const TreeNode* cur = root.get();
        const TreeNode* best = nullptr;
        while (cur) {
            if (cur->key >= k) { best = cur; cur = cur->left.get(); }
            else cur = cur->right.get();
        }
        return best ? best->value : nullptr;
    }
    inline RuntimeObjectPtr bst_upper_bound(
        const std::shared_ptr<TreeNode>& root, double k
    ) {
        const TreeNode* cur = root.get();
        const TreeNode* best = nullptr;
        while (cur) {
            if (cur->key > k) { best = cur; cur = cur->left.get(); }
            else cur = cur->right.get();
        }
        return best ? best->value : nullptr;
    }
    inline void bst_range(const std::shared_ptr<TreeNode>& node, double lo, double hi,
                          std::vector<RuntimeObjectPtr>& out) {
        if (!node) return;
        if (lo < node->key) bst_range(node->left, lo, hi, out);
        if (lo <= node->key && node->key <= hi) out.push_back(node->value);
        if (node->key < hi) bst_range(node->right, lo, hi, out);
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
                bool inserted = false;
                st.root = bst_insert(st.root, *k, v, inserted);
                if (inserted) st.count += 1;   // only count genuine new keys
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

    inline rt_basic::Callable method_tree_height() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                return rb::list_of({rb::make_number(
                    static_cast<double>(bst_height(g_trees[instance_id(env)].root)))});
            },
            rb::make_sign("height", {}, {{"h", "std::Number"}})
        );
    }
    inline rt_basic::Callable method_tree_lower_bound() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto k = rb::number_of(rb::para_at(paras, 0));
                if (!k) {
                    return rb::list_of({rb::native_error(
                        "tree.lower_bound requires a numeric value")});
                }
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                auto v = bst_lower_bound(g_trees[instance_id(env)].root, *k);
                return rb::list_of({v ? v : rb::make_boolean(false)});
            },
            rb::make_sign("lower_bound", {{"value", "std::Number"}}, {{"val", "std::Object"}})
        );
    }
    inline rt_basic::Callable method_tree_upper_bound() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto k = rb::number_of(rb::para_at(paras, 0));
                if (!k) {
                    return rb::list_of({rb::native_error(
                        "tree.upper_bound requires a numeric value")});
                }
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                auto v = bst_upper_bound(g_trees[instance_id(env)].root, *k);
                return rb::list_of({v ? v : rb::make_boolean(false)});
            },
            rb::make_sign("upper_bound", {{"value", "std::Number"}}, {{"val", "std::Object"}})
        );
    }
    inline rt_basic::Callable method_tree_range() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto lo = rb::number_of(rb::para_at(paras, 0));
                auto hi = rb::number_of(rb::para_at(paras, 1));
                if (!lo || !hi) {
                    return rb::list_of({rb::native_error(
                        "tree.range requires numeric lo and hi")});
                }
                std::vector<RuntimeObjectPtr> out;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                    bst_range(g_trees[instance_id(env)].root, *lo, *hi, out);
                }
                return rb::list_of({build_array(out)});
            },
            rb::make_sign(
                "range",
                {{"lo", "std::Number"}, {"hi", "std::Number"}},
                {{"arr", "std::Array"}})
        );
    }
    inline rt_basic::Callable method_tree_clear() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                auto& st = g_trees[instance_id(env)];
                st.root = nullptr;
                st.count = 0;
                return rb::empty_result();
            },
            rb::make_sign("clear", {}, {})
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
                auto& st = g_maps[instance_id(env)];
                std::string k = map_key(key);
                st.entries[k] = val;
                st.keyobjs[k] = key;
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
                auto it = e.find(map_key(key));
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
                    g_maps[instance_id(env)].entries.count(map_key(key)) > 0)});
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
                std::string k = map_key(key);
                bool ok = e.erase(k) > 0;
                g_maps[instance_id(env)].keyobjs.erase(k);
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
                    for (auto& kv : g_maps[instance_id(env)].keyobjs) {
                        out.push_back(kv.second);
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
                auto& st = g_maps[instance_id(env)];
                st.entries.clear();
                st.keyobjs.clear();
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
    // D2 fix: Dijkstra shortest path — respects edge weights (BFS ignored them).
    // D2 修复：Dijkstra 最短路——尊重边权重（BFS 曾忽略权重）。
    inline std::vector<std::string> graph_dijkstra(
        GraphState& g, const std::string& start, const std::string& goal
    ) {
        if (g.nodes.find(start) == g.nodes.end()) return {};
        if (start == goal) return {start};
        using P = std::pair<double, std::string>;
        std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
        std::unordered_map<std::string, double> dist;
        std::unordered_map<std::string, std::string> parent;
        for (auto& n : g.nodes) dist[n] = std::numeric_limits<double>::infinity();
        dist[start] = 0.0;
        pq.push({0.0, start});
        while (!pq.empty()) {
            auto [d, u] = pq.top(); pq.pop();
            if (d > dist[u]) continue;
            if (u == goal) break;
            auto it = g.adj.find(u);
            if (it == g.adj.end()) continue;
            for (auto& e : it->second) {
                double nd = d + e.second;
                if (nd < dist[e.first]) {
                    dist[e.first] = nd;
                    parent[e.first] = u;
                    pq.push({nd, e.first});
                }
            }
        }
        if (parent.find(goal) == parent.end() && goal != start) return {};
        std::vector<std::string> path;
        for (std::string c = goal; ; c = parent[c]) {
            path.push_back(c);
            if (c == start) break;
        }
        std::reverse(path.begin(), path.end());
        return path;
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
                    for (auto& s : graph_dijkstra(
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
    inline rt_basic::Callable method_graph_shortest_distance() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto a = rb::para_at(paras, 0), b = rb::para_at(paras, 1);
                if (!a || !b) {
                    return rb::list_of({rb::native_error(
                        "graph.shortest_distance requires start and goal")});
                }
                double d = 0.0;
                bool ok = false;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_st_mux);
                    auto& g = g_graphs[instance_id(env)];
                    auto path = graph_dijkstra(g, key_text(a), key_text(b));
                    ok = !path.empty();
                    if (ok) {
                        d = 0.0;
                        for (std::size_t i = 1; i < path.size(); ++i) {
                            auto it = g.adj.find(path[i - 1]);
                            if (it != g.adj.end()) {
                                for (auto& e : it->second) {
                                    if (e.first == path[i]) { d += e.second; break; }
                                }
                            }
                        }
                    }
                }
                if (!ok) {
                    return rb::list_of({rb::native_error(
                        "graph.shortest_distance: goal unreachable")});
                }
                return rb::list_of({rb::make_number(d)});
            },
            rb::make_sign(
                "shortest_distance",
                {{"start", "std::Object"}, {"goal", "std::Object"}},
                {{"d", "std::Number"}})
        );
    }

    // ---- dispose(): explicit state reclamation (industrial-audit D5) ----
    // dispose()：显式回收本实例在进程级注册表中的状态（工业化审计 D5）。
    // The destructor hook (clsprototype on_release) calls the same eraser, so
    // state is also freed automatically when the object is garbage-collected.
    // 析构钩子（原型 on_release）会调用同一删除器，故对象被回收时状态也会
    // 自动释放。
    inline rt_basic::Callable method_queue_dispose() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                erase_queue(id_from(env));
                return rb::empty_result();
            },
            rb::make_sign("dispose", {}, {})
        );
    }
    inline rt_basic::Callable method_stack_dispose() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                erase_stack(id_from(env));
                return rb::empty_result();
            },
            rb::make_sign("dispose", {}, {})
        );
    }
    inline rt_basic::Callable method_tree_dispose() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                erase_tree(id_from(env));
                return rb::empty_result();
            },
            rb::make_sign("dispose", {}, {})
        );
    }
    inline rt_basic::Callable method_map_dispose() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                erase_map(id_from(env));
                return rb::empty_result();
            },
            rb::make_sign("dispose", {}, {})
        );
    }
    inline rt_basic::Callable method_graph_dispose() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                erase_graph(id_from(env));
                return rb::empty_result();
            },
            rb::make_sign("dispose", {}, {})
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
            proto->set_method("dispose",  method_queue_dispose());
            proto->on_release = [](rt_basic::InstanceMap& env) { erase_queue(id_from(env)); };
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
            proto->set_method("dispose",  method_stack_dispose());
            proto->on_release = [](rt_basic::InstanceMap& env) { erase_stack(id_from(env)); };
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
            proto->set_method("height",     method_tree_height());
            proto->set_method("lower_bound", method_tree_lower_bound());
            proto->set_method("upper_bound", method_tree_upper_bound());
            proto->set_method("range",      method_tree_range());
            proto->set_method("clear",      method_tree_clear());
            proto->set_method("dispose",  method_tree_dispose());
            proto->on_release = [](rt_basic::InstanceMap& env) { erase_tree(id_from(env)); };
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
            proto->set_method("dispose",  method_map_dispose());
            proto->on_release = [](rt_basic::InstanceMap& env) { erase_map(id_from(env)); };
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
            proto->set_method("shortest_distance", method_graph_shortest_distance());
            proto->set_method("dispose",  method_graph_dispose());
            proto->on_release = [](rt_basic::InstanceMap& env) { erase_graph(id_from(env)); };
            runtime::Prototypes p; p.regcls("Graph", proto); ::stdRT.add_protos(p);
        }
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("structs", &init_structs_stdlib), true);

} // namespace rt_lib_structs
