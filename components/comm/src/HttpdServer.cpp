#include "HttpdServer.h"
#include "esp_log.h"
#include "lwip/sockets.h"        // setsockopt + SO_KEEPALIVE / TCP_KEEPIDLE (dead-peer reaping)
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "httpd";

// Comms-plane core (see docs/18 §11 tiering plan): pin the httpd task to PRO_CPU
// (core 0) with WiFi on a dual-core target (ESP32), so it cannot preempt the control
// loop on APP_CPU. Single-core (C2): no affinity — isolation is by priority.
#if !CONFIG_FREERTOS_UNICORE
#define ACR_HTTPD_CORE  0
#else
#define ACR_HTTPD_CORE  tskNO_AFFINITY
#endif

// Called by httpd on every accepted socket: enable TCP keepalive so a peer that
// vanished at the OS level (F5 / tab close / Wi-Fi drop, no FIN) is detected and
// the half-open socket freed in ~10s instead of hanging the (RAM-limited) C2 slot
// pool for minutes. Aggressive params: idle 5s, then 2 probes 3s apart.
static esp_err_t httpd_sock_keepalive(httpd_handle_t /*hd*/, int fd) {
    int on = 1, idle = 5, intvl = 3, cnt = 2;
    setsockopt(fd, SOL_SOCKET,  SO_KEEPALIVE,  &on,    sizeof(on));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE,  &idle,  sizeof(idle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,   sizeof(cnt));
    return ESP_OK;
}

// Bodies/queries are small JSON — cap the buffered request body to bound heap use.
static const size_t MAX_BODY = 2048;

HttpdServer::HttpdServer(uint16_t port) : _port(port) {}

HttpdServer::~HttpdServer() { stop(); }

void HttpdServer::on(const char* uri, http_method method, Handler fn) {
    _routes.push_back(Route{ String(uri), method, fn });
}

bool HttpdServer::begin() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.core_id          = ACR_HTTPD_CORE;        // comms plane (core 0 on dual-core)
    cfg.server_port      = _port;
    cfg.ctrl_port        = _port + 100;          // internal control socket (distinct per instance)
    cfg.max_uri_handlers = 6;                    // we register only 3 wildcard dispatchers
    // Keep the pool near the C2's RAM-realistic socket count so lru_purge actually
    // engages (evicts the oldest/likely-dead socket for a fresh client) BEFORE the
    // board runs out of TCP memory. Combined with keepalive below, a browser F5 or
    // tab-close no longer parks a slot for minutes.
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;                 // evict the oldest idle socket instead of refusing
    // NOTE: deliberately NOT using enable_so_linger/linger_timeout=0 — that does an
    // abortive RST close which can reset the connection before the client reads the
    // response (broke OTA's 200). Dead-socket reaping is handled by keepalive below
    // + lru_purge above; normal closes stay graceful.
    cfg.open_fn          = httpd_sock_keepalive; // per-socket TCP keepalive → reap dead peers ~10s
    cfg.uri_match_fn     = httpd_uri_match_wildcard;
    cfg.stack_size       = 5120;                 // handlers build JSON (ArduinoJson heaps its docs)
    // HOL blocking on idle sockets is prevented by Connection: close (below); dead
    // peers are reaped by keepalive — so recv doesn't need to be aggressively short.
    // 10s: comfortable for normal requests, bounded for a stalled client. (Large
    // OTA uploads need more throughput tuning on the C2 — tracked separately.)
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 5;

    if (httpd_start(&_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed on port %u", _port);
        _server = nullptr;
        return false;
    }

    // Three wildcard dispatchers (one per method used) → internal route table lookup.
    for (http_method m : { HTTP_GET, HTTP_POST, HTTP_OPTIONS }) {
        httpd_uri_t u = {};
        u.uri      = "/*";
        u.method   = m;
        u.handler  = &HttpdServer::dispatchTrampoline;
        u.user_ctx = this;
        httpd_register_uri_handler(_server, &u);
    }
    ESP_LOGI(TAG, "started on port %u (%u routes, max_open_sockets=%d)",
             _port, (unsigned)_routes.size(), cfg.max_open_sockets);
    return true;
}

void HttpdServer::stop() {
    if (_server) { httpd_stop(_server); _server = nullptr; }
}

esp_err_t HttpdServer::dispatchTrampoline(httpd_req_t* req) {
    static_cast<HttpdServer*>(req->user_ctx)->handleRequest(req);
    return ESP_OK;   // response already sent inside handleRequest
}

