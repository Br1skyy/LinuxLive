#include "lili-relay/client_session.hpp"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sys/socket.h>

namespace lili {

ClientSession::ClientSession(int socket_fd) : socket_fd_(socket_fd), running_(false) {}

ClientSession::~ClientSession() {
    stop();
}

void ClientSession::start() {
    running_ = true;
    read_thread_ = std::thread([this]() { read_message(); });
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

void ClientSession::send(const std::string& message) {
    if (socket_fd_ >= 0) {
        ::send(socket_fd_, message.c_str(), message.size(), 0);
    }
}

void ClientSession::read_message() {
    char buffer[1024];
    while (running_) {
        ssize_t bytes_read = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            handle_message(std::string(buffer));
        } else if (bytes_read == 0) {
            break;
        }
    }
}

void ClientSession::handle_message(const std::string& message) {
    std::cout << "Received: " << message << std::endl;
}

}
