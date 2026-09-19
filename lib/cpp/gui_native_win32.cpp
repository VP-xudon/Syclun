// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.
//
// gui_native_win32.cpp - Win32/User32 backend for the `gui` library (D9 themed).
// gui_native_win32.cpp —— `gui` 库的 Win32/User32 后端（D9 主题化）。

#include "gui_native.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <algorithm>

// Dwmapi attribute constants that older MinGW headers may not declare.
// 较旧 MinGW 头文件可能未声明这些 Dwmapi 属性常量。
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#  define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#  define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#  define DWMWCP_ROUND 2
#endif

// Define the opaque structs declared forward in gui_native.h, in global scope.
// 在全局作用域定义 gui_native.h 中前向声明的同名不透明结构。
struct gui_ctrl_s {
    HWND     hwnd = nullptr;
    int      kind = 0;        // 1 button 2 checkbox 3 slider 4 listbox 5 entry 6 textarea 7 label
    gui_cb   cb   = nullptr;
    void*    user = nullptr;
    std::string textbuf;      // cached GetWindowText buffer

    // Resolved visual style for this control. / 本控件解析后的视觉样式。
    std::string font_family;
    int      font_size = 11;
    int      corner    = 0;
    GuiColor bg   = {240,240,240,255};
    GuiColor fg   = {0,0,0,255};
    GuiColor accent = {0,120,215,255};
    HFONT    font = nullptr;
    HBRUSH   bgbrush = nullptr;

    // Button interaction state (owner-drawn). / 按钮交互状态（自绘）。
    WNDPROC  oldproc = nullptr;
    int      hover   = 0;
    int      pressed = 0;
};

struct gui_win_s {
    HWND            hwnd = nullptr;
    gui_cb          close_cb = nullptr;
    void*           close_user = nullptr;
    std::vector<gui_ctrl_s*> children;
    GuiColor        bg = {240,240,240,255};
    int             dark = 0;
    HBRUSH          bgbrush = nullptr;
    ~gui_win_s() {
        if (bgbrush) DeleteObject(bgbrush);
        for (auto* c : children) {
            if (c->font)   DeleteObject(c->font);
            if (c->bgbrush) DeleteObject(c->bgbrush);
            delete c;
        }
    }
};

