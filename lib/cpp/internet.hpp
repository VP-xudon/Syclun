// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/internet.hpp
//
// Standard library: internet (C++-backed backend).
// 标准库：internet（C++ 底层实现）。
//
// Generalized network services: interface enumeration, DNS resolution,
// reachability and connect latency. This is the "broad networking" layer that
// sits above a single HTTP request — wifi / ethernet link *information* is
// provided here, while actively *managing* a wifi or cellular link (connect /
// disconnect / scan) is OS-specific and intentionally out of scope for L3
// (see docs). HTTP request handling lives in the `http` library.
// 广义网络服务：网络接口枚举、DNS 解析、可达性与连接时延。这是位于单个
// HTTP 请求之上的“广义网络”层——此处提供 wifi/以太网链路的*信息*，而主动
// *管理*（连接/断开/扫描）wifi 或蜂窝链路是操作系统相关的，L3 刻意不在范围
// （见文档）。HTTP 请求处理在 `http` 库。
//
// Cross-platform: Winsock2 + GetAdaptersAddresses on Windows; getaddrinfo +
// getifaddrs on Linux/macOS.
// 跨平台：Windows 用 Winsock2 + GetAdaptersAddresses；Linux/macOS 用
// getaddrinfo + getifaddrs。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <cstdio>
#include <set>
#include <sstream>

#include "net_common.hpp"          // shared socket helpers (rt_lib_net_detail)
#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

#ifdef _WIN32
#  include <iphlpapi.h>
#  pragma comment(lib, "Iphlpapi.lib")
#else
// POSIX (Linux / macOS / BSD): getifaddrs / freeifaddrs / struct ifaddrs are
// declared in <ifaddrs.h>, which is NOT pulled in transitively by the socket
// headers (net_common.hpp). Without it the interfaces / local_addresses methods
// fail to compile on every non-Windows platform ("getifaddrs/freeifaddrs was not
// declared", "struct ifaddrs is an incomplete type"), breaking the CI ubuntu +
// macos legs and the release verification gate.
#  include <ifaddrs.h>
#endif

namespace rt_lib_internet {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;
    using namespace rt_lib_net_detail;

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

    inline void dict_set_val(RuntimeObjectPtr dict,
                             const std::string& k, RuntimeObjectPtr v) {
        auto* cls = dynamic_cast<RuntimeClass*>(dict.get());
        if (!cls || !v) return;
        auto& am = cls->get_attributes();
        auto keyObj = rb::make_string(k);
        std::string slot = rb::DICT_VALPRE + rb::encode_key(keyObj);
        bool existed = am.count(slot) != 0;
        am[rb::DICT_KEYPRE + slot.substr(3)] = keyObj;
        am[slot] = v;
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

    // Run a shell command and capture its stdout (used for OS-level network
    // queries that have no portable C API, e.g. wifi scanning).
    // 运行 shell 命令并捕获其标准输出（用于没有可移植 C API 的 OS 级网络查询，
    // 如 Wi‑Fi 扫描）。
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

    // Non-blocking TCP connect probe with a timeout (ms). Returns true iff the
    // port accepted a connection within the timeout. Used by port scanning and
    // a practical "ping".
    // 带超时的非阻塞 TCP 连接探测。在超时内端口接受连接则返回真。用于端口扫描
    // 与实用的 “ping”。
    inline bool tcp_probe(const std::string& host, int port, int timeout_ms) {
        net_startup();
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0) return false;
        bool ok = false;
        for (auto* ai = res; ai; ai = ai->ai_next) {
            sock_t s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (s == SOCK_BAD) continue;
#ifdef _WIN32
            u_long m = 1; ioctlsocket(s, FIONBIO, &m);
#else
            fcntl(s, F_SETFL, O_NONBLOCK);
#endif
            // Bind the target port into the sockaddr (getaddrinfo left port 0).
            // 把目标端口写入 sockaddr（getaddrinfo 留的是端口 0）。
            if (ai->ai_family == AF_INET)
                ((sockaddr_in*)ai->ai_addr)->sin_port = htons((unsigned short)port);
            else if (ai->ai_family == AF_INET6)
                ((sockaddr_in6*)ai->ai_addr)->sin6_port = htons((unsigned short)port);
            int cr = ::connect(s, ai->ai_addr, (int)ai->ai_addrlen);
            if (cr == 0) { ok = true; sock_close(s); break; }
#ifdef _WIN32
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
            if (errno == EINPROGRESS) {
#endif
                fd_set wfds; FD_ZERO(&wfds);
#ifdef _WIN32
                FD_SET((unsigned)(uintptr_t)s, &wfds);
#else
                FD_SET(s, &wfds);
#endif
                struct timeval tv;
                tv.tv_sec = timeout_ms / 1000;
                tv.tv_usec = (timeout_ms % 1000) * 1000;
                int sel = select((int)(uintptr_t)s + 1, nullptr, &wfds, nullptr, &tv);
                if (sel > 0) {
                    int err = 0; socklen_t l = sizeof(err);
                    getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &l);
                    if (err == 0) ok = true;
                }
            }
            sock_close(s);
            if (ok) break;
        }
        if (res) freeaddrinfo(res);
        return ok;
    }

    inline RuntimeObjectPtr iface_dict(const std::string& name,
                                       const std::string& family,
                                       const std::string& addr,
                                       const std::string& netmask) {
        auto d = rb::make_dict();
        dict_set_str(d, "name", name);
        dict_set_str(d, "family", family);
        dict_set_str(d, "address", addr);
        dict_set_str(d, "netmask", netmask);
        return d;
    }

    inline bool is_loopback(const std::string& a) {
        return a == "127.0.0.1" || a == "::1" || a.rfind("127.", 0) == 0;
    }

