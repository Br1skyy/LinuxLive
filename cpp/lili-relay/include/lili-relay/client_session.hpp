#pragma once

#include <memory>
#include <string>
#include <thread>
#include <atomic>

namespace lili {

class ClientSession : public std::enable_shared_from_this<ClientSession> {
public:
    ClientSession(int socket_fd);
    ~ClientSession();

    void start();
    void stop();
    bool is_running() const { return running_; }

    void send(const std::string& message);

private:
    void read_message();
    void handle_message(const std::string& message);

    int socket_fd_;
    std::atomic<bool> running_;
    std::thread read_thread_;
};

}