namespace {

static const char* GUI_CLASS = "SyclunGUIClass";
static bool g_class_reg = false;
static bool g_quit = false;
static bool g_dwm_ok = true;
std::vector<gui_win_s*> g_windows;

// ---- global theme / 全局主题 ----
static GuiTheme g_theme = {
    "Segoe UI",   // font_family
    11,           // font_size
    8,            // corner_radius
    {240,240,240,255}, // bg
    {32,32,32,255},    // fg
    {0,120,215,255},   // accent
    0             // dark
};
static bool g_theme_set = false;

#define RGB_(c) RGB((c).r, (c).g, (c).b)

inline GuiColor blend(GuiColor a, GuiColor b, double t) {
    auto m = [](unsigned char x, unsigned char y, double k) -> unsigned char {
        return (unsigned char)(x + (int)(k * ((double)y - (double)x)));
    };
    return GuiColor{ m(a.r,b.r,t), m(a.g,b.g,t), m(a.b,b.b,t), 255 };
}

// Resolve an override style on top of the global theme.
// 在全局主题之上解析覆盖样式。
inline void resolve(const GuiStyle* st, std::string& fam, int& fsz, int& cor,
                    GuiColor& bg, GuiColor& fg, GuiColor& ac) {
    fam = (st && (st->flags & GUI_STYLE_FONT_FAMILY) && st->font_family && *st->font_family)
              ? st->font_family : (g_theme.font_family ? g_theme.font_family : "Segoe UI");
    fsz = (st && (st->flags & GUI_STYLE_FONT_SIZE) && st->font_size > 0)
              ? st->font_size : (g_theme.font_size > 0 ? g_theme.font_size : 11);
    cor = (st && (st->flags & GUI_STYLE_CORNER)) ? st->corner_radius : g_theme.corner_radius;
    bg  = (st && (st->flags & GUI_STYLE_BG))   ? st->bg    : g_theme.bg;
    fg  = (st && (st->flags & GUI_STYLE_FG))   ? st->fg    : g_theme.fg;
    ac  = (st && (st->flags & GUI_STYLE_ACCENT)) ? st->accent : g_theme.accent;
}

inline HFONT make_font(const std::string& fam, int size) {
    LOGFONTA lf{};
    lf.lfHeight = -(size > 0 ? size : 11);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    std::string f = fam.empty() ? "Segoe UI" : fam;
    strncpy(lf.lfFaceName, f.c_str(), LF_FACESIZE - 1);
    return CreateFontIndirectA(&lf);
}

inline void apply_dwm(gui_win w) {
    if (!g_dwm_ok || !w || !w->hwnd) return;
    // Dark mode / 暗色模式
    BOOL dark = w->dark ? TRUE : FALSE;
    DwmSetWindowAttribute(w->hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    // Rounded window corners / 圆角窗口
    int pref = g_theme.corner_radius > 0 ? DWMWCP_ROUND : 1 /*DWMWCP_DEFAULT*/;
    DwmSetWindowAttribute(w->hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

inline void paint_window_bg(gui_win w, HDC hdc, const RECT& r) {
    if (!w->bgbrush) w->bgbrush = CreateSolidBrush(RGB_(w->bg));
    FillRect(hdc, &r, w->bgbrush);
}

inline void draw_button(DRAWITEMSTRUCT* di, gui_ctrl c) {
    HDC hdc = di->hDC;
    RECT r = di->rcItem;
    GuiColor fill = c->pressed ? c->accent
                  : (c->hover ? blend(c->accent, c->bg, 0.25) : c->bg);
    HBRUSH br = CreateSolidBrush(RGB_(fill));
    HBRUSH obr = (HBRUSH)SelectObject(hdc, br);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB_(c->accent));
    HPEN open = (HPEN)SelectObject(hdc, pen);
    if (c->corner > 0)
        RoundRect(hdc, r.left, r.top, r.right, r.bottom, c->corner*2, c->corner*2);
    else
        Rectangle(hdc, r.left, r.top, r.right, r.bottom);
    SelectObject(hdc, open); DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB_(c->fg));
    char buf[512];
    int n = (int)GetWindowTextA(di->hwndItem, buf, 511);
    DrawTextA(hdc, buf, n, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, obr); DeleteObject(br);
}

static LRESULT CALLBACK btn_sub(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    gui_ctrl c = (gui_ctrl)GetWindowLongPtrA(w, GWLP_USERDATA);
    if (c) {
        if (m == WM_MOUSEMOVE) {
            if (!c->hover) { c->hover = 1; InvalidateRect(w, nullptr, FALSE); }
            TRACKMOUSEEVENT t; t.cbSize = sizeof(t); t.dwFlags = TME_LEAVE;
            t.hwndTrack = w; t.dwHoverTime = 0; TrackMouseEvent(&t);
        } else if (m == WM_MOUSELEAVE) {
            c->hover = 0; c->pressed = 0; InvalidateRect(w, nullptr, FALSE);
        } else if (m == WM_LBUTTONDOWN) {
            c->pressed = 1; InvalidateRect(w, nullptr, FALSE);
        } else if (m == WM_LBUTTONUP) {
            c->pressed = 0; InvalidateRect(w, nullptr, FALSE);
        }
    }
    WNDPROC old = c ? c->oldproc : nullptr;
    if (old) return CallWindowProcA(old, w, m, wp, lp);
    return DefWindowProcA(w, m, wp, lp);
}

LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    gui_win_s* self = nullptr;
    if (m == WM_CREATE) {
        self = (gui_win_s*)((CREATESTRUCT*)lp)->lpCreateParams;
        SetWindowLongPtrA(w, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (gui_win_s*)GetWindowLongPtrA(w, GWLP_USERDATA);
    }
    if (!self) return DefWindowProcA(w, m, wp, lp);

    if (m == WM_DRAWITEM) {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        if (di->CtlType == ODT_BUTTON) {
            gui_ctrl c = (gui_ctrl)GetWindowLongPtrA(di->hwndItem, GWLP_USERDATA);
            if (c) { draw_button(di, c); return TRUE; }
        }
        return DefWindowProcA(w, m, wp, lp);
    }
    if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLORLISTBOX ||
        m == WM_CTLCOLORDLG) {
        HWND ctl = (HWND)wp;
        gui_ctrl c = (gui_ctrl)GetWindowLongPtrA(ctl, GWLP_USERDATA);
        HDC hdc = (HDC)lp;
        if (c && (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLORLISTBOX)) {
            SetTextColor(hdc, RGB_(c->fg));
            SetBkColor(hdc, RGB_(c->bg));
            if (!c->bgbrush) c->bgbrush = CreateSolidBrush(RGB_(c->bg));
            return (LRESULT)c->bgbrush;
        }
        if (m == WM_CTLCOLORDLG) {
            SetTextColor(hdc, RGB_(self->bg));
            return (LRESULT)self->bgbrush;
        }
    }
    if (m == WM_CTLCOLOREDIT) {
        HWND ctl = (HWND)wp;
        gui_ctrl c = (gui_ctrl)GetWindowLongPtrA(ctl, GWLP_USERDATA);
        HDC hdc = (HDC)lp;
        if (c) {
            SetTextColor(hdc, RGB_(c->fg));
            SetBkColor(hdc, RGB_(c->bg));
            if (!c->bgbrush) c->bgbrush = CreateSolidBrush(RGB_(c->bg));
            return (LRESULT)c->bgbrush;
        }
    }
    if (m == WM_CTLCOLORBTN) {
        // Only used for the checkbox (not owner-drawn); color its label.
        // 仅用于复选框（非自绘）；为其标签着色。
        HWND ctl = (HWND)wp;
        gui_ctrl c = (gui_ctrl)GetWindowLongPtrA(ctl, GWLP_USERDATA);
        HDC hdc = (HDC)lp;
        if (c) {
            SetTextColor(hdc, RGB_(c->fg));
            SetBkColor(hdc, RGB_(c->bg));
            if (!c->bgbrush) c->bgbrush = CreateSolidBrush(RGB_(c->bg));
            return (LRESULT)c->bgbrush;
        }
    }
    if (m == WM_ERASEBKGND) {
        RECT r; GetClientRect(w, &r);
        paint_window_bg(self, (HDC)wp, r);
        return 1;
    }
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
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); // replaced by WM_ERASEBKGND
    RegisterClassA(&wc);
    g_class_reg = true;
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);
}

