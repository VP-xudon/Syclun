// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.
//
// gui_native_x11.cpp - X11 backend for the `gui` library.
// gui_native_x11.cpp —— `gui` 库的 X11 后端。
//
// Xlib has no native controls, so this backend implements a small retained
// widget layer: each control is a rectangle with a type, drawn on Expose and
// hit-tested on mouse/keyboard input. It is intentionally compact but fully
// functional (clickable buttons, typeable entries, selectable listboxes, ...).
// Xlib 没有原生控件，故本后端实现一个轻量保留式控件层：每个控件是一个带类型
// 的矩形，在 Expose 时绘制、在鼠标 / 键盘输入时命中测试。刻意保持精简但功能
// 完整（可点击按钮、可输入文本框、可选项列表等）。

#include "gui_native.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

// Opaque structs from gui_native.h, defined in global scope.
// gui_native.h 的不透明结构在全局作用域定义。
struct gui_ctrl_s {
    int      kind = 0;       // 1 button 2 checkbox 3 slider 4 listbox 5 entry 6 textarea 7 label
    int      x = 0, y = 0, w = 0, h = 0;
    std::string text;
    std::vector<std::string> items;
    bool     checked = false;
    int      minv = 0, maxv = 100, value = 0;
    int      selection = -1;
    bool     focused = false;
    gui_cb   cb = nullptr;
    void*    user = nullptr;
};

struct gui_win_s {
    Window   wnd = 0;
    std::string title;
    int      w = 0, h = 0;
    std::vector<gui_ctrl_s*> children;
    gui_cb   close_cb = nullptr;
    void*    close_user = nullptr;
    bool     alive = true;
    ~gui_win_s() { for (auto* c : children) delete c; }
};

