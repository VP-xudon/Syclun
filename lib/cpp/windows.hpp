// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/windows.hpp
//
// Standard library: windows (C++-backed backend wrapper).
// 标准库：windows（C++ 底层实现封装）。
//
// A pyglet-like 2-D canvas window. It is backed by a native C bridge
// (windows_native.h) with three real implementations: Win32 GDI (Windows, tested
// locally), X11 (Linux) and Cocoa (macOS) — so it is genuinely cross-platform
// and far richer than a thin GDI wrapper: lines / rectangles / text / circles /
// ellipses / polygons, filled or outlined, with on_draw / on_key / on_close
// callbacks. 类 pyglet 的 2D 画布窗口。由原生 C 桥（windows_native.h）支撑，
// 含三套真实实现：Win32 GDI（Windows，本机已测）、X11（Linux）、Cocoa（macOS），
// 故真正跨平台且远比薄 GDI 封装丰富：线 / 矩形 / 文本 / 圆 / 椭圆 / 多边形，
// 可填充或描边，并带 on_draw / on_key / on_close 回调。
//
// Threading note: callbacks run on the main (UI) thread; the interpreter already
// holds the GIL there, so closures are invoked without re-acquiring it.
// 线程说明：回调在主（UI）线程运行，解释器在此已持 GIL，故调用闭包时不再取锁。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <deque>
#include <cstdint>

#include "../../src/builtin.hpp"
#include "windows_native.h"

namespace rt_lib_windows {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    struct WinCb {
        RuntimeObjectPtr clos;
        bool key = false;
    };
    static std::deque<WinCb> g_cbstore;
    static void win_draw_thunk(void* user) {
        WinCb* c = static_cast<WinCb*>(user);
        if (!c || !c->clos) return;
        rt_basic::InstanceMap env;
        rb::call_behavior(c->clos, env, rb::empty_result());
    }
    static void win_key_thunk(const char* key, void* user) {
        WinCb* c = static_cast<WinCb*>(user);
        if (!c || !c->clos) return;
        rt_basic::InstanceMap env;
        rb::call_behavior(c->clos, env, rb::list_of({rb::make_string(key ? key : "")}));
    }
    static WinCb* make_cb(const RuntimeObjectPtr& clos, bool is_key) {
        g_cbstore.emplace_back();
        g_cbstore.back().clos = clos;
        g_cbstore.back().key = is_key;
        return &g_cbstore.back();
    }

    static win_canvas cv_of(rt_basic::InstanceMap& env) {
        auto it = env.find("#cv");
        if (it == env.end()) return nullptr;
        auto s = rb::string_of(it->second);
        if (!s) return nullptr;
        return (win_canvas)(uintptr_t)std::stoull(*s);
    }

    static void rgb_split(double col, int& r, int& g, int& b) {
        long v = (long)col;
        r = (int)((v >> 16) & 0xFF); g = (int)((v >> 8) & 0xFF); b = (int)(v & 0xFF);
    }

