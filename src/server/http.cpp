#include "server/http.hpp"

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace wb::http {

namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool send_all(SOCKET s, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int chunk = static_cast<int>(std::min<std::size_t>(data.size() - sent, 1 << 20));
        const int n = ::send(s, data.data() + sent, chunk, 0);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

Response plain_error(int status, const std::string& message) {
    Response r;
    r.status = status;
    r.content_type = "application/json";
    r.body = "{\"error\":\"" + message + "\"}";
    return r;
}

}  // namespace

std::string url_decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size() && hex_value(s[i + 1]) >= 0 && hex_value(s[i + 2]) >= 0) {
            out += static_cast<char>(hex_value(s[i + 1]) * 16 + hex_value(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::map<std::string, std::string> parse_query(std::string_view qs) {
    std::map<std::string, std::string> out;
    while (!qs.empty()) {
        const std::size_t amp = qs.find('&');
        const std::string_view pair = qs.substr(0, amp);
        if (!pair.empty()) {
            const std::size_t eq = pair.find('=');
            if (eq == std::string_view::npos) out[url_decode(pair)] = "";
            else out[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
        }
        if (amp == std::string_view::npos) break;
        qs.remove_prefix(amp + 1);
    }
    return out;
}

bool parse_head(std::string_view head, Request& out, std::string& error) {
    const std::size_t eol = head.find("\r\n");
    const std::string_view request_line = head.substr(0, eol);
    const std::size_t sp1 = request_line.find(' ');
    const std::size_t sp2 = request_line.rfind(' ');
    if (sp1 == std::string_view::npos || sp2 == sp1) {
        error = "malformed request line";
        return false;
    }
    out.method = std::string(request_line.substr(0, sp1));
    const std::string_view target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string_view version = request_line.substr(sp2 + 1);
    if (version.substr(0, 5) != "HTTP/") {
        error = "unsupported protocol";
        return false;
    }
    if (target.empty() || target.front() != '/') {
        error = "request target must start with '/'";
        return false;
    }
    const std::size_t q = target.find('?');
    out.path = url_decode(target.substr(0, q));
    if (q != std::string_view::npos) out.query = parse_query(target.substr(q + 1));

    std::size_t pos = (eol == std::string_view::npos) ? head.size() : eol + 2;
    while (pos < head.size()) {
        std::size_t end = head.find("\r\n", pos);
        if (end == std::string_view::npos) end = head.size();
        const std::string_view line = head.substr(pos, end - pos);
        pos = end + 2;
        if (line.empty()) continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            error = "malformed header line";
            return false;
        }
        out.headers[lower(std::string(trim(line.substr(0, colon))))] = std::string(trim(line.substr(colon + 1)));
    }
    return true;
}

std::string status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 202: return "Accepted";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 422: return "Unprocessable Entity";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "Status";
    }
}

std::string serialize(const Response& r) {
    std::string out = "HTTP/1.1 " + std::to_string(r.status) + " " + status_text(r.status) + "\r\n";
    out += "Content-Type: " + r.content_type + "\r\n";
    out += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\n";
    out += "X-Content-Type-Options: nosniff\r\n";
    out += "Connection: close\r\n";
    for (const auto& [k, v] : r.headers) out += k + ": " + v + "\r\n";
    out += "\r\n";
    out += r.body;
    return out;
}

Server::Server(Handler handler, ServerLimits limits)
    : handler_(std::move(handler)), limits_(limits), listen_sock_(INVALID_SOCKET) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) throw std::runtime_error("winsock initialisation failed");
}

Server::~Server() {
    stop();
    // wait briefly for in-flight requests so handlers never outlive their owner
    for (int i = 0; i < 200 && active_.load() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    WSACleanup();
}

int Server::listen(int port) {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) throw std::runtime_error("could not create a socket");
    BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof exclusive);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // never reachable from the network
    addr.sin_port = htons(static_cast<u_short>(port));
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == SOCKET_ERROR) {
        closesocket(s);
        throw std::runtime_error("port " + std::to_string(port) + " on 127.0.0.1 is already in use");
    }
    if (::listen(s, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(s);
        throw std::runtime_error("could not listen on 127.0.0.1");
    }
    int len = sizeof addr;
    getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
    listen_sock_ = s;
    return ntohs(addr.sin_port);
}

void Server::serve() {
    while (!stopping_.load()) {
        SOCKET c = ::accept(static_cast<SOCKET>(listen_sock_), nullptr, nullptr);
        if (c == INVALID_SOCKET) {
            if (stopping_.load()) break;
            continue;
        }
        if (active_.load() >= limits_.max_concurrent) {
            send_all(c, serialize(plain_error(503, "server busy, retry shortly")));
            closesocket(c);
            continue;
        }
        ++active_;
        std::thread([this, c] {
            handle_connection(static_cast<std::uintptr_t>(c));
            --active_;
        }).detach();
    }
}

void Server::stop() {
    if (stopping_.exchange(true)) return;
    if (listen_sock_ != INVALID_SOCKET) {
        closesocket(static_cast<SOCKET>(listen_sock_));  // unblocks accept()
        listen_sock_ = INVALID_SOCKET;
    }
}

void Server::handle_connection(std::uintptr_t raw) {
    const SOCKET s = static_cast<SOCKET>(raw);
    DWORD timeout_ms = 30000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof timeout_ms);

    std::string buf;
    char chunk[64 * 1024];
    std::size_t head_end = std::string::npos;
    while (head_end == std::string::npos) {
        const int n = ::recv(s, chunk, sizeof chunk, 0);
        if (n <= 0) { closesocket(s); return; }
        buf.append(chunk, static_cast<std::size_t>(n));
        head_end = buf.find("\r\n\r\n");
        if (head_end == std::string::npos && buf.size() > limits_.max_head_bytes) {
            send_all(s, serialize(plain_error(400, "request headers too large")));
            closesocket(s);
            return;
        }
    }

    Request req;
    std::string error;
    if (!parse_head(std::string_view(buf).substr(0, head_end), req, error)) {
        send_all(s, serialize(plain_error(400, error)));
        closesocket(s);
        return;
    }

    std::size_t content_length = 0;
    if (auto it = req.headers.find("content-length"); it != req.headers.end()) {
        try {
            content_length = static_cast<std::size_t>(std::stoull(it->second));
        } catch (...) {
            send_all(s, serialize(plain_error(400, "invalid content-length")));
            closesocket(s);
            return;
        }
    }
    if (content_length > limits_.max_body_bytes) {
        send_all(s, serialize(plain_error(413, "upload is larger than the 256 MB limit")));
        closesocket(s);
        return;
    }

    req.body = buf.substr(head_end + 4);
    req.body.reserve(content_length);
    while (req.body.size() < content_length) {
        const int n = ::recv(s, chunk, sizeof chunk, 0);
        if (n <= 0) { closesocket(s); return; }
        req.body.append(chunk, static_cast<std::size_t>(n));
    }
    req.body.resize(content_length);

    Response res;
    try {
        res = handler_(req);
    } catch (const std::exception& e) {
        res = plain_error(500, "internal error");
        (void)e;
    }
    send_all(s, serialize(res));
    shutdown(s, SD_SEND);
    closesocket(s);
}

}  // namespace wb::http
