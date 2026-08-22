#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <cstdint>

namespace lili {

// A machine registered with a master node. Its identity is its Ed25519 pubkey.
struct Subnode {
    std::string pubkey;
    std::string display_name;
    std::string hostname;
    std::string distro;
    std::string wm;
    std::string ip;          // last known address from the TCP session
    uint64_t registered_at = 0;
    uint64_t last_seen = 0;  // heartbeat / activity
    bool active = false;
};

// Authoritative registry of registered subnodes, persisted as JSON.
class SubnodeRegistry {
public:
    // Insert or update an existing subnode (same pubkey => update, not dup).
    void register_subnode(const Subnode& sub);
    void heartbeat(const std::string& pubkey, uint64_t now);
    void unregister(const std::string& pubkey);
    bool is_registered(const std::string& pubkey) const;
    size_t size() const { return by_pubkey_.size(); }
    // Mark subnodes inactive when their last heartbeat is older than
    // timeout_seconds.
    void prune_inactive(uint64_t now, uint64_t timeout_seconds);

    std::vector<Subnode> list() const;
    std::optional<Subnode> get(const std::string& pubkey) const;

    bool save(const std::string& path) const;
    bool load(const std::string& path);

private:
    std::unordered_map<std::string, Subnode> by_pubkey_;
};

}
