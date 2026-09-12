// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.
//
// gui_native_win32.cpp - Win32/User32 backend for the `gui` library.
// gui_native_win32.cpp —— `gui` 库的 Win32/User32 后端（本机可测）。

#include "gui_native.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <algorithm>

// Define the opaque structs declared forward in gui_native.h, in global scope
// (the header's `gui_win` / `gui_ctrl` are pointers to these exact tags).
// 在全局作用域定义 gui_native.h 中前向声明的同名不透明结构（头文件里的
// gui_win / gui_ctrl 正是指向这些精确标签的指针）。
struct gui_ctrl_s {
    HWND     hwnd = nullptr;
    int      kind = 0;        // 1 button 2 checkbox 3 slider 4 listbox 5 entry 6 textarea 7 label
    gui_cb   cb   = nullptr;
    void*    user = nullptr;
    std::string textbuf;      // cached GetWindowText buffer
};

struct gui_win_s {
    HWND            hwnd = nullptr;
    gui_cb          close_cb = nullptr;
    void*           close_user = nullptr;
    std::vector<gui_ctrl_s*> children;
    ~gui_win_s() { for (auto* c : children) delete c; }
};

namespace {

static const char* GUI_CLASS = "SyclunGUIClass";
static bool g_class_reg = false;
static bool g_quit = false;
std::vector<gui_win_s*> g_windows;

LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    gui_win_s* self = nullptr;
    if (m == WM_CREATE) {
        self = (gui_win_s*)((CREATESTRUCT*)lp)->lpCreateParams;
        SetWindowLongPtrA(w, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (gui_win_s*)GetWindowLongPtrA(w, GWLP_USERDATA);
    }
    if (!self) return DefWindowProcA(w, m, wp, lp);

    if (m == WM_COMMAND) {
        HWND ctl = (HWND)lp;
        for (auto* c : self->children) {
            if (c->hwnd != ctl) continue;
            WORD note = HIWORD(wp);
            if (c->kind == 1 && note == BN_CLICKED) { if (c->cb) c->cb(c->user); return 0; }
            if (c->kind == 4 && note == LBN_SELCHANGE) { if (c->cb) c->cb(c->user); return 0; }
            if (c->kind == 3 && (note == TB_THUMBPOSITION || note == TB_THUMBTRACK || note == TB_ENDTRACK)) {
                if (c->cb) c->cb(c->user); return 0;
            }
        }
        return 0;
    }
    if (m == WM_DESTROY) {
        if (self->close_cb) self->close_cb(self->close_user);
        auto it = std::find(g_windows.begin(), g_windows.end(), self);
        if (it != g_windows.end()) g_windows.erase(it);
        delete self;
        if (g_windows.empty() || g_quit) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}

void ensure_class() {
    if (g_class_reg) return;
    WNDCLASSA wc{};
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = GUI_CLASS;
    RegisterClassA(&wc);
    g_class_reg = true;
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);
}

} // namespace

