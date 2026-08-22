#include "lili-core/persistence.hpp"
#include "lili-core/identity.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace lili {

Persistence::Persistence() {
    IdentityManager::ensure_data_dir();
    std::filesystem::create_directories(messages_dir());
}

std::string Persistence::achievements_path() const {
    return IdentityManager::data_dir() + "/achievements.json";
}

std::string Persistence::nodes_path() const {
    return IdentityManager::data_dir() + "/nodes.json";
}

std::string Persistence::messages_dir() const {
    return IdentityManager::data_dir() + "/messages";
}

std::string Persistence::profile_path(const std::string& pubkey) const {
    return IdentityManager::data_dir() + "/profile_" + pubkey.substr(0, 16) + ".json";
}

std::string Persistence::relay_config_path() const {
    return IdentityManager::data_dir() + "/config.json";
}

std::string Persistence::friends_path(const std::string& pubkey) const {
    return IdentityManager::data_dir() + "/friends_" + pubkey.substr(0, 16) + ".json";
}

bool Persistence::save_achievements(const std::vector<StoredAchievement>& achievements) {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& a : achievements) {
        j.push_back({
            {"id", a.id},
            {"name", a.name},
            {"description", a.description},
            {"tier", a.tier},
            {"category", a.category},
            {"icon", a.icon},
            {"unlocked", a.unlocked},
            {"unlocked_at", a.unlocked_at}
        });
    }
    std::ofstream f(achievements_path());
    f << j.dump(2);
    return f.good();
}

std::vector<StoredAchievement> Persistence::load_achievements() {
    std::vector<StoredAchievement> result;
    std::ifstream f(achievements_path());
    if (!f) return result;

    try {
        auto j = nlohmann::json::parse(f);
        for (const auto& item : j) {
            StoredAchievement a;
            a.id = item["id"].get<std::string>();
            a.name = item["name"].get<std::string>();
            a.description = item["description"].get<std::string>();
            a.tier = item["tier"].get<std::string>();
            a.category = item["category"].get<std::string>();
            a.icon = item["icon"].get<std::string>();
            a.unlocked = item["unlocked"].get<bool>();
            a.unlocked_at = item["unlocked_at"].get<uint64_t>();
            result.push_back(a);
        }
    } catch (...) {}

    return result;
}

bool Persistence::save_messages(const std::string& node_id, const std::vector<StoredMessage>& messages) {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& m : messages) {
        j.push_back({
            {"sender_pubkey", m.sender_pubkey},
            {"sender_name", m.sender_name},
            {"content", m.content},
            {"timestamp", m.timestamp},
            {"node_id", m.node_id},
            {"is_encrypted", m.is_encrypted}
        });
    }
    std::ofstream f(messages_dir() + "/" + node_id + ".json");
    f << j.dump(2);
    return f.good();
}

std::vector<StoredMessage> Persistence::load_messages(const std::string& node_id) {
    std::vector<StoredMessage> result;
    std::ifstream f(messages_dir() + "/" + node_id + ".json");
    if (!f) return result;

    try {
        auto j = nlohmann::json::parse(f);
        for (const auto& item : j) {
            StoredMessage m;
            m.sender_pubkey = item["sender_pubkey"].get<std::string>();
            m.sender_name = item["sender_name"].get<std::string>();
            m.content = item["content"].get<std::string>();
            m.timestamp = item["timestamp"].get<uint64_t>();
            m.node_id = item["node_id"].get<std::string>();
            m.is_encrypted = item["is_encrypted"].get<bool>();
            result.push_back(m);
        }
    } catch (...) {}

    return result;
}

bool Persistence::save_nodes(const std::vector<StoredNode>& nodes) {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& n : nodes) {
        j.push_back({
            {"id", n.id},
            {"name", n.name},
            {"description", n.description},
            {"creator_pubkey", n.creator_pubkey},
            {"admin_privkey", n.admin_privkey},
            {"relay_url", n.relay_url},
            {"created_at", n.created_at},
            {"member_count", n.member_count},
            {"is_local", n.is_local},
            {"running", n.running}
        });
    }
    std::ofstream f(nodes_path());
    f << j.dump(2);
    return f.good();
}

std::vector<StoredNode> Persistence::load_nodes() {
    std::vector<StoredNode> result;
    std::ifstream f(nodes_path());
    if (!f) return result;

    try {
        auto j = nlohmann::json::parse(f);
        for (const auto& item : j) {
            StoredNode n;
            n.id = item["id"].get<std::string>();
            n.name = item["name"].get<std::string>();
            n.description = item.value("description", "");
            n.creator_pubkey = item.value("creator_pubkey", "");
            n.admin_privkey = item.value("admin_privkey", "");
            n.relay_url = item.value("relay_url", "");
            n.created_at = item.value("created_at", 0ULL);
            n.member_count = item.value("member_count", 1);
            n.is_local = item.value("is_local", false);
            n.running = item.value("running", n.is_local);
            result.push_back(n);
        }
    } catch (...) {}

    return result;
}

