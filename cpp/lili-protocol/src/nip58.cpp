#include "lili-protocol/nip58.hpp"
#include "lili-protocol/signing.hpp"
#include <nlohmann/json.hpp>
#include <sstream>

namespace lili {

Event create_achievement_event(const Achievement& achievement, const KeyPair& keypair) {
    Event event;
    event.created_at = static_cast<uint64_t>(time(nullptr));
    event.kind = static_cast<uint16_t>(Event::Kind::ACHIEVEMENT);

    nlohmann::json content;
    content["name"] = achievement.name;
    content["description"] = achievement.description;
    content["tier"] = static_cast<int>(achievement.tier);
    content["criteria"] = achievement.criteria;
    event.content = content.dump();

    event.tags.push_back({"d", achievement.name});
    event.tags.push_back({"tier", std::to_string(static_cast<int>(achievement.tier))});

    return sign_event(event, keypair);
}

std::optional<Achievement> parse_achievement_event(const Event& event) {
    if (event.kind != static_cast<uint16_t>(Event::Kind::ACHIEVEMENT)) {
        return std::nullopt;
    }

    try {
        auto j = nlohmann::json::parse(event.content);
        Achievement achievement;
        achievement.name = j["name"].get<std::string>();
        achievement.description = j["description"].get<std::string>();
        achievement.tier = static_cast<AchievementTier>(j["tier"].get<int>());
        achievement.criteria = j["criteria"].get<std::vector<std::string>>();
        return achievement;
    } catch (...) {
        return std::nullopt;
    }
}

}
