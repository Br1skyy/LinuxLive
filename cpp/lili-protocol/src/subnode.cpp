#include "lili-protocol/subnode.hpp"
#include <nlohmann/json.hpp>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace lili {

static std::string pubkey_hex(const KeyPair& kp) {
    std::ostringstream oss;
    for (size_t i = 0; i < 32; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(kp.public_key[i]);
    return oss.str();
}

std::optional<RegistrationInfo> parse_register_info(const Event& event) {
    if (event.kind != static_cast<uint16_t>(Event::Kind::REGISTER)) return std::nullopt;
    try {
        auto j = nlohmann::json::parse(event.content);
        RegistrationInfo info;
        info.display_name = j.value("display_name", "");
        info.hostname = j.value("hostname", "");
        info.distro = j.value("distro", "");
        info.wm = j.value("wm", "");
        info.client_version = j.value("client_version", "");
        return info;
    } catch (...) {
        return std::nullopt;
    }
}

Event create_register_event(const RegistrationInfo& info,
                            const std::string& passphrase,
                            const KeyPair& keypair) {
    Event event;
    event.created_at = static_cast<uint64_t>(time(nullptr));
    event.kind = static_cast<uint16_t>(Event::Kind::REGISTER);

    nlohmann::json content;
    content["display_name"] = info.display_name;
    content["hostname"] = info.hostname;
    content["distro"] = info.distro;
    content["wm"] = info.wm;
    content["client_version"] = info.client_version;
    if (!passphrase.empty()) content["passphrase"] = passphrase;
    event.content = content.dump();

    event.tags = {{"d", pubkey_hex(keypair)}};
    return sign_event(event, keypair);
}

Event create_heartbeat_event(uint64_t uptime_seconds,
                             uint32_t achievements_unlocked,
                             const KeyPair& keypair) {
    Event event;
    event.created_at = static_cast<uint64_t>(time(nullptr));
    event.kind = static_cast<uint16_t>(Event::Kind::HEARTBEAT);

    nlohmann::json content;
    content["uptime"] = uptime_seconds;
    content["achievements_unlocked"] = achievements_unlocked;
    event.content = content.dump();

    event.tags = {{"d", pubkey_hex(keypair)}};
    return sign_event(event, keypair);
}

Event create_register_ack(const std::string& subnode_pubkey,
                          bool accepted,
                          const std::string& message,
                          const KeyPair& master_keypair) {
    Event event;
    event.created_at = static_cast<uint64_t>(time(nullptr));
    event.kind = static_cast<uint16_t>(Event::Kind::REGISTER_ACK);

    nlohmann::json content;
    content["status"] = accepted ? "accepted" : "denied";
    content["message"] = message;
    event.content = content.dump();

    event.tags = {{"d", subnode_pubkey}};
    return sign_event(event, master_keypair);
}

}