#ifdef _WIN32
    inline std::string wstr_to_utf8(const wchar_t* w) {
        if (!w) return "";
        int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 0) return "";
        std::string out((size_t)n - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
        return out;
    }
#endif

    // Enumerate network interfaces, returning an Array of Dicts.
    // 枚举网络接口，返回 Dict 数组。
    inline rt_basic::InstanceListPtr do_interfaces() {
        std::vector<RuntimeObjectPtr> list;
#ifdef _WIN32
        ULONG buflen = 15000;
        std::vector<unsigned char> buf(buflen);
        PIP_ADAPTER_ADDRESSES adap = (PIP_ADAPTER_ADDRESSES)buf.data();
        if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adap, &buflen) == NO_ERROR) {
            for (auto* a = adap; a; a = a->Next) {
                std::string aname = wstr_to_utf8(a->FriendlyName);
                for (auto* ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
                    int f = ua->Address.lpSockaddr->sa_family;
                    char b[INET6_ADDRSTRLEN] = {0};
                    if (f == AF_INET)
                        inet_ntop(AF_INET,
                            &((sockaddr_in*)ua->Address.lpSockaddr)->sin_addr, b, sizeof(b));
                    else if (f == AF_INET6)
                        inet_ntop(AF_INET6,
                            &((sockaddr_in6*)ua->Address.lpSockaddr)->sin6_addr, b, sizeof(b));
                    else continue;
                    list.push_back(iface_dict(aname, f == AF_INET ? "inet" : "inet6", b, ""));
                }
            }
        }
