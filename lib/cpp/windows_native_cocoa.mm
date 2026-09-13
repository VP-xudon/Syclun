// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.
//
// windows_native_cocoa.mm - macOS/Cocoa backend for the `windows` library.
// windows_native_cocoa.mm —— `windows` 库的 macOS/Cocoa 后端（类 pyglet 2D 画布）。
//
// Compiled with -fobjc-arc. A custom NSView drives the on_draw callback and a
// CGContext does the drawing. The context is flipped so (0,0) is the top-left
// corner, matching the Win32/X11 backends.
// 以 -fobjc-arc 编译。自定义 NSView 驱动 on_draw 回调，CGContext 负责绘制。上下文
// 经过翻转，使 (0,0) 位于左上角，与 Win32/X11 后端一致。

#include "windows_native.h"

#import <Cocoa/Cocoa.h>
#include <vector>
#include <string>
#include <algorithm>

// Define the opaque struct declared forward in windows_native.h, in global scope
// (the header's `win_canvas` is a pointer to this exact tag). See the Win32/X11
// backends for the same pattern.
// 在全局作用域定义 windows_native.h 中前向声明的同名不透明结构（头文件里的
// win_canvas 正是指向此精确标签的指针）。Win32/X11 后端同此。
// Drawing command record (global so win_canvas_s may hold a vector of it).
// 绘制命令记录（置于全局作用域，使 win_canvas_s 可持其数组）。
struct Cmd {
    int type = 0;       // 1 line 2 rect 3 text 4 circle 5 ellipse 6 polygon
    int a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0, fill = 0;
    std::string text;
    std::vector<int> poly;
};

struct win_canvas_s {
    void*         view = nullptr;          // NSView*, non-retained for drawing
    void*         ctx = nullptr;          // CGContextRef, valid only during on_draw
    bool          active = false;
    int           bg_r = 255, bg_g = 255, bg_b = 255;
    std::vector<Cmd> cmds;
    win_draw_cb   draw_cb = nullptr; void* draw_user = nullptr;
    win_key_cb    key_cb = nullptr;  void* key_user = nullptr;
    win_close_cb  close_cb = nullptr; void* close_user = nullptr;
};

namespace {

std::vector<win_canvas_s*> g_canvas;

void exec_cmd(CGContextRef ctx, const Cmd& cmd) {
    CGFloat rf = cmd.e / 255.0, gf = cmd.f / 255.0, bf = cmd.g / 255.0;
    switch (cmd.type) {
        case 1:
            CGContextSetRGBStrokeColor(ctx, rf, gf, bf, 1.0);
            CGContextSetLineWidth(ctx, 1.0);
            CGContextMoveToPoint(ctx, cmd.a, cmd.b);
            CGContextAddLineToPoint(ctx, cmd.c, cmd.d);
            CGContextStrokePath(ctx);
            break;
        case 2:
            if (cmd.fill) { CGContextSetRGBFillColor(ctx, rf, gf, bf, 1.0);
                CGContextFillRect(ctx, CGRectMake(cmd.a, cmd.b, cmd.c, cmd.d)); }
            else { CGContextSetRGBStrokeColor(ctx, rf, gf, bf, 1.0);
                CGContextSetLineWidth(ctx, 1.0);
                CGContextStrokeRect(ctx, CGRectMake(cmd.a, cmd.b, cmd.c, cmd.d)); }
            break;
        case 3: {
            CGContextSaveGState(ctx);
            CGContextTranslateCTM(ctx, cmd.a, cmd.b);
            CGContextScaleCTM(ctx, 1, -1);
            CGContextSetRGBFillColor(ctx, rf, gf, bf, 1.0);
            CGContextSelectFont(ctx, "Helvetica", 13, kCGEncodingMacRoman);
            CGContextSetTextDrawingMode(ctx, kCGTextFill);
            CGContextShowTextAtPoint(ctx, 0, 0, cmd.text.c_str(), (size_t)cmd.text.size());
            CGContextRestoreGState(ctx);
            break;
        }
        case 4: {
            CGRect r = CGRectMake(cmd.a - cmd.c, cmd.b - cmd.c, cmd.c * 2, cmd.c * 2);
            if (cmd.fill) { CGContextSetRGBFillColor(ctx, rf, gf, bf, 1.0); CGContextFillEllipseInRect(ctx, r); }
            else { CGContextSetRGBStrokeColor(ctx, rf, gf, bf, 1.0); CGContextSetLineWidth(ctx, 1.0); CGContextStrokeEllipseInRect(ctx, r); }
            break;
        }
        case 5: {
            CGRect r = CGRectMake(cmd.a, cmd.b, cmd.c, cmd.d);
            if (cmd.fill) { CGContextSetRGBFillColor(ctx, rf, gf, bf, 1.0); CGContextFillEllipseInRect(ctx, r); }
            else { CGContextSetRGBStrokeColor(ctx, rf, gf, bf, 1.0); CGContextSetLineWidth(ctx, 1.0); CGContextStrokeEllipseInRect(ctx, r); }
            break;
        }
        case 6:
            if (cmd.poly.size() < 6) break;
            CGContextBeginPath(ctx);
            CGContextMoveToPoint(ctx, cmd.poly[0], cmd.poly[1]);
            for (size_t i = 2; i + 1 < cmd.poly.size(); i += 2)
                CGContextAddLineToPoint(ctx, cmd.poly[i], cmd.poly[i + 1]);
            CGContextClosePath(ctx);
            if (cmd.fill) { CGContextSetRGBFillColor(ctx, rf, gf, bf, 1.0); CGContextFillPath(ctx); }
            else { CGContextSetRGBStrokeColor(ctx, rf, gf, bf, 1.0); CGContextSetLineWidth(ctx, 1.0); CGContextStrokePath(ctx); }
            break;
    }
}

void redraw(win_canvas_s* c) {
    NSView* view = (__bridge NSView*)c->view;
    CGContextRef ctx = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
    c->ctx = ctx; c->active = true;
    CGContextSaveGState(ctx);
    // Flip so (0,0) is top-left, matching Win32/X11.
    // 翻转使 (0,0) 为左上角，与 Win32/X11 一致。
    CGContextTranslateCTM(ctx, 0, view.bounds.size.height);
    CGContextScaleCTM(ctx, 1, -1);
    CGContextSetRGBFillColor(ctx, c->bg_r / 255.0, c->bg_g / 255.0, c->bg_b / 255.0, 1.0);
    CGContextFillRect(ctx, CGRectMake(0, 0, view.bounds.size.width, view.bounds.size.height));
    for (auto& cmd : c->cmds) exec_cmd(ctx, cmd);
    c->cmds.clear();
    if (c->draw_cb) c->draw_cb(c->draw_user);
    CGContextRestoreGState(ctx);
    c->active = false; c->ctx = nullptr;
}

void emit(win_canvas_s* c, const Cmd& cmd) {
    if (c->active && c->ctx) exec_cmd((CGContextRef)c->ctx, cmd);
    else c->cmds.push_back(cmd);
}

} // namespace

