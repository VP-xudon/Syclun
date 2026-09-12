// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/bt.hpp
//
// Standard library: bt (C++-backed backend).
// 标准库：bt（C++ 底层实现）。
//
// Bluetooth device discovery, enumeration, connect / disconnect and radio
// state. There is no single portable C API for Bluetooth across Windows /
// macOS / Linux, so each platform's native tooling is invoked (via popen) and
// its output parsed. This gives real device listing and pairing control on
// Linux (bluez / bluetoothctl), best-effort discovery on macOS, and device
// enumeration on Windows. RFCOMM SPP byte-stream I/O is out of scope for L3
// (it is OS- and driver-specific and would need a dedicated transport layer).
// Bluetooth 设备的发现、枚举、连接 / 断开与无线电状态。跨 Windows/macOS/Linux
// 没有统一的 C API，故调用各平台原生工具（经 popen）并解析其输出。由此在 Linux
// （bluez / bluetoothctl）上得到真实的设备列表与配对控制，macOS 上尽力发现，
// Windows 上枚举设备。RFCOMM SPP 字节流 I/O 在 L3 之外（它依赖具体 OS 与驱动，
// 需专门的传输层）。
//
// Cross-platform: compiles everywhere; functionality depends on the presence of
// the OS Bluetooth tooling at runtime.
// 跨平台：处处可编译；功能取决于运行期是否具备相应 OS 蓝牙工具。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdio>
#include <sstream>

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_bt {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    inline void dict_set_str(RuntimeObjectPtr dict,
                             const std::string& k, const std::string& v) {
        auto* cls = dynamic_cast<RuntimeClass*>(dict.get());
        if (!cls) return;
        auto& am = cls->get_attributes();
        auto keyObj = rb::make_string(k);
        std::string slot = rb::DICT_VALPRE + rb::encode_key(keyObj);
        bool existed = am.count(slot) != 0;
        am[rb::DICT_KEYPRE + slot.substr(3)] = keyObj;
        am[slot] = rb::make_string(v);
        if (!existed) rb::set_container_size(am, rb::container_size(am) + 1);
    }

    inline void array_push(RuntimeObjectPtr arr, RuntimeObjectPtr v) {
        auto* cls = dynamic_cast<RuntimeClass*>(arr.get());
        if (!cls || !v) return;
        auto& am = cls->get_attributes();
        std::size_t idx = rb::container_size(am);
        am[rb::elem_key(idx)] = v;
        rb::set_container_size(am, idx + 1);
    }

    // Run a command and capture stdout (the portable bridge to OS tooling).
    // 运行命令并捕获标准输出（通往 OS 工具的可移植桥接）。
    inline std::string run_command(const std::string& cmd) {
        std::string out;
        FILE* f = popen(cmd.c_str(), "r");
        if (!f) return out;
        char buf[1024];
        while (fgets(buf, sizeof(buf), f)) out += buf;
        pclose(f);
        return out;
    }

    inline std::vector<std::string> split_lines(const std::string& s) {
        std::vector<std::string> lines;
        std::string cur;
        for (char c : s) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else if (c != '\r') cur += c;
        }
        if (!cur.empty()) lines.push_back(cur);
        return lines;
    }

    // Trim helpers. / 去空白助手。
    inline std::string trim(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
        while (b > a && (s[b-1] == ' ' || s[b-1] == '\t')) --b;
        return s.substr(a, b - a);
    }

    // Parse "Device AA:BB:.. Name" style lines into {address, name} dicts.
    // 把 “Device AA:BB:.. 名称” 式行解析为 {address, name} 字典。
    inline RuntimeObjectPtr parse_btctl_devices(const std::string& out) {
        auto arr = rb::make_array();
        for (auto& line : split_lines(out)) {
            size_t p = line.find("Device ");
            if (p == std::string::npos) continue;
            std::string rest = trim(line.substr(p + 7));
            size_t sp = rest.find(' ');
            std::string addr = (sp == std::string::npos) ? rest : rest.substr(0, sp);
            std::string name = (sp == std::string::npos) ? "" : trim(rest.substr(sp + 1));
            auto d = rb::make_dict();
            dict_set_str(d, "address", addr);
            dict_set_str(d, "name", name);
            dict_set_str(d, "status", "paired");
            array_push(arr, d);
        }
        return arr;
    }

    // List known / paired Bluetooth devices. / 列出已知 / 已配对蓝牙设备。
    inline rt_basic::Callable method_bt_devices() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr) {
#if defined(__linux__)
                return rb::list_of({parse_btctl_devices(run_command("bluetoothctl devices 2>/dev/null"))});
#elif defined(_WIN32)
                std::string out = run_command(
                    "powershell -NoProfile -Command "
                    "\"Get-PnpDevice -Class Bluetooth | Where-Object { $_.Status -eq 'OK' } | "
                    "Select-Object -ExpandProperty FriendlyName\"");
                auto arr = rb::make_array();
                for (auto& line : split_lines(out)) {
                    std::string name = trim(line);
                    if (name.empty()) continue;
                    auto d = rb::make_dict();
                    dict_set_str(d, "address", "(n/a)");
                    dict_set_str(d, "name", name);
                    dict_set_str(d, "status", "present");
                    array_push(arr, d);
                }
                return rb::list_of({arr});
#else
                std::string out = run_command("system_profiler SPBluetoothDataType 2>/dev/null");
                auto arr = rb::make_array();
                std::string curAddr, curName;
                for (auto& line : split_lines(out)) {
                    size_t a = line.find("Address:");
                    size_t n = line.find("Name:");
                    size_t d = line.find("Device:");
                    if (a != std::string::npos) curAddr = trim(line.substr(a + 8));
                    if (n != std::string::npos) curName = trim(line.substr(n + 5));
                    if (d != std::string::npos) {
                        auto dd = rb::make_dict();
                        dict_set_str(dd, "address", curAddr);
                        dict_set_str(dd, "name", curName);
                        dict_set_str(dd, "status", "paired");
                        array_push(arr, dd);
                    }
                }
                return rb::list_of({arr});
#endif
            },
            rb::make_sign("devices", {}, {{"out", "std::Array"}}));
    }

    // Scan for nearby devices for `timeout` seconds (best effort per platform).
    // 扫描附近设备 `timeout` 秒（各平台尽力而为）。
    inline rt_basic::Callable method_bt_scan() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto t = rb::number_of(rb::para_at(paras, 0));
                int secs = t ? (int)*t : 8;
