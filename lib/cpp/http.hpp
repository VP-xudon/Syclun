// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/http.hpp
//
// Standard library: http (C++-backed backend).
// 标准库：http（C++ 底层实现）。
//
// A small, dependency-free HTTP/1.1 client over raw sockets. It covers the
// day-to-day needs of an `http` library (GET/POST/PUT/DELETE + a generic
// request builder, response status / headers / body, JSON-friendly strings)
// without pulling in libcurl or a TLS stack. HTTPS is intentionally out of
// scope: a correct TLS backend is a project of its own and would break the
// zero-third-party-dependency rule.
// 一个零依赖、基于裸套接字的 HTTP/1.1 客户端。它覆盖 http 库日常所需
// （GET/POST/PUT/DELETE + 通用请求构造器，响应含状态码/头/正文），不引入
// libcurl 或 TLS 栈。HTTPS 刻意不在范围内：正确的 TLS 后端本身就是大工程，
// 且会破坏“零第三方依赖”原则。
//
// Cross-platform: Winsock2 on Windows, <sys/socket.h> + getaddrinfo on
// Linux/macOS. The whole call is synchronous (one request = one connect/read).
// 跨平台：Windows 用 Winsock2，Linux/macOS 用 <sys/socket.h> + getaddrinfo。
// 整个调用是同步的（一次请求 = 一次连接/读取）。
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <tuple>
#include <set>
#include <fstream>

#include "net_common.hpp"          // shared socket helpers (rt_lib_net_detail)
#include "../../src/builtin.hpp"   // reuse the shared runtime + helper API

namespace rt_lib_http {

    using runtime::RuntimeObject;
    using runtime::RuntimeObjectPtr;
    using runtime::RuntimeClass;
    namespace rb = rt_builtin;
    using namespace rt_lib_net_detail;

    // ---- URL parsing / URL 解析 ----
    struct Url { std::string host, path; int port = 80; bool ok = false; };

    inline Url parse_url(const std::string& u) {
        Url r;
        r.port = 80;
        std::string s = u;
        auto pos = s.find("://");
        std::string scheme;
        if (pos != std::string::npos) {
            scheme = s.substr(0, pos);
            s = s.substr(pos + 3);
        }
        if (!scheme.empty() && scheme != "http" && scheme != "https")
            return r;                 // only http(s) supported / 仅支持 http(s)
        auto slash = s.find('/');
        std::string auth = (slash == std::string::npos) ? s : s.substr(0, slash);
        r.path = (slash == std::string::npos) ? "/" : s.substr(slash);
        auto at = auth.rfind('@');    // strip userinfo / 去掉 userinfo
        if (at != std::string::npos) auth = auth.substr(at + 1);
        int port = (scheme == "https") ? 443 : 80;
        std::string hostpart = auth;
        auto colon = hostpart.rfind(':');
        if (colon != std::string::npos) {
            std::string ps = hostpart.substr(colon + 1);
            hostpart = hostpart.substr(0, colon);
            try { port = (int)std::stoi(ps); } catch (...) { port = 80; }
        }
        if (!hostpart.empty() && hostpart.front() == '[' && hostpart.back() == ']')
            hostpart = hostpart.substr(1, hostpart.size() - 2);  // IPv6 brackets
        r.host = hostpart;
        r.port = port;
        if (r.path.empty()) r.path = "/";
        r.ok = !hostpart.empty();
        return r;
    }

    // ---- Dict helpers / 字典助手 ----
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

    // Set an arbitrary value (not just a string) into a Dict.
    // 把任意值（不限于字符串）写入 Dict。
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

    // Push a value onto an Array (mirrors Array.push semantics).
    // 把值压入 Array（与 Array.push 语义一致）。
    inline void array_push(RuntimeObjectPtr arr, RuntimeObjectPtr v) {
        auto* cls = dynamic_cast<RuntimeClass*>(arr.get());
        if (!cls || !v) return;
        auto& am = cls->get_attributes();
        std::size_t idx = rb::container_size(am);
        am[rb::elem_key(idx)] = v;
        rb::set_container_size(am, idx + 1);
    }

