#pragma once
// a deliberately small http/1.1 server for a single local user.
//
// it binds to 127.0.0.1 only, serves one request per connection
// (connection: close) and hands each request to a handler on its own thread,
// so a slow request never blocks the ui. the parsing helpers are free
// functions so they can be unit-tested without sockets.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wb::http {

struct Request {
    std::string method;
    std::string path;  // decoded, without the query string
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> headers;  // keys lower-cased
    std::string body;
};

struct Response {
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
};

std::string url_decode(std::string_view s);
std::map<std::string, std::string> parse_query(std::string_view qs);

// parses the request line and headers (everything before the blank line).
// returns false with `error` set when the head is malformed.
bool parse_head(std::string_view head, Request& out, std::string& error);

std::string status_text(int status);
std::string serialize(const Response& r);

struct ServerLimits {
    std::size_t max_head_bytes = 64 * 1024;
    std::size_t max_body_bytes = 260u * 1024u * 1024u;  // a little above the 256 MB file limit
    int max_concurrent = 32;
};

class Server {
public:
    using Handler = std::function<Response(const Request&)>;

    Server(Handler handler, ServerLimits limits = {});
    ~Server();

    // binds 127.0.0.1:port (0 = pick a free port) and returns the bound port.
    int listen(int port);
    // accepts connections until stop() is called.
    void serve();
    void stop();

private:
    void handle_connection(std::uintptr_t sock);

    Handler handler_;
    ServerLimits limits_;
    std::uintptr_t listen_sock_;
    std::atomic<bool> stopping_{false};
    std::atomic<int> active_{0};
};

}  // namespace wb::http
