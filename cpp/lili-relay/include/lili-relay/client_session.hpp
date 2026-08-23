#pragma once

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <cstdint>

namespace lili {

class ClientSession : public std::enable_shared_from_this<ClientSession> {
public:
    ClientSession(int socket_fd);
    ~ClientSession();

    void start();
    void stop();
    bool is_running() const { return running_; }
    bool is_websocket() const { return ws_handshake_done_; }

    void send_ws_text(const std::string& message);

    using MessageCallback = std::function<void(const std::string&)>;
    void set_on_message(MessageCallback cb) { on_message_ = cb; }

private:
    bool do_ws_handshake();
    bool send_http_response(const std::string& response);
    bool recv_raw(uint8_t* buf, size_t len, size_t* out_len);
    bool send_raw(const uint8_t* data, size_t len);
    void ws_read_loop();
    bool ws_send_frame(uint8_t opcode, const uint8_t* payload, size_t len);

    int socket_fd_;
    std::atomic<bool> running_;
    std::atomic<bool> ws_handshake_done_;
    std::thread read_thread_;
    MessageCallback on_message_;
};

}
