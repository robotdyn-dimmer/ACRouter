#pragma once
//
// HttpdServer — a thin Arduino-WebServer-compatible shim over ESP-IDF's
// esp_http_server. Motivation: the Arduino `WebServer` is single-connection and
// runs in the app loop, so concurrent browser connections wedge the (heap-tight)
// ESP32-C2 (measured: 2 concurrent reqs → one waits ~1s; 4 → full wedge).
// esp_http_server runs its OWN task that select()s over many sockets, serving each
// in turn — so parallel connections are juggled instead of wedging.
//
// Concurrency model: httpd uses ONE task; handlers run one-at-a-time to completion.
// So per-request state kept as members here is safe (never two handlers at once);
// the win is multi-socket juggling, not parallel handler execution.
//
// Only the subset actually used by WebServerManager is implemented:
//   on / onNotFound / begin / stop / handleClient(no-op) / collectHeaders(no-op)
//   arg("plain"=body | else query) / hasArg / header / hasHeader / uri
//   send(code[,type,body]) / sendHeader
// OTA's raw multipart upload is NOT shimmed — OTAManager registers its own native
// httpd handler via handle().
//
#include <Arduino.h>            // String
#include <functional>
#include <vector>
#include "esp_http_server.h"    // httpd_* + http_method (HTTP_GET / HTTP_POST / HTTP_OPTIONS)

class HttpdServer {
public:
    using Handler = std::function<void()>;

    explicit HttpdServer(uint16_t port = 80);
    ~HttpdServer();

    // --- Arduino WebServer-compatible registration/lifecycle ---
    void on(const char* uri, http_method method, Handler fn);
    void onNotFound(Handler fn) { _not_found = fn; }
    void collectHeaders(const char** /*names*/, size_t /*count*/) {}  // no-op: httpd exposes all headers
    bool begin();
    void stop();
    void handleClient() {}       // no-op: httpd runs its own task

    // --- Request accessors (valid only inside a handler) ---
    String uri();                        // path only (query stripped)
    String arg(const char* name);        // "plain" => raw body; else URL query param
    bool   hasArg(const char* name);
    String header(const char* name);
    bool   hasHeader(const char* name);

    // --- Response ---
    void send(int code, const char* contentType, const String& body);
    void send(int code, const char* contentType, const char* body) { send(code, contentType, String(body)); }
    void send(int code) { send(code, "text/plain", String()); }
    void sendHeader(const char* name, const String& value);

    httpd_handle_t handle() const { return _server; }        // escape hatch (OTAManager)
    httpd_req_t*   currentReq() const { return _req; }       // raw req inside a handler (OTA chunked recv)

private:
    struct Route  { String uri; http_method method; Handler fn; };
    struct Header { String name; String value; };

    static esp_err_t dispatchTrampoline(httpd_req_t* req);   // registered for GET/POST/OPTIONS "/*"
    void handleRequest(httpd_req_t* req);
    void resetPerRequest();
    void loadBody();
    static const char* statusText(int code);

    uint16_t       _port;
    httpd_handle_t _server = nullptr;
    std::vector<Route> _routes;
    Handler        _not_found;

    // Per-request state (single httpd task => one handler at a time => safe as members)
    httpd_req_t*   _req = nullptr;
    String         _path;              // uri without query
    String         _query;            // raw query string
    String         _body;             // request body (lazy)
    bool           _body_loaded = false;
    std::vector<Header> _out_headers; // queued via sendHeader(), flushed in send()
    bool           _sent = false;
};
