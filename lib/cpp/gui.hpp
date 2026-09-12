// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/gui.hpp
//
// Standard library: gui (C++-backed backend wrapper).
// 标准库：gui（C++ 底层实现封装）。
//
// A small, tkinter-like system-window toolkit: a top-level Window plus Label /
// Button / Entry / Checkbox / Slider / ListBox / TextArea components and a
// message box, with actions wired to Synth-OOP closures. It is backed by a
// native C bridge (gui_native.h) that has three real implementations:
//   - Win32  (Windows, tested locally)
//   - X11    (Linux, custom retained widgets)
//   - Cocoa  (macOS, native AppKit controls)
// so the library is genuinely cross-platform (it does NOT degrade to an error).
// 一个迷你、类 tkinter 的系统窗口工具包：顶层 Window 加 Label / Button /
// Entry / Checkbox / Slider / ListBox / TextArea 组件与消息框，动作接回
// Synth-OOP 闭包。它由原生 C 桥（gui_native.h）支撑，该桥有三套真实实现：
// Win32（Windows，本机已测）/ X11（Linux，自定义保留式控件）/ Cocoa（macOS，
// 原生 AppKit 控件），故本库真正跨平台（不再降级为报错）。
//
// Threading note: callbacks run on the main (UI) thread inside the message
// loop. The interpreter already holds the GIL there, so user closures are
// invoked with rb::call_behavior WITHOUT re-acquiring the GIL.
// 线程说明：回调在主（UI）线程的消息循环中运行，解释器在此已持 GIL，故用
// rb::call_behavior 调用用户闭包时不再取锁。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <deque>
#include <cstdint>

#include "../../src/builtin.hpp"
#include "gui_native.h"

namespace rt_lib_gui {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // ---- closure -> C callback bridge / 闭包 -> C 回调桥 ----
    struct GuiCb {
        RuntimeObjectPtr clos;
    };
    // Stable-address store; lives for the process (acceptable for an interpreter).
    // 地址稳定的存储；随进程存在（对解释器而言可接受）。
    static std::deque<GuiCb> g_cbstore;
    static void gui_invoke(void* user) {
        GuiCb* c = static_cast<GuiCb*>(user);
        if (!c || !c->clos) return;
        rt_basic::InstanceMap env;
        rb::call_behavior(c->clos, env, rb::empty_result());
    }
    static GuiCb* make_cb(const RuntimeObjectPtr& clos) {
        g_cbstore.emplace_back();
        g_cbstore.back().clos = clos;
        return &g_cbstore.back();
    }

    // Per-window control registry (index -> native control pointer).
    // 每窗控件登记表（索引 -> 原生控件指针）。
    static std::unordered_map<uintptr_t, std::vector<gui_ctrl>> g_ctrls;
    static std::vector<gui_ctrl>& ctrls_of(gui_win w) {
        return g_ctrls[(uintptr_t)w];
    }

    static gui_win win_of(rt_basic::InstanceMap& env) {
        auto it = env.find("#win");
        if (it == env.end()) return nullptr;
        auto s = rb::string_of(it->second);
        if (!s) return nullptr;
        return (gui_win)(uintptr_t)std::stoull(*s);
    }

    // --------------------------------------------------------
    // Window methods / Window 方法
    // --------------------------------------------------------