// Apply a resolved style to a freshly created control.
// 把解析后的样式应用到新建控件。
inline void apply_style(gui_ctrl c) {
    if (c->hwnd) {
        if (!c->font) c->font = make_font(c->font_family, c->font_size);
        if (c->font) SendMessageA(c->hwnd, WM_SETFONT, (WPARAM)c->font, TRUE);
    }
}

} // namespace

extern "C" {

void gui_set_theme(const GuiTheme* theme) {
    if (!theme) {
        g_theme = GuiTheme{"Segoe UI", 11, 8,
                           {240,240,240,255}, {32,32,32,255}, {0,120,215,255}, 0};
        g_theme_set = false;
        return;
    }
    g_theme = *theme;
    g_theme_set = true;
}

void gui_window_apply_theme(gui_win win, const GuiTheme* theme) {
    if (!win) return;
    if (theme) {
        win->bg = theme->bg;
        win->dark = theme->dark;
    } else {
        win->bg = g_theme.bg;
        win->dark = g_theme.dark;
    }
    apply_dwm(win);
    if (win->hwnd) InvalidateRect(win->hwnd, nullptr, TRUE);
}

gui_win gui_window_create(const char* title, int w, int h) {
    ensure_class();
    auto* win = new gui_win_s();
    win->bg = g_theme.bg;
    win->dark = g_theme.dark;
    HWND hwnd = CreateWindowExA(0, GUI_CLASS, title ? title : "",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, w > 0 ? w : 400, h > 0 ? h : 300,
        nullptr, nullptr, GetModuleHandleA(nullptr), win);
    if (!hwnd) { delete win; return nullptr; }
    win->hwnd = hwnd;
    apply_dwm(win);
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

static gui_ctrl add_ctrl(gui_win win, int kind, HWND hwnd, const GuiStyle* st) {
    if (!win || !hwnd) return nullptr;
    auto* c = new gui_ctrl_s();
    c->hwnd = hwnd; c->kind = kind;
    resolve(st, c->font_family, c->font_size, c->corner, c->bg, c->fg, c->accent);
    SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)c);
    win->children.push_back(c);
    apply_style(c);
    return c;
}

