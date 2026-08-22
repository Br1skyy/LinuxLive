#include "lili-protocol/event.hpp"
#include "lili-protocol/signing.hpp"
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>
#include <nlohmann/json.hpp>

namespace lili {

std::string Event::serialize() const {
    nlohmann::json j;
    j["id"] = id;
    j["created_at"] = created_at;
    j["kind"] = kind;
    j["tags"] = tags;
    j["content"] = content;
    j["pubkey"] = pubkey;
    j["sig"] = sig;
    return j.dump();
}

std::optional<Event> Event::deserialize(const std::string& data) {
    try {
        auto j = nlohmann::json::parse(data);
        Event event;
        event.id = j["id"].get<uint64_t>();
        event.created_at = j["created_at"].get<uint64_t>();
        event.kind = j["kind"].get<uint16_t>();
        event.tags = j["tags"].get<std::vector<std::vector<std::string>>>();
        event.content = j["content"].get<std::string>();
        event.pubkey = j["pubkey"].get<std::string>();
        event.sig = j["sig"].get<std::string>();
        return event;
    } catch (...) {
        return std::nullopt;
    }
}

bool Event::verify() const {
    return lili::verify_signature(*this);
}

}
