// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.
//
// windows_native_win32.cpp - Win32/GDI backend for the `windows` library.
// windows_native_win32.cpp —— `windows` 库的 Win32/GDI 后端（本机可测）。

#include "windows_native.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>

// Drawing command record (global so win_canvas_s may hold a vector of it).
// 绘制命令记录（置于全局作用域，使 win_canvas_s 可持其数组）。
struct Cmd {
    int type = 0;       // 1 line 2 rect 3 text 4 circle 5 ellipse 6 polygon
    int a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0, fill = 0;
    std::string text;
    std::vector<int> poly;
};

// Define the opaque struct declared forward in windows_native.h, in global scope
// (the header's `win_canvas` is a pointer to this exact tag).
// 在全局作用域定义 windows_native.h 中前向声明的同名不透明结构（头文件里的
// win_canvas 正是指向此精确标签的指针）。
struct win_canvas_s {
    HWND            hwnd = nullptr;
    HDC             hdc = nullptr;
    bool            active = false;
    int             bg_r = 255, bg_g = 255, bg_b = 255;
    std::vector<Cmd> cmds;
    win_draw_cb     draw_cb = nullptr;
    void*           draw_user = nullptr;
    win_key_cb      key_cb = nullptr;
    void*           key_user = nullptr;
    win_close_cb    close_cb = nullptr;
    void*           close_user = nullptr;
};

namespace {

std::vector<win_canvas_s*> g_canvas;
static const char* WIN_CLASS = "SyclunWinCanvasClass";
static bool g_class_reg = false;
static bool g_quit = false;

COLORREF rgb(int r, int g, int b) { return RGB(r & 255, g & 255, b & 255); }

void exec_cmd(HDC hdc, const Cmd& cmd) {
    switch (cmd.type) {
        case 1: { // line
            HPEN p = CreatePen(PS_SOLID, 1, rgb(cmd.e, cmd.f, cmd.g));
            HGDIOBJ old = SelectObject(hdc, p);
            MoveToEx(hdc, cmd.a, cmd.b, nullptr);
            LineTo(hdc, cmd.c, cmd.d);
            SelectObject(hdc, old); DeleteObject(p);
            break;
        }
        case 2: { // rect
            HBRUSH br = CreateSolidBrush(rgb(cmd.e, cmd.f, cmd.g));
            if (cmd.fill) { RECT r{cmd.a, cmd.b, cmd.a + cmd.c, cmd.b + cmd.d}; FillRect(hdc, &r, br); }
            else { HPEN p = CreatePen(PS_SOLID, 1, rgb(cmd.e, cmd.f, cmd.g));
                   HGDIOBJ old = SelectObject(hdc, p);
                   Rectangle(hdc, cmd.a, cmd.b, cmd.a + cmd.c, cmd.b + cmd.d);
                   SelectObject(hdc, old); DeleteObject(p); }
            DeleteObject(br);
            break;
        }
        case 3: { // text
            SetTextColor(hdc, rgb(cmd.e, cmd.f, cmd.g));
            SetBkMode(hdc, TRANSPARENT);
            TextOutA(hdc, cmd.a, cmd.b, cmd.text.c_str(), (int)cmd.text.size());
            break;
        }
        case 4: { // circle
            HBRUSH br = CreateSolidBrush(rgb(cmd.e, cmd.f, cmd.g));
            if (cmd.fill) { HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
                Ellipse(hdc, cmd.a - cmd.c, cmd.b - cmd.c, cmd.a + cmd.c, cmd.b + cmd.c);
                SelectObject(hdc, ob); }
            else { HPEN p = CreatePen(PS_SOLID, 1, rgb(cmd.e, cmd.f, cmd.g));
                   HGDIOBJ old = SelectObject(hdc, p);
                   Ellipse(hdc, cmd.a - cmd.c, cmd.b - cmd.c, cmd.a + cmd.c, cmd.b + cmd.c);
                   SelectObject(hdc, old); DeleteObject(p); }
            DeleteObject(br);
            break;
        }
        case 5: { // ellipse
            HBRUSH br = CreateSolidBrush(rgb(cmd.e, cmd.f, cmd.g));
            if (cmd.fill) { HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
                Ellipse(hdc, cmd.a, cmd.b, cmd.a + cmd.c, cmd.b + cmd.d);
                SelectObject(hdc, ob); }
            else { HPEN p = CreatePen(PS_SOLID, 1, rgb(cmd.e, cmd.f, cmd.g));
                   HGDIOBJ old = SelectObject(hdc, p);
                   Ellipse(hdc, cmd.a, cmd.b, cmd.a + cmd.c, cmd.b + cmd.d);
                   SelectObject(hdc, old); DeleteObject(p); }
            DeleteObject(br);
            break;
        }
        case 6: { // polygon
            if (cmd.poly.size() < 6) break;
            std::vector<POINT> pts; pts.reserve(cmd.poly.size() / 2);
            for (size_t i = 0; i + 1 < cmd.poly.size(); i += 2)
                pts.push_back({cmd.poly[i], cmd.poly[i + 1]});
            if (cmd.fill) { HBRUSH br = CreateSolidBrush(rgb(cmd.e, cmd.f, cmd.g));
                            HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
                            Polygon(hdc, pts.data(), (int)pts.size());
                            SelectObject(hdc, ob); DeleteObject(br); }
            else { HPEN p = CreatePen(PS_SOLID, 1, rgb(cmd.e, cmd.f, cmd.g));
                   HGDIOBJ old = SelectObject(hdc, p);
                   Polygon(hdc, pts.data(), (int)pts.size());
                   SelectObject(hdc, old); DeleteObject(p); }
            break;
        }
    }
}

LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    win_canvas_s* self = nullptr;
    if (m == WM_CREATE) { self = (win_canvas_s*)((CREATESTRUCT*)lp)->lpCreateParams;
        SetWindowLongPtrA(w, GWLP_USERDATA, (LONG_PTR)self); }
    else self = (win_canvas_s*)GetWindowLongPtrA(w, GWLP_USERDATA);
    if (!self) return DefWindowProcA(w, m, wp, lp);

    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(w, &ps);
        self->hdc = hdc; self->active = true;
        RECT rc; GetClientRect(w, &rc);
        HBRUSH bg = CreateSolidBrush(rgb(self->bg_r, self->bg_g, self->bg_b));
        FillRect(hdc, &rc, bg); DeleteObject(bg);
        for (auto& cmd : self->cmds) exec_cmd(hdc, cmd);
        self->cmds.clear();
        if (self->draw_cb) self->draw_cb(self->draw_user);
        self->active = false; self->hdc = nullptr;
        EndPaint(w, &ps);
        return 0;
    }
    if (m == WM_CHAR) {
        if (self->key_cb) { char ch = (char)wp; self->key_cb(&ch, self->key_user); }
        return 0;
    }
    if (m == WM_DESTROY) {
        if (self->close_cb) self->close_cb(self->close_user);
        auto it = std::find(g_canvas.begin(), g_canvas.end(), self);
        if (it != g_canvas.end()) g_canvas.erase(it);
        delete self;
        if (g_canvas.empty() || g_quit) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}

