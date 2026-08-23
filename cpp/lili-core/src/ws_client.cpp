#include "lili-core/ws_client.hpp"
#include <cstring>
#include <sstream>
#include <random>
#include <algorithm>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace lili {

WsClient::WsClient() {
    ctx_ = SSL_CTX_new(TLS_client_method());
    if (ctx_) {
        SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
        SSL_CTX_set_default_verify_paths(ctx_);
    }
}

WsClient::~WsClient() { disconnect(); }

bool WsClient::is_connected() const { return connected_; }

void WsClient::disconnect() {
    running_ = false;
    connected_ = false;
    if (ssl_) { SSL_shutdown(ssl_); SSL_free(ssl_); ssl_ = nullptr; }
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
    if (ctx_) { SSL_CTX_free(ctx_); ctx_ = nullptr; }
    if (recv_thread_.joinable()) recv_thread_.join();
}

static bool parse_ws_url(const std::string& url, bool& use_tls, std::string& host, int& port, std::string& path) {
    use_tls = false;
    host.clear();
    port = 80;
    path = "/";

    size_t pos = 0;
    if (url.substr(0, 3) == "ws://" || url.substr(0, 7) == "http://") {
        pos = url.find("://") + 3; use_tls = false; port = 80;
    }
    else if (url.substr(0, 6) == "wss://" || url.substr(0, 8) == "https://") {
        pos = url.find("://") + 3; use_tls = true; port = 443;
    }
    else return false;

    size_t path_start = url.find('/', pos);
    size_t colon = url.find(':', pos);

    if (colon != std::string::npos && (path_start == std::string::npos || colon < path_start)) {
        host = url.substr(pos, colon - pos);
        port = std::stoi(url.substr(colon + 1, path_start != std::string::npos ? path_start - colon - 1 : std::string::npos));
    } else {
        host = url.substr(pos, path_start != std::string::npos ? path_start - pos : std::string::npos);
    }

    if (path_start != std::string::npos) path = url.substr(path_start);
    return !host.empty();
}

bool WsClient::connect(const std::string& url, bool tor_proxy) {
    disconnect();

    bool tls;
    std::string host;
    int port;
    std::string path;
    if (!parse_ws_url(url, tls, host, port, path)) return false;

    use_tls_ = tls;

    if (tor_proxy) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;

        struct sockaddr_in proxy_addr{};
        proxy_addr.sin_family = AF_INET;
        proxy_addr.sin_port = htons(9050);
        proxy_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (::connect(fd_, (struct sockaddr*)&proxy_addr, sizeof(proxy_addr)) < 0) {
            close(fd_); fd_ = -1; return false;
        }

        // SOCKS5 greeting: version 5, no auth
        uint8_t hello[3] = {0x05, 0x01, 0x00};
        if (!raw_send(hello, 3)) { close(fd_); fd_ = -1; return false; }

        uint8_t hello_resp[2];
        size_t got;
        if (!raw_recv(hello_resp, 2, &got) || got < 2 || hello_resp[0] != 0x05) {
            close(fd_); fd_ = -1; return false;
        }

        // SOCKS5 CONNECT with domain name
        // +----+-----+-------+------+----------+----------+
        // |VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
        // +----+-----+-------+------+----------+----------+
        // | 05 | 01  |  00   | 03   | variable |    02    |
        // +----+-----+-------+------+----------+----------+
        std::vector<uint8_t> connect_req;
        connect_req.push_back(0x05);
        connect_req.push_back(0x01);
        connect_req.push_back(0x00);
        connect_req.push_back(0x03);
        connect_req.push_back((uint8_t)host.size());
        connect_req.insert(connect_req.end(), host.begin(), host.end());
        connect_req.push_back((port >> 8) & 0xFF);
        connect_req.push_back(port & 0xFF);

        if (!raw_send(connect_req.data(), connect_req.size())) {
            close(fd_); fd_ = -1; return false;
        }

        uint8_t resp[256];
        if (!raw_recv(resp, 10, &got) || got < 4 || resp[1] != 0x00) {
            close(fd_); fd_ = -1; return false;
        }
    } else {
        struct hostent* he = gethostbyname(host.c_str());
        if (!he) return false;

        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

        if (::connect(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd_); fd_ = -1; return false;
        }
    }

    if (use_tls_ && ctx_) {
        ssl_ = SSL_new(ctx_);
        SSL_set_fd(ssl_, fd_);
        SSL_set_tlsext_host_name(ssl_, host.c_str());
        if (SSL_connect(ssl_) <= 0) {
            SSL_free(ssl_); ssl_ = nullptr;
            close(fd_); fd_ = -1;
            return false;
        }
    }

    if (!ws_handshake(host, port, path)) {
        disconnect();
        return false;
    }

    connected_ = true;
    running_ = true;
    recv_thread_ = std::thread(&WsClient::recv_loop, this);
    return true;
}