#if defined(__linux__)
                return rb::list_of({parse_btctl_devices(
                    run_command("timeout " + std::to_string(secs) +
                                " bluetoothctl scan on 2>/dev/null"))});
#elif defined(__APPLE__)
                // blueutil is the common macOS scanner; fall back to empty.
                // blueutil 是 macOS 常用扫描器；否则返回空。
                std::string out = run_command(
                    "blueutil -s --timeout " + std::to_string(secs) + " 2>/dev/null");
                auto arr = rb::make_array();
                for (auto& line : split_lines(out)) {
                    std::string s = trim(line);
                    if (s.empty()) continue;
                    auto d = rb::make_dict();
                    dict_set_str(d, "address", s);
                    dict_set_str(d, "name", "");
                    dict_set_str(d, "status", "found");
                    array_push(arr, d);
                }
                return rb::list_of({arr});
#else
                // Windows discovery needs a dedicated WinRT scan; out of scope L3.
                // Windows 发现需专门 WinRT 扫描；L3 之外。
                return rb::list_of({rb::make_array()});
#endif
            },
            rb::make_sign("scan", {{"timeout", "std::Number"}}, {{"out", "std::Array"}}));
    }

    // Connect to a device by address. Best effort: works on Linux (bluez).
    // 按地址连接设备。尽力而为：Linux（bluez）可用。
    inline rt_basic::Callable method_bt_connect() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto addr = rb::string_of(rb::para_at(paras, 0));
                if (!addr) return rb::list_of({rb::make_boolean(false)});
#if defined(__linux__)
                std::string out = run_command(
                    "bluetoothctl connect " + *addr + " 2>/dev/null");
                bool ok = out.find("Connection successful") != std::string::npos ||
                          out.find("successful") != std::string::npos;
                return rb::list_of({rb::make_boolean(ok)});
#else
                // macOS / Windows: pairing control via OS tooling is out of L3 scope.
                // macOS / Windows：经 OS 工具配对控制在 L3 之外。
                return rb::list_of({rb::make_boolean(false)});
#endif
            },
            rb::make_sign("connect", {{"address", "std::String"}}, {{"out", "std::Boolean"}}));
    }

    // Disconnect a device by address (Linux / bluez). / 按地址断开（Linux / bluez）。
    inline rt_basic::Callable method_bt_disconnect() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto addr = rb::string_of(rb::para_at(paras, 0));
                if (!addr) return rb::list_of({rb::make_boolean(false)});
#if defined(__linux__)
                std::string out = run_command(
                    "bluetoothctl disconnect " + *addr + " 2>/dev/null");
                bool ok = out.find("Successful") != std::string::npos ||
                          out.find("successful") != std::string::npos;
                return rb::list_of({rb::make_boolean(ok)});
#else
                return rb::list_of({rb::make_boolean(false)});
#endif
            },
            rb::make_sign("disconnect", {{"address", "std::String"}}, {{"out", "std::Boolean"}}));
    }

    // Whether the Bluetooth radio is powered on. / 蓝牙无线电是否开启。
    inline rt_basic::Callable method_bt_enabled() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr) {
#if defined(__linux__)
                std::string out = run_command("bluetoothctl show 2>/dev/null");
                bool on = out.find("Powered: yes") != std::string::npos;
                return rb::list_of({rb::make_boolean(on)});
#elif defined(_WIN32)
                std::string out = run_command(
                    "powershell -NoProfile -Command "
                    "\"Get-PnpDevice -Class Bluetooth | Where-Object { $_.Status -eq 'OK' } | "
                    "Measure-Object | Select-Object -ExpandProperty Count\"");
                bool on = !trim(out).empty() && trim(out) != "0";
                return rb::list_of({rb::make_boolean(on)});
#else
                std::string out = run_command("system_profiler SPBluetoothDataType 2>/dev/null");
                bool on = out.find("Bluetooth") != std::string::npos &&
                          out.find("Off") == std::string::npos;
                return rb::list_of({rb::make_boolean(on)});
#endif
            },
            rb::make_sign("enabled", {}, {{"out", "std::Boolean"}}));
    }

    inline void init_bt_stdlib() {
        auto p = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
        p->set_method("devices",     method_bt_devices());
        p->set_method("scan",        method_bt_scan());
        p->set_method("connect",     method_bt_connect());
        p->set_method("disconnect",  method_bt_disconnect());
        p->set_method("enabled",     method_bt_enabled());
        runtime::Prototypes protos;
        protos.regcls("Bluetooth", p);
        ::stdRT.add_protos(protos);
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("bt", &init_bt_stdlib), true);

} // namespace rt_lib_bt
