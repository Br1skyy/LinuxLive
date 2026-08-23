#pragma once

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
};

class RelayServer {
public:
    RelayServer(uint16_t port, const RelayConfig& config = RelayConfig());
    ~RelayServer();

    void start();
    void stop();
    bool is_running() const { return running_; }
    bool is_listening() const { return listening_; }

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

    const RelayConfig& config() const { return config_; }

private:
    void accept_connection();
    void handle_client_message(std::shared_ptr<ClientSession> session, const std::string& msg);

    uint16_t port_;
    RelayConfig config_;
    std::vector<std::shared_ptr<ClientSession>> sessions_;
    std::atomic<bool> running_;
    std::atomic<bool> listening_{false};
    std::atomic<int> listen_fd_{-1};
    std::thread accept_thread_;
    std::thread prune_thread_;

    std::deque<StoredEvent> events_;
    mutable std::mutex storage_mutex_;

    std::unordered_map<std::string, std::deque<uint64_t>> rate_limit_map_;
    mutable std::mutex rate_mutex_;
};

}