void ensure_class() {
    if (g_class_reg) return;
    WNDCLASSA wc{};
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = WIN_CLASS;
    RegisterClassA(&wc);
    g_class_reg = true;
}

void emit(win_canvas_s* c, const Cmd& cmd) {
    if (c->active && c->hdc) exec_cmd(c->hdc, cmd);
    else c->cmds.push_back(cmd);
}

} // namespace

extern "C" {

win_canvas win_canvas_create(const char* title, int w, int h) {
    ensure_class();
    auto* c = new win_canvas_s();
    HWND hwnd = CreateWindowExA(0, WIN_CLASS, title ? title : "",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, w > 0 ? w : 400, h > 0 ? h : 300,
        nullptr, nullptr, GetModuleHandleA(nullptr), c);
    if (!hwnd) { delete c; return nullptr; }
    c->hwnd = hwnd;
    g_canvas.push_back(c);
    return c;
}

void win_canvas_set_title(win_canvas cv, const char* title) {
    if (cv && cv->hwnd) SetWindowTextA(cv->hwnd, title ? title : "");
}
void win_canvas_on_draw(win_canvas cv, win_draw_cb cb, void* user) {
    if (cv) { cv->draw_cb = cb; cv->draw_user = user; }
}
void win_canvas_on_key(win_canvas cv, win_key_cb cb, void* user) {
    if (cv) { cv->key_cb = cb; cv->key_user = user; }
}
void win_canvas_on_close(win_canvas cv, win_close_cb cb, void* user) {
    if (cv) { cv->close_cb = cb; cv->close_user = user; }
}

void win_canvas_clear(win_canvas cv, int r, int g, int b) {
    if (!cv) return;
    cv->bg_r = r; cv->bg_g = g; cv->bg_b = b;
    cv->cmds.clear();
    if (cv->hwnd && !cv->active) InvalidateRect(cv->hwnd, nullptr, TRUE);
}

void win_canvas_draw_line(win_canvas cv, int x1, int y1, int x2, int y2, int r, int g, int b) {
    if (!cv) return;
    Cmd c; c.type = 1; c.a = x1; c.b = y1; c.c = x2; c.d = y2; c.e = r; c.f = g; c.g = b;
    emit(cv, c);
}
void win_canvas_draw_rect(win_canvas cv, int x, int y, int w, int h, int r, int g, int b, int fill) {
    if (!cv) return;
    Cmd c; c.type = 2; c.a = x; c.b = y; c.c = w; c.d = h; c.e = r; c.f = g; c.g = b; c.fill = fill;
    emit(cv, c);
}
void win_canvas_draw_text(win_canvas cv, int x, int y, const char* text, int r, int g, int b) {
    if (!cv) return;
    Cmd c; c.type = 3; c.a = x; c.b = y; c.e = r; c.f = g; c.g = b;
    c.text = text ? text : "";
    emit(cv, c);
}
void win_canvas_draw_circle(win_canvas cv, int cx, int cy, int radius, int r, int g, int b, int fill) {
    if (!cv) return;
    Cmd c; c.type = 4; c.a = cx; c.b = cy; c.c = radius; c.e = r; c.f = g; c.g = b; c.fill = fill;
    emit(cv, c);
}
void win_canvas_draw_ellipse(win_canvas cv, int x, int y, int w, int h, int r, int g, int b, int fill) {
    if (!cv) return;
    Cmd c; c.type = 5; c.a = x; c.b = y; c.c = w; c.d = h; c.e = r; c.f = g; c.g = b; c.fill = fill;
    emit(cv, c);
}
void win_canvas_draw_polygon(win_canvas cv, const int* pts, int n, int r, int g, int b, int fill) {
    if (!cv || !pts || n < 3) return;
    Cmd c; c.type = 6; c.e = r; c.f = g; c.g = b; c.fill = fill;
    for (int i = 0; i < n * 2; ++i) c.poly.push_back(pts[i]);
    emit(cv, c);
}

void win_canvas_close(win_canvas cv) {
    if (cv && cv->hwnd) DestroyWindow(cv->hwnd);
}
void win_canvas_run(void) {
    g_quit = false;
    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg); DispatchMessageA(&msg);
    }
}
void win_canvas_quit(void) {
    g_quit = true;
    for (auto* c : g_canvas) if (c->hwnd) DestroyWindow(c->hwnd);
}

} // extern "C"