bool WsClient::ws_handshake(const std::string& host, int, const std::string& path) {
    uint8_t nonce[16];
    RAND_bytes(nonce, 16);
    std::string key_b64;
    {
        static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 16; i += 3) {
            uint32_t n = (uint32_t)nonce[i] << 16;
            if (i + 1 < 16) n |= (uint32_t)nonce[i + 1] << 8;
            if (i + 2 < 16) n |= (uint32_t)nonce[i + 2];
            key_b64 += B64[(n >> 18) & 0x3F];
            key_b64 += B64[(n >> 12) & 0x3F];
            key_b64 += (i + 1 < 16) ? B64[(n >> 6) & 0x3F] : '=';
            key_b64 += (i + 2 < 16) ? B64[n & 0x3F] : '=';
        }
    }

    std::ostringstream req;
    req << "GET " << path << " HTTP/1.1\r\n";
    req << "Host: " << host << "\r\n";
    req << "Upgrade: websocket\r\n";
    req << "Connection: Upgrade\r\n";
    req << "Sec-WebSocket-Key: " << key_b64 << "\r\n";
    req << "Sec-WebSocket-Version: 13\r\n";
    req << "\r\n";

    std::string r = req.str();
    if (!raw_send((const uint8_t*)r.data(), r.size())) return false;

    std::string resp;
    resp.reserve(4096);
    char c;
    while (true) {
        size_t n;
        if (!raw_recv((uint8_t*)&c, 1, &n) || n == 0) return false;
        resp += c;
        if (resp.size() >= 4 && resp.substr(resp.size() - 4) == "\r\n\r\n") break;
        if (resp.size() > 4096) return false;
    }

    return resp.find("101") != std::string::npos;
}

bool WsClient::raw_send(const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n;
        if (ssl_) n = SSL_write(ssl_, data + sent, len - sent);
        else n = ::send(fd_, data + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool WsClient::raw_recv(uint8_t* buf, size_t len, size_t* out_len) {
    ssize_t n;
    if (ssl_) n = SSL_read(ssl_, buf, len);
    else {
        struct pollfd pfd = {fd_, POLLIN, 0};
        if (poll(&pfd, 1, 5000) <= 0) { *out_len = 0; return true; }
        n = ::recv(fd_, buf, len, 0);
    }
    if (n <= 0) { *out_len = 0; return false; }
    *out_len = n;
    return true;
}

bool WsClient::ws_send_frame(uint8_t opcode, const uint8_t* payload, size_t len) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    std::vector<uint8_t> frame;
    frame.push_back(0x80 | opcode);

    if (len < 126) {
        frame.push_back(0x80 | (uint8_t)len);
    } else if (len <= 65535) {
        frame.push_back(0x80 | 126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; i--)
            frame.push_back((len >> (i * 8)) & 0xFF);
    }

    uint8_t mask[4];
    RAND_bytes(mask, 4);
    frame.insert(frame.end(), mask, mask + 4);

    for (size_t i = 0; i < len; i++)
        frame.push_back(payload[i] ^ mask[i % 4]);

    return raw_send(frame.data(), frame.size());
}

bool WsClient::send_text(const std::string& message) {
    return ws_send_frame(0x01, (const uint8_t*)message.data(), message.size());
}

void WsClient::recv_loop() {
    while (running_) {
        uint8_t hdr[2];
        size_t n;
        if (!raw_recv(hdr, 2, &n) || n < 2) {
            if (connected_) { connected_ = false; if (on_close_) on_close_(); }
            break;
        }

        bool fin = hdr[0] & 0x80;
        uint8_t opcode = hdr[0] & 0x0F;
        bool masked = hdr[1] & 0x80;
        uint64_t payload_len = hdr[1] & 0x7F;

        if (payload_len == 126) {
            uint8_t ext[2];
            if (!raw_recv(ext, 2, &n) || n < 2) break;
            payload_len = ((uint64_t)ext[0] << 8) | ext[1];
        } else if (payload_len == 127) {
            uint8_t ext[8];
            if (!raw_recv(ext, 8, &n) || n < 8) break;
            payload_len = 0;
            for (int i = 0; i < 8; i++)
                payload_len = (payload_len << 8) | ext[i];
        }

        uint8_t server_mask[4] = {};
        if (masked) {
            if (!raw_recv(server_mask, 4, &n) || n < 4) break;
        }

        std::vector<uint8_t> payload(payload_len);
        size_t total_read = 0;
        while (total_read < payload_len) {
            size_t chunk = std::min((uint64_t)4096, payload_len - total_read);
            size_t got;
            if (!raw_recv(payload.data() + total_read, chunk, &got) || got == 0) break;
            total_read += got;
        }

        if (masked) {
            for (size_t i = 0; i < payload_len; i++)
                payload[i] ^= server_mask[i % 4];
        }

        if (opcode == 0x01) {
            std::string msg((char*)payload.data(), payload_len);
            if (on_message_) on_message_(msg);
        } else if (opcode == 0x08) {
            ws_send_frame(0x08, nullptr, 0);
            connected_ = false;
            if (on_close_) on_close_();
            break;
        } else if (opcode == 0x09) {
            ws_send_frame(0x0A, payload.data(), payload_len);
        }
    }
}

}
