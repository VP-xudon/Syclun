// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.
//
// windows_native.h - platform-neutral C bridge for the `windows` library.
// windows_native.h —— `windows` 库的平台无关 C 桥接层（类 pyglet 的 2D 画布）。
//
// Same structure as gui_native.h: three real implementations (Win32 / X11 /
// Cocoa) implement this ABI; only the matching one is compiled per platform.
// 与 gui_native.h 同构：三套真实实现（Win32 / X11 / Cocoa）实现本 ABI，按平台
// 只编译对应一套。

#ifndef SYNTH_WINDOWS_NATIVE_H
#define SYNTH_WINDOWS_NATIVE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct win_canvas_s* win_canvas;

typedef void (*win_draw_cb)(void* user);
typedef void (*win_key_cb)(const char* key, void* user);
typedef void (*win_close_cb)(void* user);

win_canvas win_canvas_create(const char* title, int w, int h);
void win_canvas_set_title(win_canvas cv, const char* title);
void win_canvas_on_draw(win_canvas cv, win_draw_cb cb, void* user);
void win_canvas_on_key(win_canvas cv, win_key_cb cb, void* user);
void win_canvas_on_close(win_canvas cv, win_close_cb cb, void* user);

// Drawing commands. They are executed immediately when inside on_draw (a live
// paint) and buffered otherwise, so a program that draws once without on_draw
// still renders. clear() empties the buffer and sets the background color.
// 绘制命令。处于 on_draw（一次活动绘制）内时立即执行，否则缓冲，使“无 on_draw
// 仅绘制一次”的程序也能渲染。clear() 清空缓冲并设定背景色。
void win_canvas_clear(win_canvas cv, int r, int g, int b);
void win_canvas_draw_line(win_canvas cv, int x1, int y1, int x2, int y2, int r, int g, int b);
void win_canvas_draw_rect(win_canvas cv, int x, int y, int w, int h, int r, int g, int b, int fill);
void win_canvas_draw_text(win_canvas cv, int x, int y, const char* text, int r, int g, int b);
void win_canvas_draw_circle(win_canvas cv, int cx, int cy, int radius, int r, int g, int b, int fill);
void win_canvas_draw_ellipse(win_canvas cv, int x, int y, int w, int h, int r, int g, int b, int fill);
// pts: flat array of n*2 integers (x0,y0,x1,y1,...). / 扁平数组，长度 n*2。
void win_canvas_draw_polygon(win_canvas cv, const int* pts, int n, int r, int g, int b, int fill);

void win_canvas_close(win_canvas cv);
void win_canvas_run(void);    // message loop until all canvases closed
void win_canvas_quit(void);

#ifdef __cplusplus
}
#endif

#endif // SYNTH_WINDOWS_NATIVE_H