    inline rt_basic::Callable method_window_open() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto title = rb::string_of(rb::para_at(paras, 0));
                auto w = rb::number_of(rb::para_at(paras, 1));
                auto h = rb::number_of(rb::para_at(paras, 2));
                if (!title || !w || !h)
                    return rb::list_of({rb::native_error("gui.open requires (title, width, height)")});
                gui_win win = gui_window_create(title->c_str(), (int)*w, (int)*h);
                if (!win) return rb::list_of({rb::native_error("gui: failed to create window")});
                env["#win"] = rb::make_string(std::to_string((uintptr_t)win));
                g_ctrls[(uintptr_t)win];   // ensure an entry exists
                return rb::empty_result();
            },
            rb::make_sign("open",
                {{"title", "std::String"}, {"width", "std::Number"}, {"height", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_window_set_title() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto title = rb::string_of(rb::para_at(paras, 0));
                if (!win || !title) return rb::empty_result();
                gui_window_set_title(win, title->c_str());
                return rb::empty_result();
            },
            rb::make_sign("set_title", {{"title", "std::String"}}, {}));
    }

    inline rt_basic::Callable method_window_label() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto text = rb::string_of(rb::para_at(paras, 0));
                auto x = rb::number_of(rb::para_at(paras, 1));
                auto y = rb::number_of(rb::para_at(paras, 2));
                if (!win || !text || !x || !y) return rb::empty_result();
                gui_add_label(win, text->c_str(), (int)*x, (int)*y);
                return rb::empty_result();
            },
            rb::make_sign("label",
                {{"text", "std::String"}, {"x", "std::Number"}, {"y", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_window_button() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto text = rb::string_of(rb::para_at(paras, 0));
                auto x = rb::number_of(rb::para_at(paras, 1));
                auto y = rb::number_of(rb::para_at(paras, 2));
                auto w = rb::number_of(rb::para_at(paras, 3));
                auto h = rb::number_of(rb::para_at(paras, 4));
                auto action = rb::para_at(paras, 5);
                if (!win || !text || !x || !y || !w || !h || !action)
                    return rb::list_of({rb::native_error(
                        "gui.button requires (text, x, y, width, height, action)")});
                GuiCb* cb = make_cb(action);
                gui_add_button(win, text->c_str(), (int)*x, (int)*y, (int)*w, (int)*h,
                               &gui_invoke, cb);
                return rb::empty_result();
            },
            rb::make_sign("button",
                {{"text", "std::String"}, {"x", "std::Number"}, {"y", "std::Number"},
                 {"width", "std::Number"}, {"height", "std::Number"}, {"action", "@"}}, {}));
    }

    // entry(x, y, width, height) -> index
    inline rt_basic::Callable method_window_entry() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto x = rb::number_of(rb::para_at(paras, 0));
                auto y = rb::number_of(rb::para_at(paras, 1));
                auto w = rb::number_of(rb::para_at(paras, 2));
                auto h = rb::number_of(rb::para_at(paras, 3));
                if (!win || !x || !y || !w || !h)
                    return rb::list_of({rb::native_error("gui.entry requires (x, y, width, height)")});
                gui_ctrl c = gui_add_entry(win, "", (int)*x, (int)*y, (int)*w, (int)*h);
                auto& v = ctrls_of(win);
                v.push_back(c);
                return rb::list_of({rb::make_int(static_cast<int64_t>(v.size() - 1))});
            },
            rb::make_sign("entry",
                {{"x", "std::Number"}, {"y", "std::Number"},
                 {"width", "std::Number"}, {"height", "std::Number"}},
                {{"out", "std::Number"}}));
    }

    inline rt_basic::Callable method_window_entry_text() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto idx = rb::number_of(rb::para_at(paras, 0));
                if (!win || !idx) return rb::list_of({rb::make_string("")});
                auto& v = ctrls_of(win);
                long i = (long)*idx;
                if (i < 0 || i >= (long)v.size()) return rb::list_of({rb::make_string("")});
                return rb::list_of({rb::make_string(gui_entry_text(v[(size_t)i]))});
            },
            rb::make_sign("entry_text", {{"index", "std::Number"}}, {{"out", "std::String"}}));
    }

    // checkbox(text, x, y) -> index
    inline rt_basic::Callable method_window_checkbox() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto text = rb::string_of(rb::para_at(paras, 0));
                auto x = rb::number_of(rb::para_at(paras, 1));
                auto y = rb::number_of(rb::para_at(paras, 2));
                if (!win || !text || !x || !y)
                    return rb::list_of({rb::native_error("gui.checkbox requires (text, x, y)")});
                gui_ctrl c = gui_add_checkbox(win, text->c_str(), (int)*x, (int)*y, 0);
                auto& v = ctrls_of(win); v.push_back(c);
                return rb::list_of({rb::make_int(static_cast<int64_t>(v.size() - 1))});
            },
            rb::make_sign("checkbox",
                {{"text", "std::String"}, {"x", "std::Number"}, {"y", "std::Number"}},
                {{"out", "std::Number"}}));
    }

    inline rt_basic::Callable method_window_checkbox_checked() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto idx = rb::number_of(rb::para_at(paras, 0));
                if (!win || !idx) return rb::list_of({rb::make_boolean(false)});
                auto& v = ctrls_of(win); long i = (long)*idx;
                if (i < 0 || i >= (long)v.size()) return rb::list_of({rb::make_boolean(false)});
                return rb::list_of({rb::make_boolean(gui_checkbox_checked(v[(size_t)i]) != 0)});
            },
            rb::make_sign("checkbox_checked", {{"index", "std::Number"}},
                          {{"out", "std::Boolean"}}));
    }

    // slider(x, y, width, min, max, value) -> index
    inline rt_basic::Callable method_window_slider() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto x = rb::number_of(rb::para_at(paras, 0));
                auto y = rb::number_of(rb::para_at(paras, 1));
                auto w = rb::number_of(rb::para_at(paras, 2));
                auto mn = rb::number_of(rb::para_at(paras, 3));
                auto mx = rb::number_of(rb::para_at(paras, 4));
                auto val = rb::number_of(rb::para_at(paras, 5));
                if (!win || !x || !y || !w || !mn || !mx || !val)
                    return rb::list_of({rb::native_error(
                        "gui.slider requires (x, y, width, min, max, value)")});
                gui_ctrl c = gui_add_slider(win, (int)*x, (int)*y, (int)*w,
                                           (int)*mn, (int)*mx, (int)*val);
                auto& v = ctrls_of(win); v.push_back(c);
                return rb::list_of({rb::make_int(static_cast<int64_t>(v.size() - 1))});
            },
            rb::make_sign("slider",
                {{"x", "std::Number"}, {"y", "std::Number"}, {"width", "std::Number"},
                 {"min", "std::Number"}, {"max", "std::Number"}, {"value", "std::Number"}},
                {{"out", "std::Number"}}));
    }

    inline rt_basic::Callable method_window_slider_value() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto idx = rb::number_of(rb::para_at(paras, 0));
                if (!win || !idx) return rb::list_of({rb::make_int(0)});
                auto& v = ctrls_of(win); long i = (long)*idx;
                if (i < 0 || i >= (long)v.size()) return rb::list_of({rb::make_int(0)});
                return rb::list_of({rb::make_int(gui_slider_value(v[(size_t)i]))});
            },
            rb::make_sign("slider_value", {{"index", "std::Number"}}, {{"out", "std::Number"}}));
    }

    // listbox(items[Array], x, y, width, height) -> index
    inline rt_basic::Callable method_window_listbox() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto items = rb::para_at(paras, 0);
                auto x = rb::number_of(rb::para_at(paras, 1));
                auto y = rb::number_of(rb::para_at(paras, 2));
                auto w = rb::number_of(rb::para_at(paras, 3));
                auto h = rb::number_of(rb::para_at(paras, 4));
                if (!win || !items || !x || !y || !w || !h)
                    return rb::list_of({rb::native_error(
                        "gui.listbox requires (items, x, y, width, height)")});
                std::vector<std::string> strs;
                // items is an Array; iterate its indexed elements.
                auto* cls = dynamic_cast<RuntimeClass*>(items.get());
                if (cls) {
                    auto& am = cls->get_attributes();
                    long n = (long)rb::container_size(am);
                    for (long i = 0; i < n; ++i) {
                        auto it = am.find("#" + std::to_string(i));
                        if (it == am.end()) continue;
                        auto s = rb::string_of(it->second);
                        strs.push_back(s ? *s : "");
                    }
                }
                std::vector<const char*> ptrs;
                for (auto& s : strs) ptrs.push_back(s.c_str());
                gui_ctrl c = gui_add_listbox(win, ptrs.empty() ? nullptr : ptrs.data(),
                                            (int)ptrs.size(), (int)*x, (int)*y, (int)*w, (int)*h);
                auto& v = ctrls_of(win); v.push_back(c);
                return rb::list_of({rb::make_int(static_cast<int64_t>(v.size() - 1))});
            },
            rb::make_sign("listbox",
                {{"items", "std::Array"}, {"x", "std::Number"}, {"y", "std::Number"},
                 {"width", "std::Number"}, {"height", "std::Number"}},
                {{"out", "std::Number"}}));
    }

    inline rt_basic::Callable method_window_listbox_selection() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto idx = rb::number_of(rb::para_at(paras, 0));
                if (!win || !idx) return rb::list_of({rb::make_int(-1)});
                auto& v = ctrls_of(win); long i = (long)*idx;
                if (i < 0 || i >= (long)v.size()) return rb::list_of({rb::make_int(-1)});
                return rb::list_of({rb::make_int(gui_listbox_selection(v[(size_t)i]))});
            },
            rb::make_sign("listbox_selection", {{"index", "std::Number"}}, {{"out", "std::Number"}}));
    }

    // textarea(text, x, y, width, height) -> index
    inline rt_basic::Callable method_window_textarea() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto text = rb::string_of(rb::para_at(paras, 0));
                auto x = rb::number_of(rb::para_at(paras, 1));
                auto y = rb::number_of(rb::para_at(paras, 2));
                auto w = rb::number_of(rb::para_at(paras, 3));
                auto h = rb::number_of(rb::para_at(paras, 4));
                if (!win || !text || !x || !y || !w || !h)
                    return rb::list_of({rb::native_error(
                        "gui.textarea requires (text, x, y, width, height)")});
                gui_ctrl c = gui_add_textarea(win, text->c_str(), (int)*x, (int)*y, (int)*w, (int)*h);
                auto& v = ctrls_of(win); v.push_back(c);
                return rb::list_of({rb::make_int(static_cast<int64_t>(v.size() - 1))});
            },
            rb::make_sign("textarea",
                {{"text", "std::String"}, {"x", "std::Number"}, {"y", "std::Number"},
                 {"width", "std::Number"}, {"height", "std::Number"}},
                {{"out", "std::Number"}}));
    }

    inline rt_basic::Callable method_window_textarea_text() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto idx = rb::number_of(rb::para_at(paras, 0));
                if (!win || !idx) return rb::list_of({rb::make_string("")});
                auto& v = ctrls_of(win); long i = (long)*idx;
                if (i < 0 || i >= (long)v.size()) return rb::list_of({rb::make_string("")});
                return rb::list_of({rb::make_string(gui_textarea_text(v[(size_t)i]))});
            },
            rb::make_sign("textarea_text", {{"index", "std::Number"}}, {{"out", "std::String"}}));
    }

    // message(title, text, kind) ; kind: 0 info, 1 warn, 2 error
    inline rt_basic::Callable method_window_message() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto title = rb::string_of(rb::para_at(paras, 0));
                auto text = rb::string_of(rb::para_at(paras, 1));
                auto kind = rb::number_of(rb::para_at(paras, 2));
                int k = kind ? (int)*kind : 0;
                gui_show_message(win, title ? title->c_str() : "",
                                 text ? text->c_str() : "", k);
                return rb::empty_result();
            },
            rb::make_sign("message",
                {{"title", "std::String"}, {"text", "std::String"}, {"kind", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_window_on_close() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                gui_win win = win_of(env);
                auto action = rb::para_at(paras, 0);
                if (!win || !action) return rb::empty_result();
                GuiCb* cb = make_cb(action);
                gui_window_on_close(win, &gui_invoke, cb);
                return rb::empty_result();
            },
            rb::make_sign("on_close", {{"action", "@"}}, {}));
    }

    inline rt_basic::Callable method_window_mainloop() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr) {
                gui_win win = win_of(env);
                if (!win) return rb::empty_result();
                gui_run();
                return rb::empty_result();
            },
            rb::make_sign("mainloop", {}, {}));
    }

    inline rt_basic::Callable method_window_close() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr) {
                gui_win win = win_of(env);
                if (win) { gui_window_close(win); g_ctrls.erase((uintptr_t)win); }
                return rb::empty_result();
            },
            rb::make_sign("close", {}, {}));
    }

    inline void init_gui_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
        proto->set_method("open",            method_window_open());
        proto->set_method("set_title",       method_window_set_title());
        proto->set_method("label",           method_window_label());
        proto->set_method("button",          method_window_button());
        proto->set_method("entry",           method_window_entry());
        proto->set_method("entry_text",      method_window_entry_text());
        proto->set_method("checkbox",        method_window_checkbox());
        proto->set_method("checkbox_checked",method_window_checkbox_checked());
        proto->set_method("slider",          method_window_slider());
        proto->set_method("slider_value",    method_window_slider_value());
        proto->set_method("listbox",         method_window_listbox());
        proto->set_method("listbox_selection",method_window_listbox_selection());
        proto->set_method("textarea",        method_window_textarea());
        proto->set_method("textarea_text",   method_window_textarea_text());
        proto->set_method("message",         method_window_message());
        proto->set_method("on_close",        method_window_on_close());
        proto->set_method("mainloop",        method_window_mainloop());
        proto->set_method("close",           method_window_close());
        runtime::Prototypes p;
        p.regcls("Window", proto);
        ::stdRT.add_protos(p);
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("gui", &init_gui_stdlib), true);

} // namespace rt_lib_gui
