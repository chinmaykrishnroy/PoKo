#pragma once
#include <functional>
#include <map>
#include <string>
#include <utility>
class String {
    std::string value;
public:
    String(const char* text = "") : value(text) {}
    String(std::string text) : value(std::move(text)) {}
    size_t length() const { return value.size(); }
    const char* c_str() const { return value.c_str(); }
    bool operator==(const String& other) const { return value == other.value; }
    bool operator!=(const String& other) const { return !(*this == other); }
    bool equalsConstantTime(const String& other) const { return *this == other; }
    int indexOf(const char* text) const { auto p=value.find(text); return p==std::string::npos ? -1 : static_cast<int>(p); }
    friend String operator+(const char* left, const String& right) { return String(std::string(left) + right.value); }
    friend String operator+(const String& left, const char* right) { return String(left.value + right); }
};
constexpr int HTTP_GET = 0, HTTP_POST = 1, DIGEST_AUTH = 2;
inline unsigned long millis() { return 100; }
namespace Middleware { using Callback = std::function<bool()>; }
class WebServer {
public:
    String path = "/", supplied, host = "192.168.0.3";
    int verb = HTTP_GET, status = 0;
    std::map<std::string, String> arguments, headers, responseHeaders;
    std::map<std::string, std::function<void()>> routes;
    std::function<bool(WebServer&, Middleware::Callback)> middleware;
    bool authenticate(const char*, const char* password) { return supplied == String(password); }
    void requestAuthentication(int, const char*) { status = 401; }
    String uri() { return path; }
    int method() { return verb; }
    String header(const char* key) { return headers[key]; }
    String hostHeader() { return host; }
    String arg(const char* key) { return arguments[key]; }
    void collectHeaders(const char**, size_t) {}
    void addMiddleware(decltype(middleware) callback) { middleware = callback; }
    void sendHeader(const char* key, const char* value) { responseHeaders[key] = value; }
    void send(int code, const char*, const char*) { status = code; }
    void on(const char* route, int, std::function<void()> callback) { routes[route] = callback; }
    void dispatch() { middleware(*this, [&]() { routes[path.c_str()](); return true; }); }
};
