// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.
//
// gui_native_cocoa.mm - macOS/Cocoa backend for the `gui` library.
// gui_native_cocoa.mm —— `gui` 库的 macOS/Cocoa 后端（使用原生 AppKit 控件）。
//
// Compiled with -fobjc-arc. Each control wraps a native NSView and routes its
// action to the stored gui_cb via a small GuiTarget trampoline.
// 以 -fobjc-arc 编译。每个控件包一个原生 NSView，经由轻量 GuiTarget 跳板把动作
// 分派给保存的 gui_cb。

#include "gui_native.h"

#import <Cocoa/Cocoa.h>

#include <string>
#include <vector>
#include <algorithm>

// Opaque structs from gui_native.h, defined in global scope.
// gui_native.h 的不透明结构在全局作用域定义。
struct gui_ctrl_s {
    void*    view = nullptr;   // __strong NSView* (ARC)
    int      kind = 0;         // 1 button 2 checkbox 3 slider 4 listbox 5 entry 6 textarea 7 label
    gui_cb   cb = nullptr;
    void*    user = nullptr;
    std::string textbuf;
};

struct gui_win_s {
    void*    wnd = nullptr;    // __strong NSWindow*
    gui_cb   close_cb = nullptr;
    void*    close_user = nullptr;
    std::vector<gui_ctrl_s*> children;
    ~gui_win_s() { for (auto* c : children) delete c; }
};

std::vector<gui_win_s*> g_windows;
bool g_quit = false;

// Trampoline object: bridges an NSControl action back to a gui_cb.
@interface GuiTarget : NSObject
@property (nonatomic) gui_cb cb;
@property (nonatomic) void* user;
- (void)fire:(id)sender;
@end

@implementation GuiTarget
- (void)fire:(id)__unused sender {
    if (_cb) _cb(_user);
}
@end

extern "C" {

gui_win gui_window_create(const char* title, int w, int h) {
    if (NSApp == nil) {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    }
    auto* win = new gui_win_s();
    int cw = w > 0 ? w : 400, ch = h > 0 ? h : 300;
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, cw, ch)
                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                              NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                      backing:NSBackingStoreBuffered defer:NO];
    [window setTitle:(title ? [NSString stringWithUTF8String:title] : @"")];
    [window makeKeyAndOrderFront:nil];
    win->wnd = (__bridge_retained void*)window;
    g_windows.push_back(win);
    return win;
}

void gui_window_set_title(gui_win win, const char* title) {
    if (!win) return;
    NSWindow* window = (__bridge NSWindow*)win->wnd;
    [window setTitle:(title ? [NSString stringWithUTF8String:title] : @"")];
}

void gui_window_close(gui_win win) {
    if (!win) return;
    NSWindow* window = (__bridge NSWindow*)win->wnd;
    [window close];
    if (win->close_cb) win->close_cb(win->close_user);
    auto it = std::find(g_windows.begin(), g_windows.end(), win);
    if (it != g_windows.end()) g_windows.erase(it);
    CFRelease((__bridge CFTypeRef)window);
    delete win;
}

void gui_window_on_close(gui_win win, gui_cb cb, void* user) {
    if (win) { win->close_cb = cb; win->close_user = user; }
}

static gui_ctrl add_ctrl(gui_win win, int kind, void* view) {
    if (!win || !view) return nullptr;
    auto* c = new gui_ctrl_s();
    c->kind = kind; c->view = view;
    win->children.push_back(c);
    return c;
}

gui_ctrl gui_add_label(gui_win win, const char* text, int x, int y) {
    NSTextField* v = [[NSTextField alloc] initWithFrame:NSMakeRect(x, y, 240, 18)];
    [v setBezeled:NO]; [v setDrawsBackground:NO]; [v setEditable:NO]; [v setSelectable:NO];
    [v setStringValue:(text ? [NSString stringWithUTF8String:text] : @"")];
    NSWindow* w = (__bridge NSWindow*)win->wnd;
    [w.contentView addSubview:v];
    return add_ctrl(win, 7, (__bridge_retained void*)v);
}

gui_ctrl gui_add_button(gui_win win, const char* text, int x, int y, int w, int h, gui_cb cb, void* user) {
    NSButton* v = [[NSButton alloc] initWithFrame:NSMakeRect(x, y, w > 0 ? w : 100, h > 0 ? h : 28)];
    [v setTitle:(text ? [NSString stringWithUTF8String:text] : @"")];
    [v setBezelStyle:NSBezelStyleRounded];
    GuiTarget* t = [[GuiTarget alloc] init];
    t.cb = cb; t.user = user;
    v.target = t; v.action = @selector(fire:);
    NSWindow* wnd = (__bridge NSWindow*)win->wnd;
    [wnd.contentView addSubview:v];
    auto* c = add_ctrl(win, 1, (__bridge_retained void*)v);
    if (c) { c->cb = cb; c->user = user; }
    return c;
}

