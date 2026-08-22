#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <openssl/ssl.h>

namespace lili {

class WsClient {
public:
    WsClient();
    ~WsClient();

    using MessageCallback = std::function<void(const std::string&)>;
    using CloseCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string&)>;

    void set_on_message(MessageCallback cb) { on_message_ = cb; }
    void set_on_close(CloseCallback cb) { on_close_ = cb; }
    void set_on_error(ErrorCallback cb) { on_error_ = cb; }

    bool connect(const std::string& url, bool tor_proxy = false);
    void disconnect();
    bool is_connected() const;

    bool send_text(const std::string& message);

private:
    void recv_loop();
    bool ws_handshake(const std::string& host, int port, const std::string& path);
    bool ws_send_frame(uint8_t opcode, const uint8_t* payload, size_t len);
    bool raw_send(const uint8_t* data, size_t len);
    bool raw_recv(uint8_t* buf, size_t len, size_t* out_len);

    int fd_ = -1;
    SSL* ssl_ = nullptr;
    SSL_CTX* ctx_ = nullptr;
    bool use_tls_ = false;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::thread recv_thread_;
    std::mutex send_mutex_;

    uint8_t mask_key_[4] = {};

    MessageCallback on_message_;
    CloseCallback on_close_;
    ErrorCallback on_error_;
};

}
