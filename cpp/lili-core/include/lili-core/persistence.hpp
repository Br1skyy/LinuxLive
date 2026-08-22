#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace lili {

struct StoredAchievement {
    std::string id;
    std::string name;
    std::string description;
    std::string tier;
    std::string category;
    std::string icon;
    bool unlocked;
    uint64_t unlocked_at;
};

struct StoredMessage {
    std::string sender_pubkey;
    std::string sender_name;
    std::string content;
    uint64_t timestamp;
    std::string node_id;
    bool is_encrypted;
};

struct StoredNode {
    std::string id;
    std::string name;
    std::string description;
    std::string creator_pubkey;
    uint64_t created_at;
};

class Persistence {
public:
    Persistence();

    bool save_achievements(const std::vector<StoredAchievement>& achievements);
    std::vector<StoredAchievement> load_achievements();

    bool save_messages(const std::string& node_id, const std::vector<StoredMessage>& messages);
    std::vector<StoredMessage> load_messages(const std::string& node_id);

    bool save_nodes(const std::vector<StoredNode>& nodes);
    std::vector<StoredNode> load_nodes();

    bool save_profile(const std::string& pubkey, const std::string& display_name, const std::string& bio);
    bool load_profile(const std::string& pubkey, std::string& display_name, std::string& bio);

    bool save_relay_url(const std::string& url);
    std::string load_relay_url();

    bool save_relay_list(const std::vector<std::string>& urls);
    std::vector<std::string> load_relay_list();

    // Role: "master" or "subnode" (persisted; one active role per machine).
    bool save_role(const std::string& role);
    std::string load_role();                      // default "subnode"

    bool save_master_name(const std::string& name);
    std::string load_master_name();               // default "LinuxLive Master"
    bool save_master_port(uint16_t port);
    uint16_t load_master_port();                  // default 7777
    bool save_master_passphrase(const std::string& pass);
    std::string load_master_passphrase();         // default "" (open)
    bool save_master_url(const std::string& url);
    std::string load_master_url();                // subnode's hub URL; default ""

    bool save_friends(const std::string& pubkey, const std::vector<std::string>& friend_pubkeys);
    std::vector<std::string> load_friends(const std::string& pubkey);

    bool export_achievements(const std::string& filepath,
                             const std::vector<StoredAchievement>& achievements,
                             const std::string& pubkey_hex);
    std::vector<StoredAchievement> import_achievements(const std::string& filepath);

private:
    std::string achievements_path() const;
    std::string nodes_path() const;
    std::string messages_dir() const;
    std::string profile_path(const std::string& pubkey) const;
    std::string relay_config_path() const;
    std::string master_config_path() const;
    std::string friends_path(const std::string& pubkey) const;
};

}