bool Persistence::save_profile(const std::string& pubkey, const std::string& display_name, const std::string& bio) {
    nlohmann::json j;
    j["pubkey"] = pubkey;
    j["display_name"] = display_name;
    j["bio"] = bio;
    std::ofstream f(profile_path(pubkey));
    f << j.dump(2);
    return f.good();
}

bool Persistence::load_profile(const std::string& pubkey, std::string& display_name, std::string& bio) {
    std::ifstream f(profile_path(pubkey));
    if (!f) return false;

    try {
        auto j = nlohmann::json::parse(f);
        display_name = j["display_name"].get<std::string>();
        bio = j.value("bio", "");
        return true;
    } catch (...) {
        return false;
    }
}

bool Persistence::save_relay_url(const std::string& url) {
    nlohmann::json j;
    j["relay_url"] = url;
    std::ofstream f(relay_config_path());
    f << j.dump(2);
    return f.good();
}

std::string Persistence::load_relay_url() {
    std::ifstream f(relay_config_path());
    if (!f) return "ws://localhost:8080";

    try {
        auto j = nlohmann::json::parse(f);
        return j["relay_url"].get<std::string>();
    } catch (...) {
        return "ws://localhost:8080";
    }
}

bool Persistence::save_friends(const std::string& pubkey, const std::vector<std::string>& friend_pubkeys) {
    nlohmann::json j = friend_pubkeys;
    std::ofstream f(friends_path(pubkey));
    f << j.dump(2);
    return f.good();
}

std::vector<std::string> Persistence::load_friends(const std::string& pubkey) {
    std::vector<std::string> result;
    std::ifstream f(friends_path(pubkey));
    if (!f) return result;

    try {
        auto j = nlohmann::json::parse(f);
        for (const auto& item : j) {
            result.push_back(item.get<std::string>());
        }
    } catch (...) {}

    return result;
}

bool Persistence::save_relay_list(const std::vector<std::string>& urls) {
    nlohmann::json j = urls;
    std::ofstream f(IdentityManager::data_dir() + "/relay_list.json");
    f << j.dump(2);
    return f.good();
}

std::vector<std::string> Persistence::load_relay_list() {
    std::vector<std::string> result;
    std::ifstream f(IdentityManager::data_dir() + "/relay_list.json");
    if (!f) {
        std::string single = load_relay_url();
        if (!single.empty()) result.push_back(single);
        return result;
    }

    try {
        auto j = nlohmann::json::parse(f);
        for (const auto& item : j) {
            result.push_back(item.get<std::string>());
        }
    } catch (...) {}

    return result;
}

bool Persistence::export_achievements(const std::string& filepath,
                                       const std::vector<StoredAchievement>& achievements,
                                       const std::string& pubkey_hex) {
    nlohmann::json events = nlohmann::json::array();
    uint64_t now = static_cast<uint64_t>(time(nullptr));

    for (const auto& a : achievements) {
        if (!a.unlocked) continue;

        nlohmann::json content;
        content["achievement_id"] = a.id;
        content["name"] = a.name;
        content["description"] = a.description;
        content["icon"] = a.icon;
        content["tier"] = a.tier;
        content["category"] = a.category;
        content["unlocked_at"] = a.unlocked_at;

        nlohmann::json event;
        event["kind"] = 30079;
        event["content"] = content.dump();
        event["pubkey"] = pubkey_hex;
        event["created_at"] = a.unlocked_at > 0 ? a.unlocked_at : now;
        event["tags"] = nlohmann::json::array({
            nlohmann::json::array({"d", a.id}),
            nlohmann::json::array({"achievement", a.id})
        });
        event["sig"] = "";

        events.push_back(event);
    }

    nlohmann::json export_file;
    export_file["version"] = 1;
    export_file["pubkey"] = pubkey_hex;
    export_file["exported_at"] = now;
    export_file["events"] = events;

    std::ofstream f(filepath);
    f << export_file.dump(2);
    return f.good();
}

std::vector<StoredAchievement> Persistence::import_achievements(const std::string& filepath) {
    std::vector<StoredAchievement> result;
    std::ifstream f(filepath);
    if (!f) return result;

    try {
        auto j = nlohmann::json::parse(f);
        if (!j.contains("events")) return result;

        for (const auto& ev : j["events"]) {
            if (!ev.contains("content") || !ev.contains("kind")) continue;
            if (ev["kind"].get<int>() != 30079) continue;

            auto content = nlohmann::json::parse(ev["content"].get<std::string>());
            StoredAchievement a;
            a.id = content.value("achievement_id", "");
            a.name = content.value("name", "");
            a.description = content.value("description", "");
            a.icon = content.value("icon", "");
            a.tier = content.value("tier", "");
            a.category = content.value("category", "");
            a.unlocked = true;
            a.unlocked_at = content.value("unlocked_at", 0ULL);
            result.push_back(a);
        }
    } catch (...) {}

    return result;
}

}
