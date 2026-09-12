// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.
//
// windows_native_x11.cpp - X11 backend for the `windows` library.
// windows_native_x11.cpp —— `windows` 库的 X11 后端（类 pyglet 的 2D 画布）。

#include "windows_native.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

// Drawing command record (global so win_canvas_s may hold a vector of it).
// 绘制命令记录（置于全局作用域，使 win_canvas_s 可持其数组）。
struct Cmd {
    int type = 0;
    int a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0, fill = 0;
    std::string text;
    std::vector<int> poly;
};

// Opaque struct from windows_native.h, defined in global scope.
// windows_native.h 的不透明结构在全局作用域定义。
struct win_canvas_s {
    Window   wnd = 0;
    bool     active = false;
    int      bg_r = 255, bg_g = 255, bg_b = 255;
    std::vector<Cmd> cmds;
    win_draw_cb draw_cb = nullptr; void* draw_user = nullptr;
    win_key_cb  key_cb = nullptr;  void* key_user = nullptr;
    win_close_cb close_cb = nullptr; void* close_user = nullptr;
};

namespace {

Display* g_disp = nullptr; int g_screen = 0; Window g_root = 0;
GC g_gc = 0; XFontStruct* g_font = nullptr; Atom g_wmDel = 0;
std::vector<win_canvas_s*> g_canvas; bool g_quit = false;

win_canvas_s* find_cv(Window w) {
    for (auto* c : g_canvas) if (c->wnd == w) return c;
    return nullptr;
}
unsigned long rgb(int r, int g, int b) {
    return ((r & 255) << 16) | ((g & 255) << 8) | (b & 255);
}

void exec_cmd(Display* d, Window w, GC gc, const Cmd& cmd) {
    switch (cmd.type) {
        case 1:
            XSetForeground(d, gc, rgb(cmd.e, cmd.f, cmd.g));
            XDrawLine(d, w, gc, cmd.a, cmd.b, cmd.c, cmd.d);
            break;
        case 2:
            XSetForeground(d, gc, rgb(cmd.e, cmd.f, cmd.g));
            if (cmd.fill) XFillRectangle(d, w, gc, cmd.a, cmd.b, cmd.c, cmd.d);
            else XDrawRectangle(d, w, gc, cmd.a, cmd.b, cmd.c, cmd.d);
            break;
        case 3:
            XSetForeground(d, gc, rgb(cmd.e, cmd.f, cmd.g));
            XDrawString(d, w, gc, cmd.a, cmd.b, cmd.text.c_str(), (int)cmd.text.size());
            break;
        case 4: {
            XSetForeground(d, gc, rgb(cmd.e, cmd.f, cmd.g));
            int rad = cmd.c;
            if (cmd.fill) XFillArc(d, w, gc, cmd.a - rad, cmd.b - rad, rad * 2, rad * 2, 0, 360 * 64);
            else XDrawArc(d, w, gc, cmd.a - rad, cmd.b - rad, rad * 2, rad * 2, 0, 360 * 64);
            break;
        }
        case 5:
            XSetForeground(d, gc, rgb(cmd.e, cmd.f, cmd.g));
            if (cmd.fill) XFillArc(d, w, gc, cmd.a, cmd.b, cmd.c, cmd.d, 0, 360 * 64);
            else XDrawArc(d, w, gc, cmd.a, cmd.b, cmd.c, cmd.d, 0, 360 * 64);
            break;
        case 6:
            if (cmd.poly.size() < 6) break;
            XSetForeground(d, gc, rgb(cmd.e, cmd.f, cmd.g));
            {
                std::vector<XPoint> pts; pts.reserve(cmd.poly.size() / 2);
                for (size_t i = 0; i + 1 < cmd.poly.size(); i += 2)
                    pts.push_back({(short)cmd.poly[i], (short)cmd.poly[i + 1]});
                if (cmd.fill) XFillPolygon(d, w, gc, pts.data(), (int)pts.size(), Complex, CoordModeOrigin);
                else XDrawLines(d, w, gc, pts.data(), (int)pts.size(), CoordModeOrigin);
            }
            break;
    }
}

void redraw(win_canvas_s* c) {
    c->active = true;
    XSetForeground(g_disp, g_gc, rgb(c->bg_r, c->bg_g, c->bg_b));
    XFillRectangle(g_disp, c->wnd, g_gc, 0, 0, 4000, 4000);
    for (auto& cmd : c->cmds) exec_cmd(g_disp, c->wnd, g_gc, cmd);
    c->cmds.clear();
    if (c->draw_cb) c->draw_cb(c->draw_user);
    c->active = false;
    XFlush(g_disp);
}

void ensure_init() {
    if (g_disp) return;
    g_disp = XOpenDisplay(nullptr);
    g_screen = DefaultScreen(g_disp);
    g_root = RootWindow(g_disp, g_screen);
    g_gc = DefaultGC(g_disp, g_screen);
    g_font = XLoadQueryFont(g_disp, "fixed");
    if (g_font) XSetFont(g_disp, g_gc, g_font->fid);
    g_wmDel = XInternAtom(g_disp, "WM_DELETE_WINDOW", False);
}

void dispose(win_canvas_s* c) {
    if (c->close_cb) c->close_cb(c->close_user);
    auto it = std::find(g_canvas.begin(), g_canvas.end(), c);
    if (it != g_canvas.end()) g_canvas.erase(it);
    if (c->wnd) XDestroyWindow(g_disp, c->wnd);
    delete c;
    if (g_canvas.empty() || g_quit) {
        if (g_font) { XFreeFont(g_disp, g_font); g_font = nullptr; }
        if (g_disp) { XCloseDisplay(g_disp); g_disp = nullptr; }
    }
}

void emit(win_canvas_s* c, const Cmd& cmd) {
    if (c->active) exec_cmd(g_disp, c->wnd, g_gc, cmd);
    else c->cmds.push_back(cmd);
}

} // namespace