// Custom view that invokes the canvas redraw on each paint and forwards key
// presses to the registered key callback.
// 每次绘制时调用画布重绘的自定义视图，并把按键转发给注册的回调。
@interface CanvasView : NSView
@property (nonatomic) win_canvas_s* selfcpp;
@end
@implementation CanvasView
- (void)drawRect:(NSRect)__unused rect { if (_selfcpp) redraw(_selfcpp); }
- (void)keyDown:(NSEvent*)ev {
    if (_selfcpp && _selfcpp->key_cb) {
        NSString* s = [ev characters];
        if (s && s.length > 0) {
            std::string k([s UTF8String]);
            _selfcpp->key_cb(k.c_str(), _selfcpp->key_user);
        }
    }
}
@end

extern "C" {

win_canvas win_canvas_create(const char* title, int w, int h) {
    if (NSApp == nil) {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    }
    auto* c = new win_canvas_s();
    int cw = w > 0 ? w : 400, ch = h > 0 ? h : 300;
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, cw, ch)
                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                              NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                      backing:NSBackingStoreBuffered defer:NO];
    [window setTitle:(title ? [NSString stringWithUTF8String:title] : @"")];
    CanvasView* view = [[CanvasView alloc] initWithFrame:NSMakeRect(0, 0, cw, ch)];
    view.selfcpp = c;
    window.contentView = view;
    [window makeKeyAndOrderFront:nil];
    c->view = (__bridge_retained void*)view;
    g_canvas.push_back(c);
    return c;
}
void win_canvas_set_title(win_canvas cv, const char* title) {
    if (!cv) return;
    NSWindow* w = (NSWindow*)[(__bridge CanvasView*)cv->view window];
    [w setTitle:(title ? [NSString stringWithUTF8String:title] : @"")];
}
void win_canvas_on_draw(win_canvas cv, win_draw_cb cb, void* user) { if (cv) { cv->draw_cb = cb; cv->draw_user = user; } }
void win_canvas_on_key(win_canvas cv, win_key_cb cb, void* user)   { if (cv) { cv->key_cb = cb; cv->key_user = user; } }
void win_canvas_on_close(win_canvas cv, win_close_cb cb, void* user){ if (cv) { cv->close_cb = cb; cv->close_user = user; } }

void win_canvas_clear(win_canvas cv, int r, int g, int b) {
    if (!cv) return;
    cv->bg_r = r; cv->bg_g = g; cv->bg_b = b; cv->cmds.clear();
    if (cv->view) [(__bridge NSView*)cv->view setNeedsDisplay:YES];
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

void win_canvas_close(win_canvas cv) {
    if (!cv) return;
    NSWindow* w = (NSWindow*)[(__bridge CanvasView*)cv->view window];
    if (cv->close_cb) cv->close_cb(cv->close_user);
    auto it = std::find(g_canvas.begin(), g_canvas.end(), cv);
    if (it != g_canvas.end()) g_canvas.erase(it);
    [w close];
    CFRelease((CFTypeRef)cv->view);
    delete cv;
}
void win_canvas_run(void) { [NSApp run]; }
void win_canvas_quit(void) { [NSApp terminate:nil]; }

} // extern "C"