gui_ctrl gui_add_entry(gui_win win, const char* placeholder, int x, int y, int w, int h) {
    NSTextField* v = [[NSTextField alloc] initWithFrame:NSMakeRect(x, y, w > 0 ? w : 160, h > 0 ? h : 22)];
    [v setPlaceholderString:(placeholder ? [NSString stringWithUTF8String:placeholder] : @"")];
    NSWindow* wnd = (__bridge NSWindow*)win->wnd;
    [wnd.contentView addSubview:v];
    return add_ctrl(win, 5, (__bridge_retained void*)v);
}

const char* gui_entry_text(gui_ctrl ctrl) {
    if (!ctrl || ctrl->kind != 5) return "";
    NSTextField* v = (__bridge NSTextField*)ctrl->view;
    ctrl->textbuf = [[v stringValue] UTF8String];
    return ctrl->textbuf.c_str();
}

gui_ctrl gui_add_checkbox(gui_win win, const char* text, int x, int y, int checked) {
    NSButton* v = [[NSButton alloc] initWithFrame:NSMakeRect(x, y, 200, 18)];
    [v setButtonType:NSButtonTypeSwitch];
    [v setTitle:(text ? [NSString stringWithUTF8String:text] : @"")];
    [v setState:checked ? NSControlStateValueOn : NSControlStateValueOff];
    NSWindow* wnd = (__bridge NSWindow*)win->wnd;
    [wnd.contentView addSubview:v];
    return add_ctrl(win, 2, (__bridge_retained void*)v);
}

int gui_checkbox_checked(gui_ctrl ctrl) {
    if (!ctrl || ctrl->kind != 2) return 0;
    NSButton* v = (__bridge NSButton*)ctrl->view;
    return [v state] == NSControlStateValueOn ? 1 : 0;
}

gui_ctrl gui_add_slider(gui_win win, int x, int y, int w, int minv, int maxv, int val) {
    NSSlider* v = [[NSSlider alloc] initWithFrame:NSMakeRect(x, y, w > 0 ? w : 160, 20)];
    [v setMinValue:minv]; [v setMaxValue:maxv]; [v setDoubleValue:val];
    NSWindow* wnd = (__bridge NSWindow*)win->wnd;
    [wnd.contentView addSubview:v];
    return add_ctrl(win, 3, (__bridge_retained void*)v);
}

int gui_slider_value(gui_ctrl ctrl) {
    if (!ctrl || ctrl->kind != 3) return 0;
    NSSlider* v = (__bridge NSSlider*)ctrl->view;
    return (int)[v doubleValue];
}

gui_ctrl gui_add_listbox(gui_win win, const char* const* items, int n, int x, int y, int w, int h) {
    NSPopUpButton* v = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(x, y, w > 0 ? w : 160, h > 0 ? h : 26)
                                                  pullsDown:NO];
    for (int i = 0; i < n; ++i)
        [v addItemWithTitle:[NSString stringWithUTF8String:items && items[i] ? items[i] : ""]];
    NSWindow* wnd = (__bridge NSWindow*)win->wnd;
    [wnd.contentView addSubview:v];
    return add_ctrl(win, 4, (__bridge_retained void*)v);
}

int gui_listbox_selection(gui_ctrl ctrl) {
    if (!ctrl || ctrl->kind != 4) return -1;
    NSPopUpButton* v = (__bridge NSPopUpButton*)ctrl->view;
    return (int)[v indexOfSelectedItem];
}

gui_ctrl gui_add_textarea(gui_win win, const char* text, int x, int y, int w, int h) {
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(x, y, w > 0 ? w : 200, h > 0 ? h : 100)];
    [scroll setHasVerticalScroller:YES];
    NSTextView* v = [[NSTextView alloc] initWithFrame:[[scroll contentView] bounds]];
    [v setString:(text ? [NSString stringWithUTF8String:text] : @"")];
    [scroll setDocumentView:v];
    NSWindow* wnd = (__bridge NSWindow*)win->wnd;
    [wnd.contentView addSubview:scroll];
    return add_ctrl(win, 6, (__bridge_retained void*)scroll);
}

const char* gui_textarea_text(gui_ctrl ctrl) {
    if (!ctrl || ctrl->kind != 6) return "";
    NSScrollView* scroll = (__bridge NSScrollView*)ctrl->view;
    NSTextView* v = (NSTextView*)[scroll documentView];
    ctrl->textbuf = [[v string] UTF8String];
    return ctrl->textbuf.c_str();
}

int gui_show_message(gui_win parent, const char* title, const char* msg, int kind) {
    (void)parent;
    NSAlert* alert = [[NSAlert alloc] init];
    if (kind == 1) [alert setAlertStyle:NSAlertStyleWarning];
    else if (kind == 2) [alert setAlertStyle:NSAlertStyleCritical];
    else [alert setAlertStyle:NSAlertStyleInformational];
    [alert setMessageText:(title ? [NSString stringWithUTF8String:title] : @"")];
    [alert setInformativeText:(msg ? [NSString stringWithUTF8String:msg] : @"")];
    [alert runModal];
    return 0;
}

void gui_run(void) {
    g_quit = false;
    [NSApp run];
}

void gui_quit(void) {
    g_quit = true;
    [NSApp terminate:nil];
}

} // extern "C"