#else
        struct ifaddrs* ifs = nullptr;
        if (getifaddrs(&ifs) == 0) {
            for (auto* i = ifs; i; i = i->ifa_next) {
                if (!i->ifa_addr) continue;
                int f = i->ifa_addr->sa_family;
                if (f != AF_INET && f != AF_INET6) continue;
                char b[INET6_ADDRSTRLEN] = {0};
                char nb[INET6_ADDRSTRLEN] = {0};
                if (f == AF_INET) {
                    inet_ntop(AF_INET, &((sockaddr_in*)i->ifa_addr)->sin_addr, b, sizeof(b));
                    if (i->ifa_netmask)
                        inet_ntop(AF_INET, &((sockaddr_in*)i->ifa_netmask)->sin_addr, nb, sizeof(nb));
                } else {
                    inet_ntop(AF_INET6, &((sockaddr_in6*)i->ifa_addr)->sin6_addr, b, sizeof(b));
                    if (i->ifa_netmask)
                        inet_ntop(AF_INET6, &((sockaddr_in6*)i->ifa_netmask)->sin6_addr, nb, sizeof(nb));
                }
                list.push_back(iface_dict(i->ifa_name, f == AF_INET ? "inet" : "inet6", b, nb));
            }
            freeifaddrs(ifs);
        }
