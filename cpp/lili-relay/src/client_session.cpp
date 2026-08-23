#include "lili-relay/client_session.hpp"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <sys/socket.h>
#include <openssl/sha.h>

namespace lili {

static const char* WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static std::string base64_encode(const uint8_t* data, size_t len) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) n |= (uint32_t)data[i + 2];
        out += tbl[(n >> 18) & 0x3F];
        out += tbl[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? tbl[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? tbl[n & 0x3F] : '=';
    }
    return out;
}

ClientSession::ClientSession(int socket_fd)
    : socket_fd_(socket_fd), running_(false), ws_handshake_done_(false) {}

ClientSession::~ClientSession() {
    stop();
}

void ClientSession::start() {
    running_ = true;
    read_thread_ = std::thread([this]() {
        if (!do_ws_handshake()) {
            running_ = false;
            if (socket_fd_ >= 0) { close(socket_fd_); socket_fd_ = -1; }
            return;
        }
        ws_handshake_done_ = true;
        ws_read_loop();
    });
}

void ClientSession::stop() {
    running_ = false;
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    if (read_thread_.joinable()) {
        read_thread_.join();
    }
}

bool ClientSession::send_raw(const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(socket_fd_, data + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool ClientSession::recv_raw(uint8_t* buf, size_t len, size_t* out_len) {
    ssize_t n = ::recv(socket_fd_, buf, len, 0);
    if (n <= 0) { *out_len = 0; return false; }
    *out_len = n;
    return true;
}

bool ClientSession::do_ws_handshake() {
    std::string request;
    request.reserve(4096);
    char c;
    while (true) {
        size_t n;
        if (!recv_raw((uint8_t*)&c, 1, &n) || n == 0) return false;
        request += c;
        if (request.size() >= 4 && request.substr(request.size() - 4) == "\r\n\r\n") break;
        if (request.size() > 4096) return false;
    }

    if (request.find("Upgrade: websocket") == std::string::npos &&
        request.find("Upgrade: WebSocket") == std::string::npos) {
        std::string resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        send_raw((const uint8_t*)resp.data(), resp.size());
        return false;
    }

    std::string key;
    size_t key_pos = request.find("Sec-WebSocket-Key: ");
    if (key_pos == std::string::npos) {
        key_pos = request.find("Sec-WebSocket-Key:");
    }
    if (key_pos != std::string::npos) {
        key_pos += 19;
        size_t key_end = request.find("\r\n", key_pos);
        if (key_end != std::string::npos) {
            key = request.substr(key_pos, key_end - key_pos);
        }
    }

    if (key.empty()) {
        std::string resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        send_raw((const uint8_t*)resp.data(), resp.size());
        return false;
    }

    std::string accept_input = key + WS_MAGIC;
    uint8_t hash[SHA_DIGEST_LENGTH];
    SHA1((const uint8_t*)accept_input.data(), accept_input.size(), hash);
    std::string accept_key = base64_encode(hash, SHA_DIGEST_LENGTH);

    std::ostringstream resp;
    resp << "HTTP/1.1 101 Switching Protocols\r\n"
         << "Upgrade: websocket\r\n"
         << "Connection: Upgrade\r\n"
         << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
         << "\r\n";

    std::string r = resp.str();
    return send_raw((const uint8_t*)r.data(), r.size());
}

void ClientSession::ws_read_loop() {
    while (running_) {
        uint8_t hdr[2];
        size_t n;
        if (!recv_raw(hdr, 2, &n) || n < 2) break;

        uint8_t opcode = hdr[0] & 0x0F;
        bool masked = hdr[1] & 0x80;
        uint64_t payload_len = hdr[1] & 0x7F;

        if (payload_len == 126) {
            uint8_t ext[2];
            if (!recv_raw(ext, 2, &n) || n < 2) break;
            payload_len = ((uint64_t)ext[0] << 8) | ext[1];
        } else if (payload_len == 127) {
            uint8_t ext[8];
            if (!recv_raw(ext, 8, &n) || n < 8) break;
            payload_len = 0;
            for (int i = 0; i < 8; i++)
                payload_len = (payload_len << 8) | ext[i];
        }

        uint8_t mask[4] = {};
        if (masked) {
            if (!recv_raw(mask, 4, &n) || n < 4) break;
        }

        std::vector<uint8_t> payload(payload_len);
        size_t total_read = 0;
        while (total_read < payload_len) {
            size_t chunk = std::min((uint64_t)4096, payload_len - total_read);
            size_t got;
            if (!recv_raw(payload.data() + total_read, chunk, &got) || got == 0) break;
            total_read += got;
        }

        if (masked) {
            for (size_t i = 0; i < payload_len; i++)
                payload[i] ^= mask[i % 4];
        }

        if (opcode == 0x01) {
            std::string msg((char*)payload.data(), payload_len);
            if (on_message_) on_message_(msg);
        } else if (opcode == 0x08) {
            ws_send_frame(0x08, nullptr, 0);
            break;
        } else if (opcode == 0x09) {
            ws_send_frame(0x0A, payload.data(), payload_len);
        }
    }
    running_ = false;
}

bool ClientSession::ws_send_frame(uint8_t opcode, const uint8_t* payload, size_t len) {
    std::vector<uint8_t> frame;
    frame.push_back(0x80 | opcode);

    if (len < 126) {
        frame.push_back((uint8_t)len);
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--)
            frame.push_back((len >> (i * 8)) & 0xFF);
    }

    if (payload && len > 0) {
        frame.insert(frame.end(), payload, payload + len);
    }

    return send_raw(frame.data(), frame.size());
}

void ClientSession::send_ws_text(const std::string& message) {
    if (ws_handshake_done_ && running_) {
        ws_send_frame(0x01, (const uint8_t*)message.data(), message.size());
    }
}

}