extern "C" {

win_canvas win_canvas_create(const char* title, int w, int h) {
    ensure_init();
    auto* c = new win_canvas_s();
    c->wnd = XCreateSimpleWindow(g_disp, g_root, 60, 60, w > 0 ? w : 400, h > 0 ? h : 300, 1,
                                 BlackPixel(g_disp, g_screen), WhitePixel(g_disp, g_screen));
    XStoreName(g_disp, c->wnd, title ? title : "");
    XSetWMProtocols(g_disp, c->wnd, &g_wmDel, 1);
    XSelectInput(g_disp, c->wnd, ExposureMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(g_disp, c->wnd);
    g_canvas.push_back(c);
    return c;
}
void win_canvas_set_title(win_canvas cv, const char* title) {
    if (cv && title) { XStoreName(g_disp, cv->wnd, title); XFlush(g_disp); }
}
void win_canvas_on_draw(win_canvas cv, win_draw_cb cb, void* user) { if (cv) { cv->draw_cb = cb; cv->draw_user = user; } }
void win_canvas_on_key(win_canvas cv, win_key_cb cb, void* user)   { if (cv) { cv->key_cb = cb; cv->key_user = user; } }
void win_canvas_on_close(win_canvas cv, win_close_cb cb, void* user){ if (cv) { cv->close_cb = cb; cv->close_user = user; } }

void win_canvas_clear(win_canvas cv, int r, int g, int b) {
    if (!cv) return;
    cv->bg_r = r; cv->bg_g = g; cv->bg_b = b;
    cv->cmds.clear();
}
void win_canvas_draw_line(win_canvas cv, int x1, int y1, int x2, int y2, int r, int g, int b) {
    if (!cv) return; Cmd c; c.type = 1; c.a = x1; c.b = y1; c.c = x2; c.d = y2; c.e = r; c.f = g; c.g = b; emit(cv, c);
}
void win_canvas_draw_rect(win_canvas cv, int x, int y, int w, int h, int r, int g, int b, int fill) {
    if (!cv) return; Cmd c; c.type = 2; c.a = x; c.b = y; c.c = w; c.d = h; c.e = r; c.f = g; c.g = b; c.fill = fill; emit(cv, c);
}
void win_canvas_draw_text(win_canvas cv, int x, int y, const char* text, int r, int g, int b) {
    if (!cv) return; Cmd c; c.type = 3; c.a = x; c.b = y; c.e = r; c.f = g; c.g = b; c.text = text ? text : ""; emit(cv, c);
}
void win_canvas_draw_circle(win_canvas cv, int cx, int cy, int radius, int r, int g, int b, int fill) {
    if (!cv) return; Cmd c; c.type = 4; c.a = cx; c.b = cy; c.c = radius; c.e = r; c.f = g; c.g = b; c.fill = fill; emit(cv, c);
}
void win_canvas_draw_ellipse(win_canvas cv, int x, int y, int w, int h, int r, int g, int b, int fill) {
    if (!cv) return; Cmd c; c.type = 5; c.a = x; c.b = y; c.c = w; c.d = h; c.e = r; c.f = g; c.g = b; c.fill = fill; emit(cv, c);
}
void win_canvas_draw_polygon(win_canvas cv, const int* pts, int n, int r, int g, int b, int fill) {
    if (!cv || !pts || n < 3) return;
    Cmd c; c.type = 6; c.e = r; c.f = g; c.g = b; c.fill = fill;
    for (int i = 0; i < n * 2; ++i) c.poly.push_back(pts[i]);
    emit(cv, c);
}

void win_canvas_close(win_canvas cv) { if (cv) dispose(cv); }

void win_canvas_run(void) {
    g_quit = false;
    if (g_canvas.empty()) return;
    XEvent ev;
    while (true) {
        if (g_canvas.empty() || g_quit) break;
        XNextEvent(g_disp, &ev);
        win_canvas_s* c = find_cv(ev.xany.window);
        if (!c) continue;
        if (ev.type == Expose) redraw(c);
        else if (ev.type == ClientMessage) {
            if ((Atom)ev.xclient.data.l[0] == g_wmDel) dispose(c);
        }
        else if (ev.type == KeyPress) {
            char buf[8]; KeySym ks; int n = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, nullptr);
            if (n > 0 && c->key_cb) c->key_cb(buf, c->key_user);
        }
    }
    while (!g_canvas.empty()) dispose(g_canvas.front());
}
void win_canvas_quit(void) { g_quit = true; }

} // extern "C"