extern "C" {

gui_win gui_window_create(const char* title, int w, int h) {
    ensure_class();
    auto* win = new gui_win_s();
    HWND hwnd = CreateWindowExA(0, GUI_CLASS, title ? title : "",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, w > 0 ? w : 400, h > 0 ? h : 300,
        nullptr, nullptr, GetModuleHandleA(nullptr), win);
    if (!hwnd) { delete win; return nullptr; }
    win->hwnd = hwnd;
    g_windows.push_back(win);
    return win;
}

void gui_window_set_title(gui_win win, const char* title) {
    if (win && win->hwnd) SetWindowTextA(win->hwnd, title ? title : "");
}

void gui_window_close(gui_win win) {
    if (win && win->hwnd) DestroyWindow(win->hwnd);
}

void gui_window_on_close(gui_win win, gui_cb cb, void* user) {
    if (win) { win->close_cb = cb; win->close_user = user; }
}

static gui_ctrl add_ctrl(gui_win win, int kind, HWND hwnd) {
    if (!win || !hwnd) return nullptr;
    auto* c = new gui_ctrl_s();
    c->hwnd = hwnd; c->kind = kind;
    win->children.push_back(c);
    return c;
}

gui_ctrl gui_add_label(gui_win win, const char* text, int x, int y) {
    HWND c = CreateWindowExA(0, "static", text ? text : "",
        WS_CHILD | WS_VISIBLE, x, y, 240, 22, win->hwnd, nullptr,
        GetModuleHandleA(nullptr), nullptr);
    return add_ctrl(win, 7, c);
}

gui_ctrl gui_add_button(gui_win win, const char* text, int x, int y,
                        int w, int h, gui_cb cb, void* user) {
    HWND c = CreateWindowExA(0, "button", text ? text : "",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w > 0 ? w : 100, h > 0 ? h : 30,
        win->hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    auto* ctrl = add_ctrl(win, 1, c);
    if (ctrl) { ctrl->cb = cb; ctrl->user = user; }
    return ctrl;
}

gui_ctrl gui_add_entry(gui_win win, const char* placeholder, int x, int y, int w, int h) {
    HWND c = CreateWindowExA(WS_EX_CLIENTEDGE, "edit", placeholder ? placeholder : "",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y, w > 0 ? w : 160, h > 0 ? h : 24,
        win->hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    return add_ctrl(win, 5, c);
}

const char* gui_entry_text(gui_ctrl ctrl) {
    if (!ctrl || !ctrl->hwnd) return "";
    int n = GetWindowTextLengthA(ctrl->hwnd);
    ctrl->textbuf.resize((size_t)n + 1, '\0');
    GetWindowTextA(ctrl->hwnd, &ctrl->textbuf[0], n + 1);
    ctrl->textbuf.resize((size_t)n);
    return ctrl->textbuf.c_str();
}

gui_ctrl gui_add_checkbox(gui_win win, const char* text, int x, int y, int checked) {
    HWND c = CreateWindowExA(0, "button", text ? text : "",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x, y, 200, 24,
        win->hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    SendMessageA(c, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return add_ctrl(win, 2, c);
}

int gui_checkbox_checked(gui_ctrl ctrl) {
    if (!ctrl || !ctrl->hwnd) return 0;
    return SendMessageA(ctrl->hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
}

gui_ctrl gui_add_slider(gui_win win, int x, int y, int w, int minv, int maxv, int val) {
    HWND c = CreateWindowExA(0, "msctls_trackbar32", "",
        WS_CHILD | WS_VISIBLE | TBS_HORZ, x, y, w > 0 ? w : 160, 30,
        win->hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    SendMessageA(c, TBM_SETRANGE, TRUE, MAKELONG(minv, maxv));
    SendMessageA(c, TBM_SETPOS, TRUE, val);
    return add_ctrl(win, 3, c);
}

int gui_slider_value(gui_ctrl ctrl) {
    if (!ctrl || !ctrl->hwnd) return 0;
    return (int)SendMessageA(ctrl->hwnd, TBM_GETPOS, 0, 0);
}

gui_ctrl gui_add_listbox(gui_win win, const char* const* items, int n, int x, int y, int w, int h) {
    HWND c = CreateWindowExA(WS_EX_CLIENTEDGE, "listbox", "",
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL, x, y, w > 0 ? w : 160, h > 0 ? h : 120,
        win->hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    for (int i = 0; i < n; ++i)
        SendMessageA(c, LB_ADDSTRING, 0, (LPARAM)(items ? items[i] : ""));
    return add_ctrl(win, 4, c);
}

int gui_listbox_selection(gui_ctrl ctrl) {
    if (!ctrl || !ctrl->hwnd) return -1;
    return (int)SendMessageA(ctrl->hwnd, LB_GETCURSEL, 0, 0);
}

gui_ctrl gui_add_textarea(gui_win win, const char* text, int x, int y, int w, int h) {
    HWND c = CreateWindowExA(WS_EX_CLIENTEDGE, "edit", text ? text : "",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        x, y, w > 0 ? w : 200, h > 0 ? h : 100,
        win->hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    return add_ctrl(win, 6, c);
}

const char* gui_textarea_text(gui_ctrl ctrl) {
    return gui_entry_text(ctrl);
}

int gui_show_message(gui_win parent, const char* title, const char* msg, int kind) {
    UINT flags = MB_OK;
    if (kind == 1) flags |= MB_ICONWARNING;
    else if (kind == 2) flags |= MB_ICONERROR;
    int r = MessageBoxA(parent && parent->hwnd ? parent->hwnd : nullptr,
                        msg ? msg : "", title ? title : "", flags);
    return r == IDOK ? 0 : r;
}

void gui_run(void) {
    g_quit = false;
    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

void gui_quit(void) {
    g_quit = true;
    for (auto* win : g_windows) if (win->hwnd) DestroyWindow(win->hwnd);
}

} // extern "C"
