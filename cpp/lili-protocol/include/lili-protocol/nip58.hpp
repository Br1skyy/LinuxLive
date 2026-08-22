#pragma once

#include "event.hpp"
#include "signing.hpp"
#include <string>
#include <vector>

namespace lili {

enum class AchievementTier {
    SELF_REPORTED,
    LOCALLY_ATTESTED,
    COMMUNITY_VERIFIED,
    THIRD_PARTY_VERIFIED,
};

struct Achievement {
    std::string name;
    std::string description;
    AchievementTier tier;
    std::vector<std::string> criteria;
};

Event create_achievement_event(const Achievement& achievement, const KeyPair& keypair);
std::optional<Achievement> parse_achievement_event(const Event& event);

}
