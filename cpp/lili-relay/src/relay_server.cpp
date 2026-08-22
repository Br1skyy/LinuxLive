#include "lili-relay/relay_server.hpp"
#include "lili-relay/client_session.hpp"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <algorithm>

namespace lili {

RelayServer::RelayServer(uint16_t port, const RelayConfig& config)
    : port_(port), config_(config), running_(false) {
    if (config_.allowed_kinds.empty()) {
        config_.allowed_kinds.insert(1);
        config_.allowed_kinds.insert(42);
        config_.allowed_kinds.insert(30019);
        config_.allowed_kinds.insert(30020);
        config_.allowed_kinds.insert(30079);
        config_.allowed_kinds.insert(30080);
    }

    if (config_.kind_ttl.empty()) {
        config_.kind_ttl[30079] = 0;
        config_.kind_ttl[30080] = 0;
        config_.kind_ttl[30019] = 86400 * 90;
        config_.kind_ttl[30020] = 86400 * 90;
        config_.kind_ttl[0] = 86400 * 30;
        config_.kind_ttl[42] = 86400 * 7;
        config_.kind_ttl[1] = 86400 * 7;
    }
}

RelayServer::~RelayServer() {
    stop();
}

void RelayServer::start() {
    running_ = true;
    accept_thread_ = std::thread([this]() { accept_connection(); });
    prune_thread_ = std::thread([this]() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(300));
            prune_old_events();
        }
    });
}

void RelayServer::stop() {
    running_ = false;
    for (auto& session : sessions_) {
        session->stop();
    }
    sessions_.clear();
    if (accept_thread_.joinable()) accept_thread_.join();
    if (prune_thread_.joinable()) prune_thread_.join();
}

bool RelayServer::is_event_allowed(uint16_t kind) const {
    return config_.allowed_kinds.find(kind) != config_.allowed_kinds.end();
}

void RelayServer::allow_event_kind(uint16_t kind) {
    config_.allowed_kinds.insert(kind);
}

bool RelayServer::is_pubkey_banned(const std::string& pubkey) const {
    return config_.banned_pubkeys.find(pubkey) != config_.banned_pubkeys.end();
}

void RelayServer::ban_pubkey(const std::string& pubkey) {
    config_.banned_pubkeys.insert(pubkey);
}

bool RelayServer::check_rate_limit(const std::string& pubkey) {
    std::lock_guard<std::mutex> lock(rate_mutex_);

    auto now = static_cast<uint64_t>(time(nullptr));
    auto& timestamps = rate_limit_map_[pubkey];

    while (!timestamps.empty() && timestamps.front() < now - config_.rate_limit_window_seconds) {
        timestamps.pop_front();
    }

    if ((int)timestamps.size() >= config_.rate_limit_events) {
        return false;
    }

    return true;
}

void RelayServer::record_event(const std::string& pubkey) {
    std::lock_guard<std::mutex> lock(rate_mutex_);
    rate_limit_map_[pubkey].push_back(static_cast<uint64_t>(time(nullptr)));
}

bool RelayServer::can_store_event(const StoredEvent& event) const {
    std::lock_guard<std::mutex> lock(storage_mutex_);

    if (event.size_bytes > config_.max_event_size_bytes) return false;
    if (events_.size() >= config_.max_total_events) return false;

    size_t kind_count = 0;
    for (const auto& e : events_) {
        if (e.kind == event.kind) kind_count++;
    }
    if (kind_count >= config_.max_events_per_kind) return false;

    size_t pubkey_count = 0;
    for (const auto& e : events_) {
        if (e.pubkey == event.pubkey) pubkey_count++;
    }
    if (pubkey_count >= config_.max_events_per_pubkey) return false;

    return true;
}

void RelayServer::store_event(const StoredEvent& event) {
    std::lock_guard<std::mutex> lock(storage_mutex_);

    while (events_.size() >= config_.max_total_events && !events_.empty()) {
        events_.pop_front();
    }

    events_.push_back(event);
}