void HttpdServer::resetPerRequest() {
    _out_headers.clear();
    _body = "";
    _body_loaded = false;
    _sent = false;
    _path = "";
    _query = "";
}

void HttpdServer::handleRequest(httpd_req_t* req) {
    resetPerRequest();
    _req = req;

    // Split path / query from req->uri (which includes the query string).
    String full(req->uri);
    int q = full.indexOf('?');
    if (q >= 0) { _path = full.substring(0, q); _query = full.substring(q + 1); }
    else        { _path = full; }

    // Route lookup: exact path + method.
    for (const Route& r : _routes) {
        if (r.method == (http_method)req->method && r.uri == _path) {
            r.fn();
            if (!_sent) httpd_resp_send(req, "", 0);   // defensive: never leave the socket hanging
            _req = nullptr;
            return;
        }
    }

    // No route: onNotFound, else 404.
    if (_not_found) {
        _not_found();
        if (!_sent) httpd_resp_send(req, "", 0);
    } else {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "{\"error\":\"Not Found\"}", HTTPD_RESP_USE_STRLEN);
    }
    _req = nullptr;
}

void HttpdServer::loadBody() {
    if (_body_loaded) return;
    _body_loaded = true;
    if (!_req) return;
    size_t total = _req->content_len;
    if (total == 0) return;
    if (total > MAX_BODY) total = MAX_BODY;    // bound heap; JSON bodies are tiny
    char buf[256];
    size_t got = 0;
    while (got < total) {
        int r = httpd_req_recv(_req, buf, (total - got) < sizeof(buf) ? (total - got) : sizeof(buf));
        if (r <= 0) break;                     // timeout / closed
        _body.concat(String(buf).substring(0, 0)); // (no-op guard; append below)
        for (int i = 0; i < r; i++) _body += buf[i];
        got += r;
    }
}

String HttpdServer::uri() { return _path; }

String HttpdServer::arg(const char* name) {
    if (strcmp(name, "plain") == 0) { loadBody(); return _body; }
    if (_query.length() == 0) return String();
    char val[128];
    if (httpd_query_key_value(_query.c_str(), name, val, sizeof(val)) == ESP_OK) return String(val);
    return String();
}

bool HttpdServer::hasArg(const char* name) {
    if (strcmp(name, "plain") == 0) { loadBody(); return _body.length() > 0; }
    if (_query.length() == 0) return false;
    char val[128];
    return httpd_query_key_value(_query.c_str(), name, val, sizeof(val)) == ESP_OK;
}

String HttpdServer::header(const char* name) {
    if (!_req) return String();
    size_t len = httpd_req_get_hdr_value_len(_req, name);
    if (len == 0) return String();
    String out;
    // Reserve len+1 for NUL.
    char* buf = (char*)malloc(len + 1);
    if (!buf) return String();
    if (httpd_req_get_hdr_value_str(_req, name, buf, len + 1) == ESP_OK) out = buf;
    free(buf);
    return out;
}

bool HttpdServer::hasHeader(const char* name) {
    return _req && httpd_req_get_hdr_value_len(_req, name) > 0;
}

void HttpdServer::sendHeader(const char* name, const String& value) {
    _out_headers.push_back(Header{ String(name), value });
}

void HttpdServer::send(int code, const char* contentType, const String& body) {
    if (!_req) return;
    httpd_resp_set_status(_req, statusText(code));
    httpd_resp_set_type(_req, contentType);
    // Force close after each response: no keep-alive means the httpd task returns to
    // select() immediately instead of blocking on an idle socket, so concurrent
    // connections are served in turn instead of starving (critical on the C2).
    httpd_resp_set_hdr(_req, "Connection", "close");
    // Header string storage (_out_headers) stays valid through httpd_resp_send, which
    // is required — httpd_resp_set_hdr does NOT copy.
    for (const Header& h : _out_headers) {
        httpd_resp_set_hdr(_req, h.name.c_str(), h.value.c_str());
    }
    httpd_resp_send(_req, body.c_str(), body.length());
    _sent = true;
}

const char* HttpdServer::statusText(int code) {
    switch (code) {
        case 200: return "200 OK";
        case 202: return "202 Accepted";
        case 204: return "204 No Content";
        case 302: return "302 Found";
        case 400: return "400 Bad Request";
        case 401: return "401 Unauthorized";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 409: return "409 Conflict";
        case 500: return "500 Internal Server Error";
        default:  return "200 OK";
    }
}
