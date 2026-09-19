// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/sysapi.hpp
//
// Standard library: sysapi (C++-backed backend).
// 标准库：sysapi（C++ 底层实现）。
//
// A tiny, extensible system-API dispatch layer: `sysapi::call(name, params)`
// looks `name` up in a registry and invokes the matching handler, passing a
// Dict of parameters and returning a Dict of results. Unknown API names and
// platform-incompatible implementations auto-report an error (the call never
// silently no-ops) — this is the intended extension point for future
// cross-platform libraries: register a handler and it becomes callable.
// 一个极简、可扩展的系统 API 派发层：`sysapi::call(name, params)` 在注册表中
// 查找 `name` 并调用对应处理器，传入参数字典、返回结果字典。未知的 API 名
// 以及不兼容当前平台的处理器会**自动报错**（调用绝不静默空转）——这正是
// 未来跨平台库的扩展点：注册一个处理器即可被调用。
//
// `sysapi::supports(name)` reports whether an API is registered (and therefore
// available on the current platform) without calling it.
// `sysapi::supports(name)` 在不上手调用的前提下报告某 API 是否已注册
//（即可在当前平台使用）。
//
// The C++ twin of lib/sysapi.synl. Self-registered under "sysapi".
// lib/sysapi.synl 的 C++ 孪生体，以 "sysapi" 自注册。
// ============================================================

#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <stdexcept>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <unistd.h>
#  include <sys/wait.h>
#endif

#include "../../src/builtin.hpp"

namespace rt_lib_sysapi {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // A handler takes the parsed parameter map and returns a result Dict.
    // 处理器接收已解析的参数字典，返回结果字典。
    using ParamMap = std::unordered_map<std::string, RuntimeObjectPtr>;
    using ApiHandler = std::function<RuntimeObjectPtr(const ParamMap&)>;

    static std::unordered_map<std::string, ApiHandler> g_apis;

    // ---- Dict helpers (string-keyed; mirrors builtin's #v:/#k: encoding) ----
    // Dict 辅助（以字符串为键；与 builtin 的 #v:/#k: 编码一致）。
    inline void dict_set(rt_basic::InstanceMap& env, const std::string& key,
                         RuntimeObjectPtr value) {
        env[rb::DICT_KEYPRE + key] = rb::make_string(key);
        env[rb::DICT_VALPRE + key] = value;
    }
    inline RuntimeObjectPtr make_result_dict(
        std::initializer_list<std::pair<std::string, RuntimeObjectPtr>> kv
    ) {
        auto d = rb::make_dict();
        auto* cls = dynamic_cast<RuntimeClass*>(d.get());
        if (cls) {
            auto& am = cls->get_attributes();
            for (auto& [k, v] : kv) dict_set(am, k, v);
            rb::set_container_size(am, kv.size());
        }
        return d;
    }
    inline std::string get_str(const ParamMap& p, const std::string& key,
                               const std::string& def = "") {
        auto it = p.find(key);
        if (it == p.end()) return def;
        auto s = rb::string_of(it->second);
        return s ? *s : def;
    }
    inline long long get_int(const ParamMap& p, const std::string& key,
                             long long def = 0) {
        auto it = p.find(key);
        if (it == p.end()) return def;
        auto n = rb::number_of(it->second);
        return n ? static_cast<long long>(*n) : def;
    }

    // ---- Platform shims / 平台垫片 ----
    // Open a URL in the default browser. Returns true on success.
    // 用默认浏览器打开 URL。成功返回 true。
    inline bool platform_open_url(const std::string& url) {
#if defined(_WIN32)
        std::wstring w = [url]() {
            if (url.empty()) return std::wstring();
            int n = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), (int)url.size(),
                                         nullptr, 0);
            std::wstring w(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, url.c_str(), (int)url.size(),
                                w.data(), n);
            return w;
        }();
        HINSTANCE r = ShellExecuteW(nullptr, L"open", w.c_str(), nullptr,
                                    nullptr, SW_SHOWNORMAL);
        return (intptr_t)r > 32;
#else
        pid_t pid = fork();
        if (pid == 0) {
#  if defined(__APPLE__)
            execlp("open", "open", url.c_str(), (char*)nullptr);
#  else
            execlp("xdg-open", "xdg-open", url.c_str(), (char*)nullptr);
#  endif
            _exit(127);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
    }

    inline bool platform_beep(int freq, int ms) {
#if defined(_WIN32)
        return Beep((DWORD)freq, (DWORD)ms) != 0;
#else
        // Terminal bell; best-effort on POSIX (true speaker beeps are rare).
        // POSIX 下尽力而为：终端响铃（真正的扬声器蜂鸣已罕见）。
        std::fputc('\a', stderr);
        std::fflush(stderr);
        return true;
#endif
    }

#if defined(_WIN32)
    inline bool platform_clipboard_set(const std::string& text) {
        if (!OpenClipboard(nullptr)) return false;
        EmptyClipboard();
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (!h) { CloseClipboard(); return false; }
        memcpy(GlobalLock(h), text.c_str(), text.size() + 1);
        GlobalUnlock(h);
        SetClipboardData(CF_TEXT, h);
        CloseClipboard();
        return true;
    }
    inline std::string platform_clipboard_get() {
        if (!OpenClipboard(nullptr)) return "";
        HANDLE h = GetClipboardData(CF_TEXT);
        std::string out;
        if (h) {
            char* p = static_cast<char*>(GlobalLock(h));
            if (p) { out = p; GlobalUnlock(h); }
        }
        CloseClipboard();
        return out;
    }
