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
//
// Themability (D9): a global GuiTheme plus an optional per-control GuiStyle
// override give the library a modern, ttk-like look — global font / corner
// radius / color scheme, each widget optionally overriding those.
// 主题化（D9）：全局 GuiTheme 叠加每控件可选的 GuiStyle 覆盖，赋予本库现代、
// 类 ttk 的外观——全局字体 / 圆角 / 配色，各控件可选择性覆盖。

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

// ---- Colors / 颜色 --------------------------------------------------------
// 0..255 RGBA. `a` is currently unused (kept for future alpha support).
// 0..255 的 RGBA。a 当前未用（为未来 alpha 支持预留）。
typedef struct GuiColor {
    unsigned char r, g, b, a;
} GuiColor;

// ---- Theme / 主题 --------------------------------------------------------
// Global visual scheme. A NULL field means "inherit platform default" where
// applicable; for fonts an empty string falls back to the system UI font.
// 全局视觉方案。适用处 NULL 字段表示「继承平台默认」；字体空串回退到系统 UI 字体。
typedef struct GuiTheme {
    const char* font_family;   // e.g. "Segoe UI" / "Helvetica" / NULL=default
    int         font_size;     // px; 0 = default
    int         corner_radius; // px; 0 = square corners
    GuiColor    bg;            // window / control background
    GuiColor    fg;            // text color
    GuiColor    accent;        // primary / highlight color
    int         dark;          // 0 = light, 1 = dark
} GuiTheme;

// ---- Per-control style override / 每控件样式覆盖 ---------------------------
// Optional override applied on top of the global theme. Each field is only used
// when its flag bit is set (so an override can change just the corner radius,
// leaving everything else inherited from the theme).
// 叠加在全局主题之上的可选覆盖。仅当对应标志位被置位时才生效（故覆盖可只改
// 圆角，其余沿用主题）。
#define GUI_STYLE_FONT_FAMILY (1u << 0)
#define GUI_STYLE_FONT_SIZE   (1u << 1)
#define GUI_STYLE_CORNER      (1u << 2)
#define GUI_STYLE_BG          (1u << 3)
#define GUI_STYLE_FG          (1u << 4)
#define GUI_STYLE_ACCENT      (1u << 5)

typedef struct GuiStyle {
    unsigned int flags;        // OR of GUI_STYLE_* bits
    const char*  font_family;  // valid if GUI_STYLE_FONT_FAMILY
    int          font_size;    // valid if GUI_STYLE_FONT_SIZE
    int          corner_radius;// valid if GUI_STYLE_CORNER
    GuiColor     bg;           // valid if GUI_STYLE_BG
    GuiColor     fg;           // valid if GUI_STYLE_FG
    GuiColor     accent;       // valid if GUI_STYLE_ACCENT
} GuiStyle;

// ---- Theme management / 主题管理 ------------------------------------------
// Install a global theme used by all subsequently created windows and controls.
// Pass NULL to reset to platform defaults. The storage is copied; the caller
// may free its own copy immediately.
// 安装全局主题，作用于此后创建的全部窗口与控件。传 NULL 复位为平台默认。
// 数据会被复制，调用方可立即释放自己的副本。
void gui_set_theme(const GuiTheme* theme);

// Apply a theme to an already-created window (dark mode, corner radius, bg).
// 把主题应用到已创建的窗口（暗色模式、圆角、背景）。
void gui_window_apply_theme(gui_win win, const GuiTheme* theme);

// ---- Window ----------------------------------------------------------------
// Create a top-level window. The app/event-loop is managed internally.
// 创建顶层窗口；应用 / 事件循环在内部托管。
gui_win gui_window_create(const char* title, int w, int h);
void    gui_window_set_title(gui_win win, const char* title);
void    gui_window_close(gui_win win);
void    gui_window_on_close(gui_win win, gui_cb cb, void* user);

// ---- Widgets ---------------------------------------------------------------
// All coordinates are in window pixels (top-left origin). `st` is an optional
// per-control style override (NULL = inherit the global theme).
// 所有坐标均为窗口像素（左上原点）。st 为可选的每控件样式覆盖（NULL = 沿用
// 全局主题）。
gui_ctrl gui_add_label(gui_win win, const char* text, int x, int y,
                       const GuiStyle* st);

gui_ctrl gui_add_button(gui_win win, const char* text, int x, int y,
                        int w, int h, gui_cb cb, void* user,
                        const GuiStyle* st);

gui_ctrl gui_add_entry(gui_win win, const char* placeholder,
                       int x, int y, int w, int h, const GuiStyle* st);
// Returns an internal buffer; copy it immediately (not guaranteed stable).
// 返回内部缓冲；请立即拷贝（不保证长期稳定）。
const char* gui_entry_text(gui_ctrl ctrl);

gui_ctrl gui_add_checkbox(gui_win win, const char* text, int x, int y,
                           int checked, const GuiStyle* st);
int      gui_checkbox_checked(gui_ctrl ctrl);

gui_ctrl gui_add_slider(gui_win win, int x, int y, int w, int minv, int maxv,
                         int val, const GuiStyle* st);
int      gui_slider_value(gui_ctrl ctrl);

// items: array of n NUL-terminated strings (may be NULL for empty list).
// items：长度为 n 的 NUL 结尾字符串数组（空列表可传 NULL）。
gui_ctrl gui_add_listbox(gui_win win, const char* const* items, int n,
                         int x, int y, int w, int h, const GuiStyle* st);
int      gui_listbox_selection(gui_ctrl ctrl);   // -1 if none

gui_ctrl gui_add_textarea(gui_win win, const char* text, int x, int y,
                          int w, int h, const GuiStyle* st);
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
