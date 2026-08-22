#include "lili-relay/discovery.hpp"
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <utility>
#include <iostream>

namespace lili {

DiscoveryResponder::DiscoveryResponder(uint16_t discovery_port, DiscoveryConfig cfg)
    : discovery_port_(discovery_port), cfg_(std::move(cfg)) {}

DiscoveryResponder::~DiscoveryResponder() {
    stop();
}

void DiscoveryResponder::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this]() { run(); });
}

void DiscoveryResponder::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void DiscoveryResponder::run() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::cerr << "Discovery: failed to create UDP socket" << std::endl;
        return;
    }
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(discovery_port_);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Discovery: failed to bind UDP port " << discovery_port_
                  << " (another master may be using it)" << std::endl;
        close(fd);
        return;
    }

    // Time out so stop() can unblock the loop.
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;  // 200 ms
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::cout << "Discovery: master listening for probes on UDP " << discovery_port_ << std::endl;

    char buf[1024];
    while (running_) {
        struct sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr*)&from, &from_len);
        if (n <= 0) continue;  // timeout or error; re-check running_
        buf[n] = '\0';

        try {
            auto j = nlohmann::json::parse(std::string(buf, n));
            if (j.value("type", "") != "lili_probe") continue;

            nlohmann::json resp;
            resp["type"] = "lili_presence";
            resp["v"] = 1;
            resp["name"] = cfg_.name;
            resp["port"] = cfg_.relay_port;
            resp["pubkey"] = cfg_.master_pubkey;
            resp["passphrase_required"] = cfg_.passphrase_required;
            std::string out = resp.dump();

            sendto(fd, out.data(), out.size(), 0,
                   (struct sockaddr*)&from, from_len);
        } catch (...) {
            // Ignore malformed probes.
        }
    }

    close(fd);
}

}