#else
    // Clipboard is not uniformly available without external tools; callers that
    // need it on POSIX should wire up xclip/pbcopy. We report incompatibility
    // instead of silently faking a result.
    // POSIX 下若未接入 xclip/pbcopy 则无统一可用实现；此处如实上报不兼容，
    // 而非静默伪造结果。
    inline bool platform_clipboard_set(const std::string&) {
        rb::native_error("sysapi: clipboard is not available on this platform "
                         "(requires xclip/pbcopy wiring)");
        return false; // unreachable / 不可达
    }
    inline std::string platform_clipboard_get() {
        rb::native_error("sysapi: clipboard is not available on this platform "
                         "(requires xclip/pbcopy wiring)");
        return ""; // unreachable / 不可达
    }
#endif

    // ---- API handlers / API 处理器 ----
    inline RuntimeObjectPtr api_open_url(const ParamMap& p) {
        std::string url = get_str(p, "url");
        bool ok = platform_open_url(url);
        return make_result_dict({
            {"ok", rb::make_boolean(ok)},
            {"url", rb::make_string(url)}
        });
    }
    inline RuntimeObjectPtr api_beep(const ParamMap& p) {
        int freq = (int)get_int(p, "freq", 440);
        int ms   = (int)get_int(p, "duration_ms", 200);
        bool ok = platform_beep(freq, ms);
        return make_result_dict({{"ok", rb::make_boolean(ok)}});
    }
    inline RuntimeObjectPtr api_sleep(const ParamMap& p) {
        long long ms = get_int(p, "ms", 0);
        if (ms < 0) ms = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return make_result_dict({{"ok", rb::make_boolean(true)}});
    }
    inline RuntimeObjectPtr api_clipboard_set(const ParamMap& p) {
        std::string text = get_str(p, "text");
        bool ok = platform_clipboard_set(text);
        return make_result_dict({{"ok", rb::make_boolean(ok)}});
    }
    inline RuntimeObjectPtr api_clipboard_get(const ParamMap&) {
        return make_result_dict({{"ok", rb::make_boolean(true)},
                                 {"text", rb::make_string(platform_clipboard_get())}});
    }

    // sysapi.call(name, params) ~> (result)
    // 未知 API 或不兼容平台自动报错。
    inline rt_basic::Callable method_call() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto name = rb::string_of(rb::para_at(paras, 0));
                if (!name)
                    return rb::list_of({rb::native_error(
                        "sysapi.call requires an API name string")});
                ParamMap params;
                auto paramsObj = rb::para_at(paras, 1);
                if (paramsObj) {
                    if (auto* src = rb::attributes_of(paramsObj)) {
                        for (auto& [k, v] : *src) {
                            if (k.size() > rb::DICT_VALPRE.size() &&
                                k.compare(0, rb::DICT_VALPRE.size(),
                                          rb::DICT_VALPRE) == 0) {
                                params[k.substr(rb::DICT_VALPRE.size())] = v;
                            }
                        }
                    }
                }
                auto it = g_apis.find(*name);
                if (it == g_apis.end()) {
                    return rb::list_of({rb::native_error(
                        "sysapi: unknown API '" + *name + "' "
                        "(use sysapi::supports to check availability)")});
                }
                // A platform-incompatible handler reports itself via rb::native_error.
                // 不兼容当前平台的处理器会经 rb::native_error 自行上报。
                try {
                    auto res = it->second(params);
                    return rb::list_of({res ? res : rb::make_dict()});
                } catch (const std::exception& e) {
                    return rb::list_of({rb::native_error(
                        std::string("sysapi '") + *name + "' failed: " + e.what())});
                }
            },
            rb::make_sign("call",
                {{"api", "std::String"}, {"params", "std::Dict"}},
                {{"result", "std::Dict"}})
        );
    }

    // sysapi.supports(name) ~> (ok)
    inline rt_basic::Callable method_supports() {
        return rb::native_method(
            [](rt_basic::InstanceMap& /*env*/, rt_basic::InstanceListPtr paras) {
                auto name = rb::string_of(rb::para_at(paras, 0));
                bool ok = name && g_apis.count(*name) > 0;
                return rb::list_of({rb::make_boolean(ok)});
            },
            rb::make_sign("supports", {{"api", "std::String"}},
                          {{"ok", "std::Boolean"}})
        );
    }

    inline void init_sysapi_stdlib() {
        // Register the built-in cross-platform APIs. New libraries add entries
        // here (or via a registration hook) to become callable through call().
        // 登记内建跨平台 API。新的跨平台库在此追加条目即可经 call() 被调用。
        g_apis["open_url"]      = api_open_url;
        g_apis["beep"]          = api_beep;
        g_apis["sleep"]         = api_sleep;
        g_apis["clipboard_set"] = api_clipboard_set;
        g_apis["clipboard_get"] = api_clipboard_get;

        auto proto = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
        proto->set_method("call",     method_call());
        proto->set_method("supports", method_supports());
        runtime::Prototypes p;
        p.regcls("SysAPI", proto);
        ::stdRT.add_protos(p);
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("sysapi", &init_sysapi_stdlib), true);

} // namespace rt_lib_sysapi