    inline rt_basic::Callable method_screen_open() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto title = rb::string_of(rb::para_at(paras, 0));
                auto w = rb::number_of(rb::para_at(paras, 1));
                auto h = rb::number_of(rb::para_at(paras, 2));
                if (!title || !w || !h)
                    return rb::list_of({rb::native_error("windows.open requires (title, width, height)")});
                win_canvas cv = win_canvas_create(title->c_str(), (int)*w, (int)*h);
                if (!cv) return rb::list_of({rb::native_error("windows: failed to create window")});
                env["#cv"] = rb::make_string(std::to_string((uintptr_t)cv));
                return rb::empty_result();
            },
            rb::make_sign("open",
                {{"title", "std::String"}, {"width", "std::Number"}, {"height", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_screen_set_title() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto title = rb::string_of(rb::para_at(paras, 0));
                if (cv && title) win_canvas_set_title(cv, title->c_str());
                return rb::empty_result();
            },
            rb::make_sign("set_title", {{"title", "std::String"}}, {}));
    }

    inline rt_basic::Callable method_screen_on_draw() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto action = rb::para_at(paras, 0);
                if (!cv || !action) return rb::empty_result();
                WinCb* cb = make_cb(action, false);
                win_canvas_on_draw(cv, &win_draw_thunk, cb);
                return rb::empty_result();
            },
            rb::make_sign("on_draw", {{"action", "@"}}, {}));
    }

    inline rt_basic::Callable method_screen_on_key() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto action = rb::para_at(paras, 0);
                if (!cv || !action) return rb::empty_result();
                WinCb* cb = make_cb(action, true);
                win_canvas_on_key(cv, &win_key_thunk, cb);
                return rb::empty_result();
            },
            rb::make_sign("on_key", {{"action", "@"}}, {}));
    }

    inline rt_basic::Callable method_screen_on_close() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto action = rb::para_at(paras, 0);
                if (!cv || !action) return rb::empty_result();
                WinCb* cb = make_cb(action, false);
                win_canvas_on_close(cv, &win_draw_thunk, cb);
                return rb::empty_result();
            },
            rb::make_sign("on_close", {{"action", "@"}}, {}));
    }

    inline rt_basic::Callable method_screen_clear() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto r = rb::number_of(rb::para_at(paras, 0));
                auto g = rb::number_of(rb::para_at(paras, 1));
                auto b = rb::number_of(rb::para_at(paras, 2));
                if (cv && r && g && b) win_canvas_clear(cv, (int)*r, (int)*g, (int)*b);
                return rb::empty_result();
            },
            rb::make_sign("clear",
                {{"r", "std::Number"}, {"g", "std::Number"}, {"b", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_screen_draw_line() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto x1 = rb::number_of(rb::para_at(paras, 0));
                auto y1 = rb::number_of(rb::para_at(paras, 1));
                auto x2 = rb::number_of(rb::para_at(paras, 2));
                auto y2 = rb::number_of(rb::para_at(paras, 3));
                auto c  = rb::number_of(rb::para_at(paras, 4));
                if (cv && x1 && y1 && x2 && y2 && c) {
                    int r, g, b; rgb_split(*c, r, g, b);
                    win_canvas_draw_line(cv, (int)*x1, (int)*y1, (int)*x2, (int)*y2, r, g, b);
                }
                return rb::empty_result();
            },
            rb::make_sign("draw_line",
                {{"x1", "std::Number"}, {"y1", "std::Number"}, {"x2", "std::Number"},
                 {"y2", "std::Number"}, {"color", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_screen_draw_rect() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto x = rb::number_of(rb::para_at(paras, 0));
                auto y = rb::number_of(rb::para_at(paras, 1));
                auto w = rb::number_of(rb::para_at(paras, 2));
                auto h = rb::number_of(rb::para_at(paras, 3));
                auto c = rb::number_of(rb::para_at(paras, 4));
                if (cv && x && y && w && h && c) {
                    int r, g, b; rgb_split(*c, r, g, b);
                    win_canvas_draw_rect(cv, (int)*x, (int)*y, (int)*w, (int)*h, r, g, b, 1);
                }
                return rb::empty_result();
            },
            rb::make_sign("draw_rect",
                {{"x", "std::Number"}, {"y", "std::Number"}, {"width", "std::Number"},
                 {"height", "std::Number"}, {"color", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_screen_draw_outline_rect() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto x = rb::number_of(rb::para_at(paras, 0));
                auto y = rb::number_of(rb::para_at(paras, 1));
                auto w = rb::number_of(rb::para_at(paras, 2));
                auto h = rb::number_of(rb::para_at(paras, 3));
                auto c = rb::number_of(rb::para_at(paras, 4));
                if (cv && x && y && w && h && c) {
                    int r, g, b; rgb_split(*c, r, g, b);
                    win_canvas_draw_rect(cv, (int)*x, (int)*y, (int)*w, (int)*h, r, g, b, 0);
                }
                return rb::empty_result();
            },
            rb::make_sign("draw_outline_rect",
                {{"x", "std::Number"}, {"y", "std::Number"}, {"width", "std::Number"},
                 {"height", "std::Number"}, {"color", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_screen_draw_text() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto x = rb::number_of(rb::para_at(paras, 0));
                auto y = rb::number_of(rb::para_at(paras, 1));
                auto t = rb::string_of(rb::para_at(paras, 2));
                auto c = rb::number_of(rb::para_at(paras, 3));
                if (cv && x && y && t && c) {
                    int r, g, b; rgb_split(*c, r, g, b);
                    win_canvas_draw_text(cv, (int)*x, (int)*y, t->c_str(), r, g, b);
                }
                return rb::empty_result();
            },
            rb::make_sign("draw_text",
                {{"x", "std::Number"}, {"y", "std::Number"}, {"text", "std::String"},
                 {"color", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_screen_draw_circle() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto cx = rb::number_of(rb::para_at(paras, 0));
                auto cy = rb::number_of(rb::para_at(paras, 1));
                auto rad = rb::number_of(rb::para_at(paras, 2));
                auto c = rb::number_of(rb::para_at(paras, 3));
                if (cv && cx && cy && rad && c) {
                    int r, g, b; rgb_split(*c, r, g, b);
                    win_canvas_draw_circle(cv, (int)*cx, (int)*cy, (int)*rad, r, g, b, 1);
                }
                return rb::empty_result();
            },
            rb::make_sign("draw_circle",
                {{"cx", "std::Number"}, {"cy", "std::Number"}, {"radius", "std::Number"},
                 {"color", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_screen_draw_outline_circle() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto cx = rb::number_of(rb::para_at(paras, 0));
                auto cy = rb::number_of(rb::para_at(paras, 1));
                auto rad = rb::number_of(rb::para_at(paras, 2));
                auto c = rb::number_of(rb::para_at(paras, 3));
                if (cv && cx && cy && rad && c) {
                    int r, g, b; rgb_split(*c, r, g, b);
                    win_canvas_draw_circle(cv, (int)*cx, (int)*cy, (int)*rad, r, g, b, 0);
                }
                return rb::empty_result();
            },
            rb::make_sign("draw_outline_circle",
                {{"cx", "std::Number"}, {"cy", "std::Number"}, {"radius", "std::Number"},
                 {"color", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_screen_draw_ellipse() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto x = rb::number_of(rb::para_at(paras, 0));
                auto y = rb::number_of(rb::para_at(paras, 1));
                auto w = rb::number_of(rb::para_at(paras, 2));
                auto h = rb::number_of(rb::para_at(paras, 3));
                auto c = rb::number_of(rb::para_at(paras, 4));
                if (cv && x && y && w && h && c) {
                    int r, g, b; rgb_split(*c, r, g, b);
                    win_canvas_draw_ellipse(cv, (int)*x, (int)*y, (int)*w, (int)*h, r, g, b, 1);
                }
                return rb::empty_result();
            },
            rb::make_sign("draw_ellipse",
                {{"x", "std::Number"}, {"y", "std::Number"}, {"width", "std::Number"},
                 {"height", "std::Number"}, {"color", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_screen_draw_outline_ellipse() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto x = rb::number_of(rb::para_at(paras, 0));
                auto y = rb::number_of(rb::para_at(paras, 1));
                auto w = rb::number_of(rb::para_at(paras, 2));
                auto h = rb::number_of(rb::para_at(paras, 3));
                auto c = rb::number_of(rb::para_at(paras, 4));
                if (cv && x && y && w && h && c) {
                    int r, g, b; rgb_split(*c, r, g, b);
                    win_canvas_draw_ellipse(cv, (int)*x, (int)*y, (int)*w, (int)*h, r, g, b, 0);
                }
                return rb::empty_result();
            },
            rb::make_sign("draw_outline_ellipse",
                {{"x", "std::Number"}, {"y", "std::Number"}, {"width", "std::Number"},
                 {"height", "std::Number"}, {"color", "std::Number"}}, {}));
    }

    // draw_polygon(points[Array], color) ; points = [x0,y0,x1,y1,...]
    inline rt_basic::Callable method_screen_draw_polygon() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto pts = rb::para_at(paras, 0);
                auto c = rb::number_of(rb::para_at(paras, 1));
                if (cv && pts && c) {
                    std::vector<int> flat;
                    auto* cls = dynamic_cast<RuntimeClass*>(pts.get());
                    if (cls) {
                        auto& am = cls->get_attributes();
                        long n = (long)rb::container_size(am);
                        for (long i = 0; i < n; ++i) {
                            auto it = am.find("#" + std::to_string(i));
                            if (it == am.end()) continue;
                            auto v = rb::number_of(it->second);
                            if (v) flat.push_back((int)*v);
                        }
                    }
                    if (flat.size() >= 6) {
                        int r, g, b; rgb_split(*c, r, g, b);
                        win_canvas_draw_polygon(cv, flat.data(), (int)flat.size() / 2, r, g, b, 1);
                    }
                }
                return rb::empty_result();
            },
            rb::make_sign("draw_polygon",
                {{"points", "std::Array"}, {"color", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_screen_draw_outline_polygon() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                win_canvas cv = cv_of(env);
                auto pts = rb::para_at(paras, 0);
                auto c = rb::number_of(rb::para_at(paras, 1));
                if (cv && pts && c) {
                    std::vector<int> flat;
                    auto* cls = dynamic_cast<RuntimeClass*>(pts.get());
                    if (cls) {
                        auto& am = cls->get_attributes();
                        long n = (long)rb::container_size(am);
                        for (long i = 0; i < n; ++i) {
                            auto it = am.find("#" + std::to_string(i));
                            if (it == am.end()) continue;
                            auto v = rb::number_of(it->second);
                            if (v) flat.push_back((int)*v);
                        }
                    }
                    if (flat.size() >= 6) {
                        int r, g, b; rgb_split(*c, r, g, b);
                        win_canvas_draw_polygon(cv, flat.data(), (int)flat.size() / 2, r, g, b, 0);
                    }
                }
                return rb::empty_result();
            },
            rb::make_sign("draw_outline_polygon",
                {{"points", "std::Array"}, {"color", "std::Number"}}, {}));
    }

    inline rt_basic::Callable method_screen_mainloop() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr) {
                win_canvas cv = cv_of(env);
                if (cv) win_canvas_run();
                return rb::empty_result();
            },
            rb::make_sign("mainloop", {}, {}));
    }

    inline rt_basic::Callable method_screen_close() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr) {
                win_canvas cv = cv_of(env);
                if (cv) win_canvas_close(cv);
                return rb::empty_result();
            },
            rb::make_sign("close", {}, {}));
    }

    inline void init_windows_stdlib() {
        auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
        proto->set_method("open",                 method_screen_open());
        proto->set_method("set_title",            method_screen_set_title());
        proto->set_method("on_draw",              method_screen_on_draw());
        proto->set_method("on_key",               method_screen_on_key());
        proto->set_method("on_close",             method_screen_on_close());
        proto->set_method("clear",                method_screen_clear());
        proto->set_method("draw_line",            method_screen_draw_line());
        proto->set_method("draw_rect",            method_screen_draw_rect());
        proto->set_method("draw_outline_rect",    method_screen_draw_outline_rect());
        proto->set_method("draw_text",            method_screen_draw_text());
        proto->set_method("draw_circle",          method_screen_draw_circle());
        proto->set_method("draw_outline_circle",   method_screen_draw_outline_circle());
        proto->set_method("draw_ellipse",         method_screen_draw_ellipse());
        proto->set_method("draw_outline_ellipse", method_screen_draw_outline_ellipse());
        proto->set_method("draw_polygon",         method_screen_draw_polygon());
        proto->set_method("draw_outline_polygon", method_screen_draw_outline_polygon());
        proto->set_method("mainloop",             method_screen_mainloop());
        proto->set_method("close",                method_screen_close());
        runtime::Prototypes p;
        p.regcls("Screen", proto);
        ::stdRT.add_protos(p);
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("windows", &init_windows_stdlib), true);

} // namespace rt_lib_windows
