#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace lili {

struct Event {
    uint64_t id;
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
        ACHIEVEMENT = 30079,
        ACHIEVEMENT_PROOF = 30080,
    };

    std::string serialize() const;
    static std::optional<Event> deserialize(const std::string& data);
    bool verify() const;
};

}
