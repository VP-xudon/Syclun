// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/cpp/net_common.hpp
//
// Shared, low-level socket helpers for the `http` and `internet` standard
// libraries. Kept in one place so the two headers do not redefine the same
// globals when both are included by std_libs.hpp in a single translation unit.
// http 与 internet 标准库共用的底层套接字助手。集中放置，避免两头文件经
// std_libs.hpp 同译时被重复定义。
//
// Cross-platform: Winsock2 on Windows; <sys/socket.h> + getaddrinfo elsewhere.
// 跨平台：Windows 用 Winsock2；其余平台用 <sys/socket.h> + getaddrinfo。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   using sock_t = SOCKET;
   static const sock_t SOCK_BAD = INVALID_SOCKET;
   inline int sock_close(sock_t s) { return closesocket(s); }
   inline int sock_recv(sock_t s, char* b, int n) { return recv(s, b, n, 0); }
   inline int sock_send(sock_t s, const char* b, int n) { return send(s, b, n, 0); }
#else
#  include <sys/socket.h>
#  include <netdb.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <signal.h>
#  include <fcntl.h>
   using sock_t = int;
   static const sock_t SOCK_BAD = -1;
   inline int sock_close(sock_t s) { return close(s); }
   inline int sock_recv(sock_t s, char* b, int n) { return (int)recv(s, b, (size_t)n, 0); }
   inline int sock_send(sock_t s, const char* b, int n) { return (int)send(s, b, (size_t)n, 0); }
#endif

#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_net_detail {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;

    // One-time per-process networking setup (WSAStartup / SIGPIPE ignore).
    // 一次性网络初始化（WSAStartup / 忽略 SIGPIPE）。
    inline bool net_startup() {
#ifdef _WIN32
        static bool done = false;
        if (done) return true;
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        done = true;
        return true;
#else
        signal(SIGPIPE, SIG_IGN);
        return true;
#endif
    }

    // Connect to host:port; return a connected socket or SOCK_BAD.
    // 连接到 host:port；返回已连接套接字或 SOCK_BAD。
    inline sock_t net_connect(const std::string& host, int port) {
        if (!net_startup()) return SOCK_BAD;
        std::string p = std::to_string(port);
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), p.c_str(), &hints, &res) != 0)
            return SOCK_BAD;
        sock_t s = SOCK_BAD;
        for (auto* ai = res; ai; ai = ai->ai_next) {
            s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (s == SOCK_BAD) continue;
            if (::connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
            sock_close(s);
            s = SOCK_BAD;
        }
        freeaddrinfo(res);
        return s;
    }

    // Read until the peer closes the connection.
    // 一直读到对端关闭连接。
    inline std::string sock_read_all(sock_t s) {
        std::string out;
        char buf[4096];
        while (true) {
            int n = sock_recv(s, buf, (int)sizeof(buf));
            if (n > 0) out.append(buf, (size_t)n);
            else break;
        }
        return out;
    }

} // namespace rt_lib_net_detail
