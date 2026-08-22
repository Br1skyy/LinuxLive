#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace lili {

// A master node discovered on the LAN.
struct DiscoveredMaster {
    std::string name;
    std::string host;      // IP address that responded
    uint16_t port = 8080;  // TCP relay port
    std::string pubkey;    // master Ed25519 pubkey (hex)
    bool passphrase_required = false;
};

// Probes the LAN for master nodes (UDP broadcast + listen for presence
// responses). Returns whatever responded within the timeout.
class Discoverer {
public:
    std::vector<DiscoveredMaster> discover(uint16_t discovery_port = 9042,
                                           int timeout_ms = 1500) const;
};

}