namespace {

const int FONT_H = 16;

Display*      g_disp = nullptr;
int           g_screen = 0;
Window        g_root = 0;
GC            g_gc = 0;
XFontStruct*  g_font = nullptr;
Atom          g_wmDel = 0;
std::vector<gui_win_s*> g_windows;
bool          g_quit = false;

gui_win_s* find_win(Window w) {
    for (auto* win : g_windows) if (win->wnd == w) return win;
    return nullptr;
}

void redraw(gui_win_s* win) {
    XSetForeground(g_disp, g_gc, WhitePixel(g_disp, g_screen));
    XFillRectangle(g_disp, win->wnd, g_gc, 0, 0, win->w, win->h);
    XSetForeground(g_disp, g_gc, BlackPixel(g_disp, g_screen));
    for (auto* c : win->children) {
        int tx = c->x + 4, ty = c->y + FONT_H - 3;
        switch (c->kind) {
            case 7: // label
                XDrawString(g_disp, win->wnd, g_gc, tx, ty, c->text.c_str(), (int)c->text.size());
                break;
            case 1: // button
                XSetForeground(g_disp, g_gc, 0xDDDDDD);
                XFillRectangle(g_disp, win->wnd, g_gc, c->x, c->y, c->w, c->h);
                XSetForeground(g_disp, g_gc, BlackPixel(g_disp, g_screen));
                XDrawRectangle(g_disp, win->wnd, g_gc, c->x, c->y, c->w, c->h);
                XDrawString(g_disp, win->wnd, g_gc, tx, ty, c->text.c_str(), (int)c->text.size());
                break;
            case 2: { // checkbox
                XDrawRectangle(g_disp, win->wnd, g_gc, c->x, c->y, 14, 14);
                if (c->checked) {
                    XDrawLine(g_disp, win->wnd, g_gc, c->x + 2, c->y + 7, c->x + 6, c->y + 12);
                    XDrawLine(g_disp, win->wnd, g_gc, c->x + 6, c->y + 12, c->x + 12, c->y + 2);
                }
                XDrawString(g_disp, win->wnd, g_gc, c->x + 20, ty, c->text.c_str(), (int)c->text.size());
                break;
            }
            case 3: { // slider
                int trackY = c->y + c->h / 2;
                XDrawLine(g_disp, win->wnd, g_gc, c->x, trackY, c->x + c->w, trackY);
                int span = c->maxv - c->minv;
                int px = c->w; if (span > 0) px = (int)((double)(c->value - c->minv) / span * c->w);
                XFillRectangle(g_disp, win->wnd, g_gc, c->x + px - 4, c->y, 8, c->h);
                XDrawRectangle(g_disp, win->wnd, g_gc, c->x + px - 4, c->y, 8, c->h);
                break;
            }
            case 4: { // listbox
                XSetForeground(g_disp, g_gc, WhitePixel(g_disp, g_screen));
                XFillRectangle(g_disp, win->wnd, g_gc, c->x, c->y, c->w, c->h);
                XSetForeground(g_disp, g_gc, BlackPixel(g_disp, g_screen));
                XDrawRectangle(g_disp, win->wnd, g_gc, c->x, c->y, c->w, c->h);
                for (size_t i = 0; i < c->items.size(); ++i) {
                    int ly = c->y + FONT_H - 3 + (int)(i * FONT_H);
                    if ((int)i == c->selection) {
                        XSetForeground(g_disp, g_gc, 0xCCCCFF);
                        XFillRectangle(g_disp, win->wnd, g_gc, c->x + 1, c->y + (int)(i * FONT_H), c->w - 2, FONT_H);
                        XSetForeground(g_disp, g_gc, BlackPixel(g_disp, g_screen));
                    }
                    XDrawString(g_disp, win->wnd, g_gc, c->x + 4, ly, c->items[i].c_str(), (int)c->items[i].size());
                }
                break;
            }
            case 5: // entry
            case 6: // textarea
                XSetForeground(g_disp, g_gc, WhitePixel(g_disp, g_screen));
                XFillRectangle(g_disp, win->wnd, g_gc, c->x, c->y, c->w, c->h);
                XSetForeground(g_disp, g_gc, BlackPixel(g_disp, g_screen));
                XDrawRectangle(g_disp, win->wnd, g_gc, c->x, c->y, c->w, c->h);
                if (c->kind == 5)
                    XDrawString(g_disp, win->wnd, g_gc, tx, ty, c->text.c_str(), (int)c->text.size());
                else {
                    size_t pos = 0;
                    int line = 0;
                    while (pos < c->text.size()) {
                        size_t nl = c->text.find('\n', pos);
                        std::string ln = (nl == std::string::npos) ? c->text.substr(pos) : c->text.substr(pos, nl - pos);
                        XDrawString(g_disp, win->wnd, g_gc, tx, c->y + FONT_H - 3 + line * FONT_H, ln.c_str(), (int)ln.size());
                        if (nl == std::string::npos) break;
                        pos = nl + 1; ++line;
                    }
                }
                break;
        }
    }
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

void dispose_win(gui_win_s* win) {
    if (win->close_cb) win->close_cb(win->close_user);
    auto it = std::find(g_windows.begin(), g_windows.end(), win);
    if (it != g_windows.end()) g_windows.erase(it);
    if (win->wnd) XDestroyWindow(g_disp, win->wnd);
    delete win;
    if (g_windows.empty() || g_quit) {
        if (g_font) { XFreeFont(g_disp, g_font); g_font = nullptr; }
        if (g_disp) { XCloseDisplay(g_disp); g_disp = nullptr; }
    }
}

} // namespace

extern "C" {

gui_win gui_window_create(const char* title, int w, int h) {
    ensure_init();
    auto* win = new gui_win_s();
    win->title = title ? title : "";
    win->w = w > 0 ? w : 400; win->h = h > 0 ? h : 300;
    win->wnd = XCreateSimpleWindow(g_disp, g_root, 60, 60, win->w, win->h, 1,
                                   BlackPixel(g_disp, g_screen), WhitePixel(g_disp, g_screen));
    XStoreName(g_disp, win->wnd, win->title.c_str());
    XSetWMProtocols(g_disp, win->wnd, &g_wmDel, 1);
    XSelectInput(g_disp, win->wnd, ExposureMask | ButtonPressMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(g_disp, win->wnd);
    g_windows.push_back(win);
    return win;
}

void gui_window_set_title(gui_win win, const char* title) {
    if (win && title) { win->title = title; XStoreName(g_disp, win->wnd, title); XFlush(g_disp); }
}

void gui_window_close(gui_win win) {
    if (win) dispose_win(win);
}

void gui_window_on_close(gui_win win, gui_cb cb, void* user) {
    if (win) { win->close_cb = cb; win->close_user = user; }
}

static gui_ctrl add_ctrl(gui_win win, int kind, int x, int y, int w, int h, const char* text) {
    if (!win) return nullptr;
    auto* c = new gui_ctrl_s();
    c->kind = kind; c->x = x; c->y = y; c->w = w; c->h = h;
    if (text) c->text = text;
    win->children.push_back(c);
    return c;
}

gui_ctrl gui_add_label(gui_win win, const char* text, int x, int y) {
    return add_ctrl(win, 7, x, y, 240, FONT_H, text);
}

gui_ctrl gui_add_button(gui_win win, const char* text, int x, int y, int w, int h, gui_cb cb, void* user) {
    auto* c = add_ctrl(win, 1, x, y, w > 0 ? w : 100, h > 0 ? h : 28, text);
    if (c) { c->cb = cb; c->user = user; }
    return c;
}

gui_ctrl gui_add_entry(gui_win win, const char* placeholder, int x, int y, int w, int h) {
    return add_ctrl(win, 5, x, y, w > 0 ? w : 160, h > 0 ? h : FONT_H + 6, placeholder);
}

const char* gui_entry_text(gui_ctrl ctrl) {
    return ctrl ? ctrl->text.c_str() : "";
}

gui_ctrl gui_add_checkbox(gui_win win, const char* text, int x, int y, int checked) {
    auto* c = add_ctrl(win, 2, x, y, 200, 18, text);
    if (c) c->checked = checked != 0;
    return c;
}

int gui_checkbox_checked(gui_ctrl ctrl) {
    return (ctrl && ctrl->checked) ? 1 : 0;
}

gui_ctrl gui_add_slider(gui_win win, int x, int y, int w, int minv, int maxv, int val) {
    auto* c = add_ctrl(win, 3, x, y, w > 0 ? w : 160, 20, "");
    if (c) { c->minv = minv; c->maxv = maxv; c->value = val; }
    return c;
}

int gui_slider_value(gui_ctrl ctrl) {
    return ctrl ? ctrl->value : 0;
}

gui_ctrl gui_add_listbox(gui_win win, const char* const* items, int n, int x, int y, int w, int h) {
    auto* c = add_ctrl(win, 4, x, y, w > 0 ? w : 160, h > 0 ? h : 120, "");
    if (c && items) for (int i = 0; i < n; ++i) c->items.push_back(items[i] ? items[i] : "");
    return c;
}

int gui_listbox_selection(gui_ctrl ctrl) {
    return ctrl ? ctrl->selection : -1;
}

gui_ctrl gui_add_textarea(gui_win win, const char* text, int x, int y, int w, int h) {
    return add_ctrl(win, 6, x, y, w > 0 ? w : 200, h > 0 ? h : 100, text);
}

const char* gui_textarea_text(gui_ctrl ctrl) {
    return gui_entry_text(ctrl);
}

int gui_show_message(gui_win parent, const char* title, const char* msg, int) {
    (void)parent; (void)title; (void)msg;
    if (msg) std::fprintf(stderr, "[gui message] %s\n", msg);
    return 0;
}

void gui_run(void) {
    g_quit = false;
    if (g_windows.empty()) return;
    XEvent ev;
    while (true) {
        if (g_windows.empty() || g_quit) break;
        XNextEvent(g_disp, &ev);
        gui_win_s* win = find_win(ev.xany.window);
        if (!win) continue;
        if (ev.type == Expose) { redraw(win); }
        else if (ev.type == ClientMessage) {
            if ((Atom)ev.xclient.data.l[0] == g_wmDel) dispose_win(win);
        }
        else if (ev.type == ButtonPress) {
            int mx = ev.xbutton.x, my = ev.xbutton.y;
            for (auto* c : win->children) {
                if (mx < c->x || mx > c->x + c->w || my < c->y || my > c->y + c->h) continue;
                if (c->kind == 1) { if (c->cb) c->cb(c->user); }
                else if (c->kind == 2) { c->checked = !c->checked; if (c->cb) c->cb(c->user); }
                else if (c->kind == 3) {
                    int span = c->maxv - c->minv;
                    if (c->w > 0) c->value = c->minv + (int)((double)(mx - c->x) / c->w * span);
                    if (c->value < c->minv) c->value = c->minv;
                    if (c->value > c->maxv) c->value = c->maxv;
                    if (c->cb) c->cb(c->user);
                }
                else if (c->kind == 4) {
                    int idx = (my - c->y) / FONT_H;
                    if (idx >= 0 && idx < (int)c->items.size()) { c->selection = idx; if (c->cb) c->cb(c->user); }
                }
                else if (c->kind == 5 || c->kind == 6) {
                    for (auto* o : win->children) o->focused = false;
                    c->focused = true;
                }
                redraw(win);
                break;
            }
        }
        else if (ev.type == KeyPress) {
            for (auto* c : win->children) {
                if (!c->focused) continue;
                char buf[8]; KeySym ks;
                int n = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, nullptr);
                if (ks == XK_BackSpace && !c->text.empty()) c->text.pop_back();
                else if (ks == XK_Return && c->kind == 6) c->text += '\n';
                else if (n > 0 && (buf[0] >= 32)) c->text += std::string(buf, n);
                redraw(win);
                break;
            }
        }
    }
    while (!g_windows.empty()) dispose_win(g_windows.front());
}

void gui_quit(void) {
    g_quit = true;
}

} // extern "C"