#endif
        return rb::list_of({rb::make_array(list)});
    }

    inline rt_basic::Callable method_net_interfaces() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr) {
                return do_interfaces();
            },
            rb::make_sign("interfaces", {}, {{"out", "std::Array"}}));
    }

    // Resolve a host name to an Array of IP address strings.
    // 把主机名解析为 IP 字符串数组。
    inline rt_basic::Callable method_net_resolve() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto host = rb::string_of(rb::para_at(paras, 0));
                if (!host) return rb::list_of({rb::native_error("internet.resolve requires a host")});
                std::vector<RuntimeObjectPtr> addrs;
                if (net_startup()) {
                    addrinfo hints{};
                    hints.ai_family = AF_UNSPEC;
                    hints.ai_socktype = SOCK_STREAM;
                    addrinfo* res = nullptr;
                    if (getaddrinfo(host->c_str(), nullptr, &hints, &res) == 0) {
                        for (auto* ai = res; ai; ai = ai->ai_next) {
                            char b[INET6_ADDRSTRLEN] = {0};
                            if (ai->ai_family == AF_INET)
                                inet_ntop(AF_INET, &((sockaddr_in*)ai->ai_addr)->sin_addr, b, sizeof(b));
                            else if (ai->ai_family == AF_INET6)
                                inet_ntop(AF_INET6, &((sockaddr_in6*)ai->ai_addr)->sin6_addr, b, sizeof(b));
                            else continue;
                            addrs.push_back(rb::make_string(b));
                        }
                        freeaddrinfo(res);
                    }
                }
                return rb::list_of({rb::make_array(addrs)});
            },
            rb::make_sign("resolve", {{"host", "std::String"}}, {{"out", "std::Array"}}));
    }

    inline rt_basic::Callable method_net_reachable() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto host = rb::string_of(rb::para_at(paras, 0));
                auto port = rb::number_of(rb::para_at(paras, 1));
                if (!host || !port)
                    return rb::list_of({rb::native_error("internet.reachable requires (host, port)")});
                sock_t s = net_connect(*host, (int)*port);
                bool ok = (s != SOCK_BAD);
                if (ok) sock_close(s);
                return rb::list_of({rb::make_boolean(ok)});
            },
            rb::make_sign("reachable", {{"host", "std::String"}, {"port", "std::Number"}},
                          {{"out", "std::Boolean"}}));
    }

    inline rt_basic::Callable method_net_latency() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto host = rb::string_of(rb::para_at(paras, 0));
                auto port = rb::number_of(rb::para_at(paras, 1));
                if (!host || !port)
                    return rb::list_of({rb::native_error("internet.latency requires (host, port)")});
                auto t0 = std::chrono::steady_clock::now();
                sock_t s = net_connect(*host, (int)*port);
                auto t1 = std::chrono::steady_clock::now();
                if (s == SOCK_BAD) return rb::list_of({rb::make_number(-1.0, true)});
                sock_close(s);
                double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                return rb::list_of({rb::make_number(ms, false)});
            },
            rb::make_sign("latency", {{"host", "std::String"}, {"port", "std::Number"}},
                          {{"out", "std::Number"}}));
    }

    inline rt_basic::Callable method_net_local_addresses() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr) {
                auto raw = do_interfaces();
                std::vector<RuntimeObjectPtr> out;
                if (raw && !raw->empty()) {
                    auto arr = (*raw)[0];
                    if (auto* cls = dynamic_cast<RuntimeClass*>(arr.get())) {
                        for (const auto& kv : cls->get_attributes()) {
                            if (kv.first.rfind(rb::elem_key(0), 0) != 0) continue;
                            if (auto* d = dynamic_cast<RuntimeClass*>(kv.second.get())) {
                                auto it = d->get_attributes().find(
                                    rb::DICT_VALPRE + rb::encode_key(rb::make_string("address")));
                                if (it != d->get_attributes().end()) {
                                    auto a = rb::string_of(it->second);
                                    if (a && !is_loopback(*a)) out.push_back(rb::make_string(*a));
                                }
                            }
                        }
                    }
                }
                return rb::list_of({rb::make_array(out)});
            },
            rb::make_sign("local_addresses", {}, {{"out", "std::Array"}}));
    }

    inline rt_basic::Callable method_net_is_online() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr) {
                // Probe two well-known anycast DNS endpoints (TCP connect only;
                // no traffic sent). Either succeeding means we have a route.
                // 探测两个公认任播 DNS 端点（仅 TCP 连接，不发送流量）；
                // 任一成功即说明存在路由。
                bool online = false;
                for (const char* h : {"8.8.8.8", "1.1.1.1"}) {
                    sock_t s = net_connect(h, 53);
                    if (s != SOCK_BAD) { sock_close(s); online = true; break; }
                }
                return rb::list_of({rb::make_boolean(online)});
            },
            rb::make_sign("is_online", {}, {{"out", "std::Boolean"}}));
    }

    // Enumerate visible Wi-Fi networks. This needs OS facilities with no
    // portable C binding, so each platform's native tool is invoked and its
    // output parsed. Returns an Array of Dicts { ssid, signal, channel, security }.
    // 枚举可见的 Wi‑Fi 网络。这依赖于无可移植 C 绑定的 OS 设施，故调用各平台的
    // 原生工具并解析其输出。返回 Dict 数组 { ssid, signal, channel, security }。
    inline rt_basic::Callable method_net_wifi() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr) {
                std::vector<RuntimeObjectPtr> list;
                auto result = rb::make_array();
#if defined(_WIN32)
                std::string out = run_command("netsh wlan show networks mode=bssid");
                std::string curSsid, curSig, curChan, curSec;
                bool have = false;
                for (auto& line : split_lines(out)) {
                    size_t c = line.find(":");
                    if (c == std::string::npos) continue;
                    std::string key = line.substr(0, c);
                    std::string val = line.substr(c + 1);
                    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);
                    if (line.find("SSID ") == 0) {
                        if (have) {
                            auto d = rb::make_dict();
                            dict_set_str(d, "ssid", curSsid);
                            dict_set_str(d, "signal", curSig);
                            dict_set_str(d, "channel", curChan);
                            dict_set_str(d, "security", curSec);
                            array_push(result, d);
                        }
                        curSsid = val; curSig = curChan = curSec = ""; have = true;
                    } else if (line.find("Signal") == 0) curSig = val;
                    else if (line.find("Channel") == 0) curChan = val;
                    else if (line.find("Authentication") == 0) curSec = val;
                }
                if (have) {
                    auto d = rb::make_dict();
                    dict_set_str(d, "ssid", curSsid);
                    dict_set_str(d, "signal", curSig);
                    dict_set_str(d, "channel", curChan);
                    dict_set_str(d, "security", curSec);
                    array_push(result, d);
                }
#elif defined(__APPLE__)
                std::string out = run_command(
                    "/System/Library/PrivateFrameworks/Apple80211.framework/"
                    "Versions/Current/Resources/airport -s");
                for (auto& line : split_lines(out)) {
                    // airport -s: <ssid> <bssid> <rssi> <channel> ... <security>
                    if (line.empty() || line[0] == ' ') continue;
                    std::istringstream is(line);
                    std::string ssid, bssid, rssi, chan;
                    is >> ssid >> bssid >> rssi >> chan;
                    if (ssid.empty()) continue;
                    auto d = rb::make_dict();
                    dict_set_str(d, "ssid", ssid);
                    dict_set_str(d, "signal", rssi);
                    dict_set_str(d, "channel", chan);
                    array_push(result, d);
                }
