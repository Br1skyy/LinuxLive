#include "lili-core/discoverer.hpp"
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

namespace lili {

std::vector<DiscoveredMaster> Discoverer::discover(uint16_t discovery_port,
                                                   int timeout_ms) const {
    std::vector<DiscoveredMaster> result;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return result;

    int broadcast = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    const std::string probe = R"({"type":"lili_probe","v":1})";
    struct sockaddr_in bc{};
    bc.sin_family = AF_INET;
    bc.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    bc.sin_port = htons(discovery_port);
    sendto(fd, probe.data(), probe.size(), 0, (struct sockaddr*)&bc, sizeof(bc));

    char buf[1024];
    const uint64_t deadline = (uint64_t)(time(nullptr)) + (uint64_t)((timeout_ms + 999) / 1000);
    while ((uint64_t)time(nullptr) <= deadline) {
        struct sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr*)&from, &from_len);
        if (n <= 0) break;  // timeout
        buf[n] = '\0';

        try {
            auto j = nlohmann::json::parse(std::string(buf, n));
            if (j.value("type", "") != "lili_presence") continue;

            DiscoveredMaster m;
            m.name = j.value("name", "LinuxLive Master");
            m.host = inet_ntoa(from.sin_addr);
            m.port = j.value("port", 8080);
            m.pubkey = j.value("pubkey", "");
            m.passphrase_required = j.value("passphrase_required", false);
            result.push_back(m);
        } catch (...) {
            // Ignore malformed presence packets.
        }
    }

    close(fd);
    return result;
}

}
