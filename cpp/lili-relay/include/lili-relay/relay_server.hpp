#pragma once

#include "lili-relay/subnode_registry.hpp"
#include "lili-relay/discovery.hpp"
#include "lili-protocol/signing.hpp"
#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <cstdint>
#include <deque>
#include <chrono>

namespace lili {

class ClientSession;

struct StoredEvent {
    std::string id;
    std::string pubkey;
    uint16_t kind;
    std::string content;
    std::vector<std::vector<std::string>> tags;
    uint64_t created_at;
    std::string sig;
    size_t size_bytes;
};

struct RelayConfig {
    int rate_limit_events = 30;
    int rate_limit_window_seconds = 60;

    size_t max_events_per_pubkey = 100;
    size_t max_events_per_kind = 10000;
    size_t max_total_events = 100000;
    size_t max_event_size_bytes = 65536;

    int event_ttl_seconds = 86400 * 7;

    // kind -> TTL in seconds, 0 = never expire
    std::unordered_map<uint16_t, int> kind_ttl;

    std::unordered_set<uint16_t> allowed_kinds;
    std::unordered_set<std::string> banned_pubkeys;

    // Master (subnode-authority) mode.
    bool master_mode = false;
    std::string master_name;            // human name advertised in discovery
    std::string registration_passphrase; // empty = open registration

    // Hybrid mode: bind only to loopback and do NOT advertise via discovery,
    // so no other subnode can connect — the hub is private to this machine.
    bool loopback_only = false;
};

class RelayServer {
public:
    RelayServer(uint16_t port, const RelayConfig& config = RelayConfig());
    ~RelayServer();

    void start();
    void stop();
    bool is_running() const { return running_; }
    uint16_t port() const { return port_; }

    bool is_event_allowed(uint16_t kind) const;
    void allow_event_kind(uint16_t kind);
    bool is_pubkey_banned(const std::string& pubkey) const;
    void ban_pubkey(const std::string& pubkey);

    bool check_rate_limit(const std::string& pubkey);
    void record_event(const std::string& pubkey);

    bool can_store_event(const StoredEvent& event) const;
    void store_event(const StoredEvent& event);
    void prune_old_events();
    void delete_events_by_pubkey(const std::string& pubkey);
    void delete_event_by_id(const std::string& event_id);
    void print_stats() const;

    // NIP-01 helpers
    bool has_event(const std::string& event_id) const;
    bool is_parameterized_replaceable(uint16_t kind) const;
    void replace_parameterized(const StoredEvent& event);

    const RelayConfig& config() const { return config_; }

    // Update the hub's display name without restarting (used by hybrid UI).
    void set_master_name(const std::string& name) { config_.master_name = name; }

    // Master-dashboard accessors (only meaningful when master_mode).
    size_t subnode_count() const;
    std::vector<Subnode> get_subnodes() const;
    // Snapshot of stored events whose kind is in `kinds`, optionally restricted
    // to one author. Returns a copy safe to read off the relay thread.
    std::vector<StoredEvent> get_events(const std::vector<uint16_t>& kinds,
                                        const std::string& author = "") const;

private:
    void accept_connection();
    void handle_client_message(std::shared_ptr<ClientSession> session, const std::string& msg);

    uint16_t port_;
    RelayConfig config_;
    std::vector<std::shared_ptr<ClientSession>> sessions_;
    std::atomic<bool> running_;
    std::thread accept_thread_;
    std::thread prune_thread_;

    std::deque<StoredEvent> events_;
    mutable std::mutex storage_mutex_;

    // sessions_ is mutated from the accept thread and read when broadcasting,
    // so it needs its own lock.
    mutable std::mutex sessions_mutex_;

    std::unordered_map<std::string, std::deque<uint64_t>> rate_limit_map_;
    mutable std::mutex rate_mutex_;

    // Master-authority state (used when config_.master_mode).
    SubnodeRegistry registry_;
    KeyPair master_keypair_;
    std::string master_dir_;
    std::unique_ptr<DiscoveryResponder> discovery_;
    void handle_register_event(std::shared_ptr<ClientSession> session,
                               const std::string& subnode_pubkey, const StoredEvent& se,
                               const std::string& ip);
    void handle_heartbeat_event(const StoredEvent& se);
    void load_master_state();
    void save_master_state();
    void load_events();
    void save_events();
    std::string master_identity_path() const;
    std::string master_registry_path() const;
    std::string master_events_path() const;
};

}