    inline void add_dict_headers(const RuntimeObjectPtr& dict, std::ostringstream& req) {
        auto* cls = dynamic_cast<RuntimeClass*>(dict.get());
        if (!cls) return;
        const auto& am = cls->get_attributes();
        for (const auto& kv : am) {
            if (kv.first.rfind(rb::DICT_VALPRE, 0) != 0) continue;
            std::string suffix = kv.first.substr(rb::DICT_VALPRE.size());
            auto kit = am.find(rb::DICT_KEYPRE + suffix);
            std::string kn = (kit != am.end() && rb::string_of(kit->second))
                                 ? *rb::string_of(kit->second) : "";
            std::string vv = rb::string_of(kv.second) ? *rb::string_of(kv.second) : "";
            if (!kn.empty()) req << kn << ": " << vv << "\r\n";
        }
    }

    // Decode a chunked-transfer-encoded body.
    // 解码分块传输编码的正文。
    inline std::string decode_chunked(const std::string& b) {
        std::string out;
        size_t i = 0;
        while (i < b.size()) {
            size_t nl = b.find("\r\n", i);
            if (nl == std::string::npos) break;
            std::string sz = b.substr(i, nl - i);
            auto semi = sz.find(';');              // strip chunk extensions
            if (semi != std::string::npos) sz = sz.substr(0, semi);
            int len = 0;
            try { len = (int)std::stoul(sz, nullptr, 16); } catch (...) { break; }
            if (len == 0) break;
            i = nl + 2;
            if (i + (size_t)len > b.size()) break;
            out.append(b.substr(i, (size_t)len));
            i += (size_t)len + 2;                 // skip trailing CRLF
        }
        return out;
    }

    // Lower-case a header name for case-insensitive comparison.
    // 将头名转小写，便于大小写无关比较。
    inline std::string to_lower(std::string s) {
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    // Parse a raw HTTP response into (status, reason, headers, body).
    // 把原始 HTTP 响应解析为 (状态码, 原因短语, 头字典, 正文)。
    inline std::tuple<int, std::string, RuntimeObjectPtr, std::string>
    parse_response(const std::string& raw) {
        std::string head, body;
        auto sep = raw.find("\r\n\r\n");
        if (sep == std::string::npos) head = raw;
        else { head = raw.substr(0, sep); body = raw.substr(sep + 4); }

        int status = 0;
        std::string reason;
        auto hdrs = rb::make_dict();

        size_t lineStart = 0;
        bool first = true;
        std::string clen;
        bool chunked = false;
        while (true) {
            size_t nl = head.find("\r\n", lineStart);
            std::string line = (nl == std::string::npos)
                                   ? head.substr(lineStart)
                                   : head.substr(lineStart, nl - lineStart);
            if (first) {
                // HTTP/1.1 200 OK
                auto sp1 = line.find(' ');
                auto sp2 = line.find(' ', sp1 == std::string::npos ? line.size() : sp1 + 1);
                if (sp1 != std::string::npos) {
                    try { status = std::stoi(line.substr(sp1 + 1, sp2 - (sp1 + 1))); } catch (...) {}
                    if (sp2 != std::string::npos) reason = line.substr(sp2 + 1);
                }
                first = false;
            } else if (!line.empty()) {
                auto colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string k = line.substr(0, colon);
                    std::string v = line.substr(colon + 1);
                    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(0, 1);
                    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
                    dict_set_str(hdrs, k, v);
                    std::string lk = to_lower(k);
                    if (lk == "content-length") clen = v;
                    else if (lk == "transfer-encoding" && to_lower(v).find("chunked") != std::string::npos)
                        chunked = true;
                }
            }
            if (nl == std::string::npos) break;
            lineStart = nl + 2;
        }

        if (chunked) body = decode_chunked(body);
        else if (!clen.empty()) {
            try {
                size_t want = (size_t)std::stoull(clen);
                if (body.size() > want) body = body.substr(0, want);
            } catch (...) {}
        }
        return { status, reason, hdrs, body };
    }