void RelayServer::prune_old_events() {
    std::lock_guard<std::mutex> lock(storage_mutex_);

    auto now = static_cast<uint64_t>(time(nullptr));
    size_t before = events_.size();

    events_.erase(
        std::remove_if(events_.begin(), events_.end(),
            [&](const StoredEvent& e) {
                auto it = config_.kind_ttl.find(e.kind);
                int ttl = (it != config_.kind_ttl.end()) ? it->second : config_.event_ttl_seconds;

                if (ttl <= 0) return false;

                return (now - e.created_at) > static_cast<uint64_t>(ttl);
            }),
        events_.end());

    size_t after = events_.size();
    if (before != after) {
        std::cout << "Pruned " << (before - after) << " expired events ("
                  << after << " remaining)" << std::endl;
    }
}

void RelayServer::delete_events_by_pubkey(const std::string& pubkey) {
    std::lock_guard<std::mutex> lock(storage_mutex_);

    size_t before = events_.size();
    events_.erase(
        std::remove_if(events_.begin(), events_.end(),
            [&](const StoredEvent& e) {
                return e.pubkey == pubkey;
            }),
        events_.end());

    size_t deleted = before - events_.size();
    if (deleted > 0) {
        std::cout << "Deleted " << deleted << " events from banned pubkey: "
                  << pubkey.substr(0, 16) << "..." << std::endl;
    }
}

void RelayServer::delete_event_by_id(const std::string& event_id) {
    std::lock_guard<std::mutex> lock(storage_mutex_);

    size_t before = events_.size();
    events_.erase(
        std::remove_if(events_.begin(), events_.end(),
            [&](const StoredEvent& e) {
                return e.id == event_id;
            }),
        events_.end());

    if (before > events_.size()) {
        std::cout << "Deleted event: " << event_id.substr(0, 16) << "..." << std::endl;
    }
}

void RelayServer::print_stats() const {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    std::cout << "=== Relay Stats ===" << std::endl;
    std::cout << "Total events stored: " << events_.size() << "/" << config_.max_total_events << std::endl;

    std::unordered_map<int, int> kind_counts;
    std::unordered_map<std::string, int> pubkey_counts;
    for (const auto& e : events_) {
        kind_counts[e.kind]++;
        pubkey_counts[e.pubkey]++;
    }

    std::cout << "Events by kind:" << std::endl;
    for (auto& [kind, count] : kind_counts) {
        auto it = config_.kind_ttl.find(kind);
        int ttl = (it != config_.kind_ttl.end()) ? it->second : config_.event_ttl_seconds;
        std::string ttl_str = (ttl <= 0) ? "forever" : std::to_string(ttl) + "s";
        std::cout << "  Kind " << kind << ": " << count << " (TTL: " << ttl_str << ")" << std::endl;
    }

    std::cout << "Unique pubkeys: " << pubkey_counts.size() << std::endl;
    std::cout << "Rate-limited pubkeys: " << rate_limit_map_.size() << std::endl;
    std::cout << "Banned pubkeys: " << config_.banned_pubkeys.size() << std::endl;
    std::cout << "===================" << std::endl;
}

void RelayServer::accept_connection() {
    // TODO: upgrade from raw TCP to WebSocket - the Nostr spec expects WS
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind socket" << std::endl;
        close(server_fd);
        return;
    }

    if (listen(server_fd, 3) < 0) {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(server_fd);
        return;
    }

    std::cout << "Relay server listening on port " << port_ << std::endl;
    std::cout << "Rate limit: " << config_.rate_limit_events << " events / "
              << config_.rate_limit_window_seconds << "s per pubkey" << std::endl;
    std::cout << "Storage limit: " << config_.max_total_events << " events total, "
              << config_.max_events_per_pubkey << " per pubkey, "
              << config_.max_events_per_kind << " per kind" << std::endl;
    std::cout << "Event size limit: " << config_.max_event_size_bytes << " bytes" << std::endl;
    std::cout << "Event TTL: " << config_.event_ttl_seconds << " seconds" << std::endl;

    while (running_) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd >= 0) {
            auto session = std::make_shared<ClientSession>(client_fd);
            sessions_.push_back(session);
            session->start();
        }
    }

    close(server_fd);
}

void RelayServer::handle_client(std::shared_ptr<ClientSession> session) {
    session->start();
}

void RelayServer::cleanup_sessions() {
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
            [](const std::shared_ptr<ClientSession>& s) {
                return !s->is_running();
            }),
        sessions_.end());
}

}
