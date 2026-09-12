// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.
//
// gui_native.h - platform-neutral C bridge for the `gui` standard library.
// gui_native.h —— `gui` 标准库的平台无关 C 桥接层。
//
// The interpreter core is header-only and must not leak platform windowing
// headers (windows.h / X11 / Cocoa) into the parser/lexer translation units.
// Every backend (Win32 / X11 / Cocoa) implements this exact C ABI; only the
// matching .cpp/.mm is compiled per platform, so a binary links exactly one
// implementation. The Synth-facing gui.hpp wraps these calls and bridges
// Synth-OOP closures into the `gui_cb` callback.
// 解释器核心是 header-only，绝不能把平台窗口头（windows.h / X11 /
// Cocoa）泄漏进 parser/lexer 的翻译单元。每个后端（Win32 / X11 /
// Cocoa）实现同一套 C ABI；按平台只编译对应的 .cpp/.mm，故二进制恰好链接
// 一个实现。面向 Synth 的 gui.hpp 封装这些调用，并把 Synth-OOP 闭包桥接进
// `gui_cb` 回调。

#ifndef SYNTH_GUI_NATIVE_H
#define SYNTH_GUI_NATIVE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gui_win_s*  gui_win;
typedef struct gui_ctrl_s* gui_ctrl;

// A user-supplied callback. `user` carries an opaque payload (the wrapped
// Synth-OOP closure); the backend stores it and invokes it on the UI thread.
// 用户回调。`user` 携带不透明负载（包装好的 Synth-OOP 闭包）；后端保存它，
// 并在 UI 线程上调用。
typedef void (*gui_cb)(void* user);

// ---- Window ----------------------------------------------------------------
// Create a top-level window. The app/event-loop is managed internally.
// 创建顶层窗口；应用 / 事件循环在内部托管。
gui_win gui_window_create(const char* title, int w, int h);
void    gui_window_set_title(gui_win win, const char* title);
void    gui_window_close(gui_win win);
void    gui_window_on_close(gui_win win, gui_cb cb, void* user);

// ---- Widgets ---------------------------------------------------------------
// All coordinates are in window pixels (top-left origin).
// 所有坐标均为窗口像素（左上原点）。
gui_ctrl gui_add_label(gui_win win, const char* text, int x, int y);

gui_ctrl gui_add_button(gui_win win, const char* text, int x, int y,
                        int w, int h, gui_cb cb, void* user);

gui_ctrl gui_add_entry(gui_win win, const char* placeholder,
                       int x, int y, int w, int h);
// Returns an internal buffer; copy it immediately (not guaranteed stable).
// 返回内部缓冲；请立即拷贝（不保证长期稳定）。
const char* gui_entry_text(gui_ctrl ctrl);

gui_ctrl gui_add_checkbox(gui_win win, const char* text, int x, int y, int checked);
int      gui_checkbox_checked(gui_ctrl ctrl);

gui_ctrl gui_add_slider(gui_win win, int x, int y, int w, int minv, int maxv, int val);
int      gui_slider_value(gui_ctrl ctrl);

// items: array of n NUL-terminated strings (may be NULL for empty list).
// items：长度为 n 的 NUL 结尾字符串数组（空列表可传 NULL）。
gui_ctrl gui_add_listbox(gui_win win, const char* const* items, int n,
                         int x, int y, int w, int h);
int      gui_listbox_selection(gui_ctrl ctrl);   // -1 if none

gui_ctrl gui_add_textarea(gui_win win, const char* text, int x, int y, int w, int h);
const char* gui_textarea_text(gui_ctrl ctrl);

// kind: 0 = info, 1 = warning, 2 = error. Returns 0/1/2.
// kind：0=信息 1=警告 2=错误。返回 0/1/2。
int gui_show_message(gui_win parent, const char* title, const char* msg, int kind);

// ---- Event loop ------------------------------------------------------------
// Run the platform event loop until every window is closed. Blocking.
// 运行平台事件循环，直至所有窗口关闭。阻塞。
void gui_run(void);
// Ask the running loop to terminate (e.g. from a callback). Non-blocking.
// 请求正在运行的循环退出（例如从回调中）。非阻塞。
void gui_quit(void);

#ifdef __cplusplus
}
#endif

#endif // SYNTH_GUI_NATIVE_H