    // Core request driver. Returns an InstanceListPtr holding either a
    // Response object or an error capsule.
    // 核心请求驱动。返回含 Response 对象或错误胶囊的 InstanceListPtr。
    inline rt_basic::InstanceListPtr do_request(
        const std::string& method, const std::string& url,
        const RuntimeObjectPtr& headersDict, const std::string& body) {
        Url u = parse_url(url);
        if (!u.ok)
            return rb::list_of({rb::native_error("http: invalid or unsupported URL: " + url)});
        sock_t s = net_connect(u.host, u.port);
        if (s == SOCK_BAD)
            return rb::list_of({rb::native_error(
                "http: cannot connect to " + u.host + ":" + std::to_string(u.port))});

        std::ostringstream req;
        req << method << " " << u.path << " HTTP/1.1\r\n";
        req << "Host: " << u.host << "\r\n";
        req << "User-Agent: Syclun-http/1.0\r\n";
        req << "Accept: */*\r\n";
        if (headersDict) add_dict_headers(headersDict, req);
        if (!body.empty()) req << "Content-Length: " << body.size() << "\r\n";
        req << "Connection: close\r\n\r\n";
        if (!body.empty()) req << body;

        std::string reqs = req.str();
        int sent = sock_send(s, reqs.c_str(), (int)reqs.size());
        if (sent < 0) {
            sock_close(s);
            return rb::list_of({rb::native_error("http: failed to send request")});
        }
        std::string raw = sock_read_all(s);
        sock_close(s);

        auto [status, reason, hdrs, bodyout] = parse_response(raw);
        auto r = ::stdRT.make("Response");
        if (auto* cls = dynamic_cast<RuntimeClass*>(r.get())) {
            auto& am = cls->get_attributes();
            am["#status"]  = rb::make_number((double)status, true);
            am["#reason"]  = rb::make_string(reason);
            am["#headers"] = hdrs;
            am["#body"]    = rb::make_string(bodyout);
        }
        return rb::list_of({r});
    }