gui_ctrl gui_add_label(gui_win win, const char* text, int x, int y,
                       const GuiStyle* st) {
    HWND c = CreateWindowExA(0, "static", text ? text : "",
        WS_CHILD | WS_VISIBLE, x, y, 240, 22, win->hwnd, nullptr,
        GetModuleHandleA(nullptr), nullptr);
    return add_ctrl(win, 7, c, st);
}

gui_ctrl gui_add_button(gui_win win, const char* text, int x, int y,
                        int w, int h, gui_cb cb, void* user,
                        const GuiStyle* st) {
    HWND c = CreateWindowExA(0, "button", text ? text : "",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, x, y, w > 0 ? w : 100, h > 0 ? h : 30,
        win->hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    auto* ctrl = add_ctrl(win, 1, c, st);
    if (ctrl) {
        ctrl->cb = cb; ctrl->user = user;
        ctrl->oldproc = (WNDPROC)SetWindowLongPtrA(c, GWLP_WNDPROC, (LONG_PTR)btn_sub);
    }
    return ctrl;
}

gui_ctrl gui_add_entry(gui_win win, const char* placeholder, int x, int y, int w, int h,
                       const GuiStyle* st) {
    HWND c = CreateWindowExA(WS_EX_CLIENTEDGE, "edit", placeholder ? placeholder : "",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y, w > 0 ? w : 160, h > 0 ? h : 24,
        win->hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    return add_ctrl(win, 5, c, st);
}

const char* gui_entry_text(gui_ctrl ctrl) {
    if (!ctrl || !ctrl->hwnd) return "";
    int n = GetWindowTextLengthA(ctrl->hwnd);
    ctrl->textbuf.resize((size_t)n + 1, '\0');
    GetWindowTextA(ctrl->hwnd, &ctrl->textbuf[0], n + 1);
    ctrl->textbuf.resize((size_t)n);
    return ctrl->textbuf.c_str();
}

gui_ctrl gui_add_checkbox(gui_win win, const char* text, int x, int y, int checked,
                          const GuiStyle* st) {
    HWND c = CreateWindowExA(0, "button", text ? text : "",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x, y, 200, 24,
        win->hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    auto* ctrl = add_ctrl(win, 2, c, st);
    if (ctrl) SendMessageA(c, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return ctrl;
}

int gui_checkbox_checked(gui_ctrl ctrl) {
    if (!ctrl || !ctrl->hwnd) return 0;
    return SendMessageA(ctrl->hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
}

gui_ctrl gui_add_slider(gui_win win, int x, int y, int w, int minv, int maxv, int val,
                        const GuiStyle* st) {
    HWND c = CreateWindowExA(0, "msctls_trackbar32", "",
        WS_CHILD | WS_VISIBLE | TBS_HORZ, x, y, w > 0 ? w : 160, 30,
        win->hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    SendMessageA(c, TBM_SETRANGE, TRUE, MAKELONG(minv, maxv));
    SendMessageA(c, TBM_SETPOS, TRUE, val);
    return add_ctrl(win, 3, c, st);
}

int gui_slider_value(gui_ctrl ctrl) {
    if (!ctrl || !ctrl->hwnd) return 0;
    return (int)SendMessageA(ctrl->hwnd, TBM_GETPOS, 0, 0);
}

gui_ctrl gui_add_listbox(gui_win win, const char* const* items, int n, int x, int y, int w, int h,
                         const GuiStyle* st) {
    HWND c = CreateWindowExA(WS_EX_CLIENTEDGE, "listbox", "",
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL, x, y, w > 0 ? w : 160, h > 0 ? h : 120,
        win->hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    for (int i = 0; i < n; ++i)
        SendMessageA(c, LB_ADDSTRING, 0, (LPARAM)(items ? items[i] : ""));
    return add_ctrl(win, 4, c, st);
}

int gui_listbox_selection(gui_ctrl ctrl) {
    if (!ctrl || !ctrl->hwnd) return -1;
    return (int)SendMessageA(ctrl->hwnd, LB_GETCURSEL, 0, 0);
}

gui_ctrl gui_add_textarea(gui_win win, const char* text, int x, int y, int w, int h,
                          const GuiStyle* st) {
    HWND c = CreateWindowExA(WS_EX_CLIENTEDGE, "edit", text ? text : "",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        x, y, w > 0 ? w : 200, h > 0 ? h : 100,
        win->hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    return add_ctrl(win, 6, c, st);
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
