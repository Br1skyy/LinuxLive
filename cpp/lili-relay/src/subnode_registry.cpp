#include "lili-relay/subnode_registry.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace lili {

void SubnodeRegistry::register_subnode(const Subnode& sub) {
    auto it = by_pubkey_.find(sub.pubkey);
    if (it != by_pubkey_.end()) {
        // Update in place (re-registration should not duplicate or reset history).
        Subnode& s = it->second;
        s.display_name = sub.display_name;
        s.hostname = sub.hostname;
        s.distro = sub.distro;
        s.wm = sub.wm;
        s.ip = sub.ip;
        s.last_seen = sub.last_seen;
        s.active = sub.active;
        return;
    }
    by_pubkey_[sub.pubkey] = sub;
}

void SubnodeRegistry::heartbeat(const std::string& pubkey, uint64_t now) {
    auto it = by_pubkey_.find(pubkey);
    if (it == by_pubkey_.end()) return;
    it->second.last_seen = now;
    it->second.active = true;
}

void SubnodeRegistry::unregister(const std::string& pubkey) {
    by_pubkey_.erase(pubkey);
}

void SubnodeRegistry::prune_inactive(uint64_t now, uint64_t timeout_seconds) {
    for (auto& [k, s] : by_pubkey_) {
        if (s.last_seen != 0 && now > s.last_seen &&
            (now - s.last_seen) > timeout_seconds) {
            s.active = false;
        }
    }
}

bool SubnodeRegistry::is_registered(const std::string& pubkey) const {
    return by_pubkey_.find(pubkey) != by_pubkey_.end();
}

std::vector<Subnode> SubnodeRegistry::list() const {
    std::vector<Subnode> out;
    out.reserve(by_pubkey_.size());
    for (const auto& [k, v] : by_pubkey_) out.push_back(v);
    return out;
}

std::optional<Subnode> SubnodeRegistry::get(const std::string& pubkey) const {
    auto it = by_pubkey_.find(pubkey);
    if (it == by_pubkey_.end()) return std::nullopt;
    return it->second;
}

bool SubnodeRegistry::save(const std::string& path) const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [k, s] : by_pubkey_) {
        arr.push_back({
            {"pubkey", s.pubkey},
            {"display_name", s.display_name},
            {"hostname", s.hostname},
            {"distro", s.distro},
            {"wm", s.wm},
            {"ip", s.ip},
            {"registered_at", s.registered_at},
            {"last_seen", s.last_seen},
            {"active", s.active}
        });
    }
    std::ofstream f(path);
    if (!f) return false;
    f << arr.dump(2);
    return f.good();
}

bool SubnodeRegistry::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    try {
        auto arr = nlohmann::json::parse(f);
        for (const auto& item : arr) {
            Subnode s;
            s.pubkey = item.value("pubkey", "");
            if (s.pubkey.empty()) continue;
            s.display_name = item.value("display_name", "");
            s.hostname = item.value("hostname", "");
            s.distro = item.value("distro", "");
            s.wm = item.value("wm", "");
            s.ip = item.value("ip", "");
            s.registered_at = item.value("registered_at", 0ULL);
            s.last_seen = item.value("last_seen", 0ULL);
            s.active = item.value("active", false);
            by_pubkey_[s.pubkey] = s;
        }
        return true;
    } catch (...) {
        return false;
    }
}

}