    // ---- Response object / Response 对象（# 键避免遮蔽同名方法）----
    inline rt_basic::Callable method_response_status() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr) {
                auto v = rb::number_of(env["#status"]);
                return rb::list_of({rb::make_number(v ? *v : 0.0, true)});
            },
            rb::make_sign("status", {}, {{"out", "std::Number"}}));
    }
    inline rt_basic::Callable method_response_reason() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr) {
                auto v = rb::string_of(env["#reason"]);
                return rb::list_of({rb::make_string(v ? *v : "")});
            },
            rb::make_sign("reason", {}, {{"out", "std::String"}}));
    }
    inline rt_basic::Callable method_response_headers() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr) {
                auto v = env.find("#headers");
                return rb::list_of({v != env.end() ? v->second : rb::make_dict()});
            },
            rb::make_sign("headers", {}, {{"out", "std::Dict"}}));
    }
    inline rt_basic::Callable method_response_body() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr) {
                auto v = rb::string_of(env["#body"]);
                return rb::list_of({rb::make_string(v ? *v : "")});
            },
            rb::make_sign("body", {}, {{"out", "std::String"}}));
    }
    inline rt_basic::Callable method_response_text() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr) {
                auto v = rb::string_of(env["#body"]);
                return rb::list_of({rb::make_string(v ? *v : "")});
            },
            rb::make_sign("text", {}, {{"out", "std::String"}}));
    }
    inline rt_basic::Callable method_response_ok() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr) {
                auto v = rb::number_of(env["#status"]);
                int st = v ? (int)*v : 0;
                return rb::list_of({rb::make_boolean(st >= 200 && st < 300)});
            },
            rb::make_sign("ok", {}, {{"out", "std::Boolean"}}));
    }

    // Build a fresh Response carrying the hidden # fields from `env`.
    // 用 env 中的隐藏 # 字段构造一个全新的 Response。
    inline RuntimeObjectPtr clone_response_from_env(rt_basic::InstanceMap& env) {
        auto r = ::stdRT.make("Response");
        auto* cls = dynamic_cast<RuntimeClass*>(r.get());
        if (!cls) return r;
        auto& am = cls->get_attributes();
        am["#status"]  = env.count("#status")  ? env["#status"]  : rb::make_number(0.0, true);
        am["#reason"]  = env.count("#reason")  ? env["#reason"]  : rb::make_string("");
        am["#headers"] = env.count("#headers") ? env["#headers"] : rb::make_dict();
        am["#body"]    = env.count("#body")    ? env["#body"]    : rb::make_string("");
        return r;
    }

    // Response publish: `-(http::Response r) << c.get(url)` needs the real
    // result to cross the flow boundary. Publish a clone of this Response (the
    // base Object `=:` would publish `#value`, which a Response has none of,
    // yielding an empty publish and a hollow `r`).
    // Response 公布：`-(http::Response r) << c.get(url)` 需要让真实结果跨越流边界。
    // 公布本 Response 的克隆（基类 Object 的 `=:` 会公布 `#value`，而 Response
    // 没有该胶囊，将导致空公布，赋值后 r 为空壳）。
    inline rt_basic::Callable method_response_publish() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr /*paras*/) {
                return rb::list_of({clone_response_from_env(env)});
            },
            rb::make_sign("=:", {}, {{"result", "http::Response"}}));
    }

    // Response receive: copy the hidden # fields from the incoming Response into
    // this variable's attribute map.
    // Response 接收：把传入 Response 的隐藏 # 字段拷入本变量的属性表。
    inline rt_basic::Callable method_response_receive() {
        return rb::native_method(
            [](rt_basic::InstanceMap& env, rt_basic::InstanceListPtr paras) {
                auto incoming = rb::para_at(paras, 0);
                if (!incoming || !paras || paras->size() != 1)
                    return rb::empty_result();
                auto* cls = dynamic_cast<RuntimeClass*>(incoming.get());
                if (!cls) return rb::empty_result();
                auto& senv = cls->get_attributes();
                const char* fields[] = {"#status", "#reason", "#headers", "#body"};
                for (const char* f : fields) {
                    auto it = senv.find(f);
                    if (it != senv.end()) env[f] = it->second;
                }
                return rb::empty_result();
            },
            rb::make_sign(":=", {{"value", "std::Object"}}, {}));
    }

    // ---- Client methods / Client 方法 ----
    inline rt_basic::Callable method_client_get() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto url = rb::string_of(rb::para_at(paras, 0));
                if (!url) return rb::list_of({rb::native_error("http.get requires a URL")});
                return do_request("GET", *url, nullptr, "");
            },
            rb::make_sign("get", {{"url", "std::String"}}, {{"out", "http::Response"}}));
    }
    inline rt_basic::Callable method_client_post() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto url = rb::string_of(rb::para_at(paras, 0));
                auto body = rb::string_of(rb::para_at(paras, 1));
                if (!url) return rb::list_of({rb::native_error("http.post requires a URL")});
                return do_request("POST", *url, nullptr, body ? *body : "");
            },
            rb::make_sign("post", {{"url", "std::String"}, {"body", "std::String"}},
                          {{"out", "http::Response"}}));
    }
    inline rt_basic::Callable method_client_put() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto url = rb::string_of(rb::para_at(paras, 0));
                auto body = rb::string_of(rb::para_at(paras, 1));
                if (!url) return rb::list_of({rb::native_error("http.put requires a URL")});
                return do_request("PUT", *url, nullptr, body ? *body : "");
            },
            rb::make_sign("put", {{"url", "std::String"}, {"body", "std::String"}},
                          {{"out", "http::Response"}}));
    }
    inline rt_basic::Callable method_client_delete() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto url = rb::string_of(rb::para_at(paras, 0));
                if (!url) return rb::list_of({rb::native_error("http.delete requires a URL")});
                return do_request("DELETE", *url, nullptr, "");
            },
            rb::make_sign("delete", {{"url", "std::String"}}, {{"out", "http::Response"}}));
    }
    inline rt_basic::Callable method_client_request() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto methodp = rb::string_of(rb::para_at(paras, 0));
                auto url = rb::string_of(rb::para_at(paras, 1));
                auto headers = rb::para_at(paras, 2);
                auto body = rb::string_of(rb::para_at(paras, 3));
                if (!methodp || !url)
                    return rb::list_of({rb::native_error("http.request requires (method, url)")});
                return do_request(*methodp, *url, headers, body ? *body : "");
            },
            rb::make_sign("request",
                {{"method", "std::String"}, {"url", "std::String"},
                 {"headers", "std::Dict"}, {"body", "std::String"}},
                {{"out", "http::Response"}}));
    }

    // ---- HTML helpers (no external parser) / HTML 助手（不依赖外部解析器）----
    inline std::string html_unescape(std::string s) {
        struct { const char* a; const char* b; } reps[] = {
            {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
            {"&apos;", "'"}, {"&#39;", "'"}, {"&nbsp;", " "}};
        for (auto& r : reps) {
            size_t p = 0;
            while ((p = s.find(r.a, p)) != std::string::npos)
                s.replace(p, std::strlen(r.a), r.b), p += std::strlen(r.b);
        }
        return s;
    }

    inline std::string extract_title(const std::string& html) {
        auto a = html.find("<title");
        if (a == std::string::npos) return "";
        auto gt = html.find('>', a);
        if (gt == std::string::npos) return "";
        auto b = html.find("</title>", gt);
        if (b == std::string::npos) return "";
        std::string t = html.substr(gt + 1, b - (gt + 1));
        return html_unescape(t);
    }

    // Collect href="..." link targets from raw HTML.
    // 从原始 HTML 收集 href="..." 链接目标。
    inline std::vector<std::string> extract_hrefs(const std::string& html) {
        std::vector<std::string> out;
        size_t i = 0;
        while ((i = html.find("href", i)) != std::string::npos) {
            i += 4;
            while (i < html.size() && (html[i] == ' ' || html[i] == '\t')) ++i;
            if (i < html.size() && html[i] == '=') {
                ++i;
                while (i < html.size() && (html[i] == ' ' || html[i] == '\t')) ++i;
                char q = html[i];
                if (q == '"' || q == '\'') {
                    ++i;
                    size_t end = html.find(q, i);
                    if (end != std::string::npos) {
                        out.push_back(html.substr(i, end - i));
                        i = end + 1;
                    }
                }
            }
        }
        return out;
    }

    // Resolve a possibly-relative link against a base URL.
    // 把可能相对的链接按基础 URL 解析为绝对 URL。
    inline std::string join_url(const std::string& base, const std::string& link) {
        if (link.empty()) return "";
        if (link.rfind("http://", 0) == 0 || link.rfind("https://", 0) == 0)
            return link;
        if (link.rfind("//", 0) == 0) {
            auto s = base.find("://");
            return (s == std::string::npos ? "http:" : base.substr(0, s + 1)) + link;
        }
        if (link.rfind("mailto:", 0) == 0 || link.rfind("javascript:", 0) == 0 ||
            link.rfind("#", 0) == 0 || link.rfind("data:", 0) == 0)
            return "";
        Url bu = parse_url(base);
        std::string scheme = (base.rfind("https://", 0) == 0) ? "https" : "http";
        // Preserve the port: a non-standard port (e.g. :8000) must survive link
        // resolution, or every relative / absolute-path link wrongly falls back
        // to the default port 80 / 443 and the crawl dies on connect.
        // 必须保留端口：非标准端口（如 :8000）在链接解析时不能丢，否则每个相对 /
        // 绝对路径链接都会回落到默认 80 / 443 端口，爬取连接失败。
        std::string origin = scheme + "://" + bu.host + ":" + std::to_string(bu.port);
        if (link.rfind("/", 0) == 0)
            return origin + link;
        // relative to the directory of base's path
        std::string path = bu.path;
        auto slash = path.rfind('/');
        std::string dir = (slash == std::string::npos) ? "/" : path.substr(0, slash + 1);
        return origin + dir + link;
    }

    // ---- Client additions: head / download ----
    inline rt_basic::Callable method_client_head() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto url = rb::string_of(rb::para_at(paras, 0));
                if (!url) return rb::list_of({rb::native_error("http.head requires a URL")});
                return do_request("HEAD", *url, nullptr, "");
            },
            rb::make_sign("head", {{"url", "std::String"}}, {{"out", "http::Response"}}));
    }
    inline rt_basic::Callable method_client_download() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto url = rb::string_of(rb::para_at(paras, 0));
                auto path = rb::string_of(rb::para_at(paras, 1));
                if (!url || !path)
                    return rb::list_of({rb::native_error("http.download requires (url, path)")});
                auto resp = do_request("GET", *url, nullptr, "");
                if (!resp || resp->empty()) return rb::list_of({rb::make_boolean(false)});
                auto r = resp->front();
                auto* cls = dynamic_cast<RuntimeClass*>(r.get());
                std::string body;
                if (cls) {
                    auto it = cls->get_attributes().find("#body");
                    if (it != cls->get_attributes().end()) {
                        auto s = rb::string_of(it->second);
                        if (s) body = *s;
                    }
                }
                std::ofstream f(*path, std::ios::binary);
                if (!f) return rb::list_of({rb::make_boolean(false)});
                f << body;
                return rb::list_of({rb::make_boolean(true)});
            },
            rb::make_sign("download", {{"url", "std::String"}, {"path", "std::String"}},
                          {{"out", "std::Boolean"}}));
    }

    // ---- Crawler / 网络爬虫 ----
    // http::Crawler.crawl(start, maxDepth, maxPages) -> Array<Dict{
    //   url, title, depth, links: Array<String> }>
    inline rt_basic::Callable method_crawler_crawl() {
        return rb::native_method(
            [](rt_basic::InstanceMap&, rt_basic::InstanceListPtr paras) {
                auto start = rb::string_of(rb::para_at(paras, 0));
                auto md = rb::number_of(rb::para_at(paras, 1));
                auto mp = rb::number_of(rb::para_at(paras, 2));
                if (!start)
                    return rb::list_of({rb::native_error("crawler.crawl requires a start URL")});
                int maxDepth = md ? (int)*md : 2;
                int maxPages = mp ? (int)*mp : 20;
                if (maxDepth < 0) maxDepth = 0;
                if (maxPages < 1) maxPages = 1;

                std::set<std::string> visited;
                std::vector<std::pair<std::string, int>> queue;
                queue.push_back({*start, 0});
                auto result = rb::make_array();

                while (!queue.empty() &&
                       (int)(*rb::number_of(
                           std::dynamic_pointer_cast<RuntimeClass>(result)
                               ->get_attributes().at(rb::SIZE_KEY))) < maxPages) {
                    auto [url, depth] = queue.front();
                    queue.erase(queue.begin());
                    if (visited.count(url)) continue;
                    visited.insert(url);
                    auto resp = do_request("GET", url, nullptr, "");
                    if (resp->empty()) continue;
                    auto r = resp->front();
                    auto* cls = dynamic_cast<RuntimeClass*>(r.get());
                    if (!cls) continue;
                    auto& am = cls->get_attributes();
                    auto st = rb::number_of(am["#status"]);
                    int status = st ? (int)*st : 0;
                    std::string body;
                    if (auto s = rb::string_of(am["#body"])) body = *s;
                    if (status < 200 || status >= 300) continue;

                    std::string title = extract_title(body);
                    auto links = rb::make_array();
                    std::vector<std::string> hrefs = extract_hrefs(body);
                    for (auto& h : hrefs) {
                        std::string abs = join_url(url, h);
                        if (abs.empty()) continue;
                        array_push(links, rb::make_string(abs));
                        if (depth + 1 <= maxDepth &&
                            (int)(*rb::number_of(
                                std::dynamic_pointer_cast<RuntimeClass>(result)
                                    ->get_attributes().at(rb::SIZE_KEY))) < maxPages)
                            queue.push_back({abs, depth + 1});
                    }

                    auto page = rb::make_dict();
                    dict_set_val(page, "url", rb::make_string(url));
                    dict_set_val(page, "title", rb::make_string(title));
                    dict_set_val(page, "depth", rb::make_string(std::to_string(depth)));
                    dict_set_val(page, "links", links);
                    array_push(result, page);
                }
                return rb::list_of({result});
            },
            rb::make_sign("crawl",
                {{"start", "std::String"}, {"maxDepth", "std::Number"}, {"maxPages", "std::Number"}},
                {{"out", "std::Array"}}));
    }

    inline void init_http_stdlib() {
        // Response prototype / Response 原型
        auto presp = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
        presp->set_method("status",  method_response_status());
        presp->set_method("reason",  method_response_reason());
        presp->set_method("headers", method_response_headers());
        presp->set_method("body",    method_response_body());
        presp->set_method("text",    method_response_text());
        presp->set_method("ok",      method_response_ok());
        presp->set_method("=:",      method_response_publish());
        presp->set_method(":=",      method_response_receive());
        runtime::Prototypes p;
        p.regcls("Response", presp);

        // Client prototype / Client 原型（无状态）
        auto pclient = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
        pclient->set_method("get",     method_client_get());
        pclient->set_method("post",    method_client_post());
        pclient->set_method("put",     method_client_put());
        pclient->set_method("delete",  method_client_delete());
        pclient->set_method("head",    method_client_head());
        pclient->set_method("download",method_client_download());
        pclient->set_method("request", method_client_request());
        p.regcls("Client", pclient);

        // Crawler prototype / Crawler 原型（无状态）
        auto pcrawl = std::make_shared<rt_basic::ClsProto>(::stdRT.getcls("Object"));
        pcrawl->set_method("crawl", method_crawler_crawl());
        p.regcls("Crawler", pcrawl);

        ::stdRT.add_protos(p);
    }

    inline bool _registered =
        (rt_builtin::register_native_lib("http", &init_http_stdlib), true);

} // namespace rt_lib_http