#else
                std::string out = run_command("nmcli -t -f ssid,signal,chan,security dev wifi 2>/dev/null");
                if (out.empty())
                    out = run_command("iwlist scan 2>/dev/null | grep -E 'ESSID|Quality|Channel'");
                for (auto& line : split_lines(out)) {
                    if (line.empty()) continue;
                    auto d = rb::make_dict();
                    if (line.find(':') != std::string::npos && line.find("ESSID") == std::string::npos) {
                        // nmcli -t form: ssid:signal:chan:security
                        size_t p1 = 0, p2;
                        std::vector<std::string> f;
                        while ((p2 = line.find(':', p1)) != std::string::npos) {
                            f.push_back(line.substr(p1, p2 - p1)); p1 = p2 + 1;
                        }
                        f.push_back(line.substr(p1));
                        if (f.size() >= 4) {
                            dict_set_str(d, "ssid", f[0]);
                            dict_set_str(d, "signal", f[1]);
                            dict_set_str(d, "channel", f[2]);
                            dict_set_str(d, "security", f[3]);
                        }
                    } else {
                        // iwlist form: parse ESSID / Quality / Channel
                        std::string ssid, sig, chan;
                        auto e = line.find("ESSID:");
                        if (e != std::string::npos) ssid = line.substr(e + 7);
                        auto q = line.find("Quality=");
                        if (q != std::string::npos) sig = line.substr(q + 8);
                        auto c = line.find("Channel:");
                        if (c != std::string::npos) chan = line.substr(c + 8);
                        dict_set_str(d, "ssid", ssid);
                        dict_set_str(d, "signal", sig);
                        dict_set_str(d, "channel", chan);
                    }
                    array_push(result, d);
                }
#endif
                (void)list;
                return rb::list_of({result});
            },
            rb::make_sign("wifi_networks", {}, {{"out", "std::Array"}}));
    }

    // TCP port scan over [from, to]. Returns an Array of open port Numbers.
    // [from,to] 区间的 TCP 端口扫描，返回开放端口（Number）数组。
    inline rt_basic::Callable method_net_scan_ports() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto host = rb::string_of(rb::para_at(paras, 0));
                auto from = rb::number_of(rb::para_at(paras, 1));
                auto to = rb::number_of(rb::para_at(paras, 2));
                if (!host || !from || !to)
                    return rb::list_of({rb::native_error(
                        "internet.scan_ports requires (host, from, to)")});
                int a = (int)*from, b = (int)*to;
                if (a > b) std::swap(a, b);
                auto result = rb::make_array();
                for (int p = a; p <= b; ++p) {
                    if (tcp_probe(*host, p, 300)) array_push(result, rb::make_number((double)p, true));
                }
                return rb::list_of({result});
            },
            rb::make_sign("scan_ports",
                {{"host", "std::String"}, {"from", "std::Number"}, {"to", "std::Number"}},
                {{"out", "std::Array"}}));
    }

    inline void init_internet_stdlib() {
        auto p = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
        p->set_method("interfaces",      method_net_interfaces());
        p->set_method("resolve",         method_net_resolve());
        p->set_method("reachable",       method_net_reachable());
        p->set_method("latency",         method_net_latency());
        p->set_method("local_addresses", method_net_local_addresses());
        p->set_method("is_online",       method_net_is_online());
        p->set_method("wifi_networks",   method_net_wifi());
        p->set_method("scan_ports",      method_net_scan_ports());
        runtime::Prototypes protos;
        protos.regcls("Network", p);
        ::stdRT.add_protos(protos);
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("internet", &init_internet_stdlib), true);

} // namespace rt_lib_internet
