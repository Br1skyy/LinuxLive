#pragma once

#include "lili-protocol/event.hpp"
#include "lili-protocol/signing.hpp"
#include <optional>
#include <string>

namespace lili {

// Payload carried in a kind 30021 REGISTER event's content field.
struct RegistrationInfo {
    std::string display_name;
    std::string hostname;
    std::string distro;
    std::string wm;
    std::string client_version;
};

// Parse the content of a kind 30021 REGISTER event.
std::optional<RegistrationInfo> parse_register_info(const Event& event);

// Build a signed REGISTER event (subnode -> master). The subnode's Ed25519
// pubkey (event.pubkey) is its identity; it is bound into the 'd' tag.
Event create_register_event(const RegistrationInfo& info,
                            const std::string& passphrase,
                            const KeyPair& keypair);

// Build a signed HEARTBEAT event (subnode -> master, kind 30023).
Event create_heartbeat_event(uint64_t uptime_seconds,
                             uint32_t achievements_unlocked,
                             const KeyPair& keypair);

// Build a signed REGISTER_ACK event (master -> subnode, kind 30022).
// `subnode_pubkey` is placed in the 'd' tag so the subnode can match it.
Event create_register_ack(const std::string& subnode_pubkey,
                          bool accepted,
                          const std::string& message,
                          const KeyPair& master_keypair);

}
