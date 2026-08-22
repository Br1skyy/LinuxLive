#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace lili {

struct Event {
    // NIP-01: 64-char lowercase hex SHA-256 of the canonical serialization.
    std::string id;
    uint64_t created_at;
    uint16_t kind;
    std::vector<std::vector<std::string>> tags;
    std::string content;
    std::string pubkey;
    std::string sig;

    enum class Kind : uint16_t {
        METADATA = 0,
        TEXT_NOTE = 1,
        CHANNEL_MESSAGE = 42,
        NODE = 30019,
        NODE_JOIN = 30020,
        REGISTER = 30021,      // subnode -> master
        REGISTER_ACK = 30022,  // master -> subnode (signed by master)
        HEARTBEAT = 30023,     // subnode -> master (liveness)
        ACHIEVEMENT = 30079,
        ACHIEVEMENT_PROOF = 30080,
        STATS = 30090,           // subnode -> master: system/terminal stats
    };

    std::string serialize() const;
    static std::optional<Event> deserialize(const std::string& data);
    bool verify() const;
};

}
