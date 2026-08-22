#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <cstdint>

namespace lili {

struct DiscoveryConfig {
    std::string name;             // master display name
    uint16_t relay_port;          // TCP port subnodes connect to
    std::string master_pubkey;    // master's Ed25519 pubkey (hex)
    bool passphrase_required = false;
};

// UDP responder that lets subnodes discover this master on the LAN.
// Protocol (JSON over UDP, well-known discovery port):
//   probe     -> {"type":"lili_probe","v":1}
//   presence  -> {"type":"lili_presence","v":1,"name":...,"port":...,"pubkey":...,"passphrase_required":...}
class DiscoveryResponder {
public:
    DiscoveryResponder(uint16_t discovery_port, DiscoveryConfig cfg);
    ~DiscoveryResponder();

    void start();
    void stop();

private:
    void run();

    uint16_t discovery_port_;
    DiscoveryConfig cfg_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}
