#include "lili-relay/relay_server.hpp"
#include "lili-relay/client_session.hpp"
#include "lili-relay/message_handler.hpp"
#include "lili-protocol/event.hpp"
#include "lili-protocol/signing.hpp"
#include "lili-protocol/subnode.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <algorithm>
#include <utility>
#include <nlohmann/json.hpp>

namespace lili {

namespace {

// Extract the 'd' tag value (identity for parameterized-replaceable events).
std::string d_tag_of(const std::vector<std::vector<std::string>>& tags) {
    for (const auto& tag : tags)
        if (tag.size() >= 2 && tag[0] == "d") return tag[1];
    return "";
}

std::string hex_encode_bytes(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    return oss.str();
}

// Match one StoredEvent against one NIP-01 filter.
bool filter_matches(const nlohmann::json& f, const StoredEvent& e) {
    if (f.contains("ids")) {
        bool ok = false;
        for (auto& id : f["ids"]) if (id.get<std::string>() == e.id) { ok = true; break; }
        if (!ok) return false;
    }
    if (f.contains("authors")) {
        bool ok = false;
        for (auto& a : f["authors"]) if (a.get<std::string>() == e.pubkey) { ok = true; break; }
        if (!ok) return false;
    }
    if (f.contains("kinds")) {
        bool ok = false;
        for (auto& k : f["kinds"]) if (k.get<int>() == e.kind) { ok = true; break; }
        if (!ok) return false;
    }
    if (f.contains("since") && e.created_at < f["since"].get<uint64_t>()) return false;
    if (f.contains("until") && e.created_at > f["until"].get<uint64_t>()) return false;

    // Tag filters: a key like "#e" must have a matching tag of that letter.
    for (auto& [key, vals] : f.items()) {
        if (key.size() == 2 && key[0] == '#') {
            char letter = key[1];
            bool found = false;
            for (const auto& tag : e.tags) {
                if (tag.empty() || tag[0].empty() || tag[0][0] != letter) continue;
                for (auto& v : vals) {
                    if (tag.size() >= 2 && v.get<std::string>() == tag[1]) { found = true; break; }
                }
                if (found) break;
            }
            if (!found) return false;
        }
    }
    return true;
}

} // namespace

RelayServer::RelayServer(uint16_t port, const RelayConfig& config)
    : port_(port), config_(config), running_(false) {
    if (config_.allowed_kinds.empty()) {
        config_.allowed_kinds.insert(1);
        config_.allowed_kinds.insert(42);
        config_.allowed_kinds.insert(30019);
        config_.allowed_kinds.insert(30020);
        config_.allowed_kinds.insert(30021);
        config_.allowed_kinds.insert(30022);
        config_.allowed_kinds.insert(30023);
        config_.allowed_kinds.insert(30079);
        config_.allowed_kinds.insert(30080);
        config_.allowed_kinds.insert(30090);
    }

    if (config_.kind_ttl.empty()) {
        config_.kind_ttl[30079] = 0;
        config_.kind_ttl[30080] = 0;
        config_.kind_ttl[30090] = 0;
        config_.kind_ttl[30019] = 86400 * 90;
        config_.kind_ttl[30020] = 86400 * 90;
        config_.kind_ttl[0] = 86400 * 30;
        config_.kind_ttl[42] = 86400 * 7;
        config_.kind_ttl[1] = 86400 * 7;
    }

    if (config_.master_mode) load_master_state();
}

// ---------------------------------------------------------------------------
// Master-authority state (identity keypair + subnode registry persistence).
// ---------------------------------------------------------------------------

std::string RelayServer::master_identity_path() const {
    return master_dir_ + "/identity.json";
}

std::string RelayServer::master_registry_path() const {
    return master_dir_ + "/registry.json";
}

std::string RelayServer::master_events_path() const {
    return master_dir_ + "/events.json";
}

size_t RelayServer::subnode_count() const {
    return registry_.size();
}

std::vector<Subnode> RelayServer::get_subnodes() const {
    return registry_.list();
}

std::vector<StoredEvent> RelayServer::get_events(
        const std::vector<uint16_t>& kinds, const std::string& author) const {
    std::vector<StoredEvent> out;
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        for (const auto& e : events_) {
            if (!kinds.empty() &&
                std::find(kinds.begin(), kinds.end(), e.kind) == kinds.end())
                continue;
            if (!author.empty() && e.pubkey != author) continue;
            out.push_back(e);
        }
    }
    return out;
}

// Persist the event store so the master remains the source of truth across
// restarts (subnodes can resync after a reboot).
void RelayServer::save_events() {
    if (!config_.master_mode) return;
    std::vector<StoredEvent> snapshot;
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        snapshot.assign(events_.begin(), events_.end());
    }
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : snapshot) {
        arr.push_back({
            {"id", e.id}, {"pubkey", e.pubkey}, {"kind", e.kind},
            {"content", e.content}, {"created_at", e.created_at}, {"sig", e.sig},
            {"tags", e.tags}
        });
    }
    std::ofstream f(master_events_path());
    if (!f) return;
    f << arr.dump(2);
}

void RelayServer::load_events() {
    if (!config_.master_mode) return;
    std::ifstream f(master_events_path());
    if (!f) return;
    try {
        auto arr = nlohmann::json::parse(f);
        std::lock_guard<std::mutex> lock(storage_mutex_);
        for (const auto& item : arr) {
            StoredEvent e;
            e.id = item.value("id", "");
            e.pubkey = item.value("pubkey", "");
            e.kind = item.value("kind", 0);
            e.content = item.value("content", "");
            e.created_at = item.value("created_at", 0ULL);
            e.sig = item.value("sig", "");
            e.size_bytes = 0;
            if (item.contains("tags"))
                e.tags = item["tags"].get<std::vector<std::vector<std::string>>>();
            events_.push_back(e);
        }
    } catch (...) {
        // Corrupt store: start empty rather than crash.
    }
}

void RelayServer::load_master_state() {
    master_dir_ = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.lili-master";
    std::filesystem::create_directories(master_dir_);

    // Load or generate the master's Ed25519 identity (used to sign ACKs and
    // advertise a stable pubkey in discovery).
    bool loaded = false;
    {
        std::ifstream f(master_identity_path());
        if (f) {
            try {
                auto j = nlohmann::json::parse(f);
                std::string priv = j.value("secret_key", "");
                std::string pub = j.value("public_key", "");
                if (priv.size() == 64 && pub.size() == 64) {
                    for (size_t i = 0; i < 32; ++i) {
                        char b[3] = {priv[i*2], priv[i*2+1], 0};
                        master_keypair_.secret_key[i] = static_cast<uint8_t>(std::strtoul(b, nullptr, 16));
                        char c[3] = {pub[i*2], pub[i*2+1], 0};
                        master_keypair_.public_key[i] = static_cast<uint8_t>(std::strtoul(c, nullptr, 16));
                    }
                    loaded = true;
                }
            } catch (...) {}
        }
    }
    if (!loaded) {
        master_keypair_ = generate_keypair();
        std::string priv = hex_encode_bytes(master_keypair_.secret_key.data(), 32);
        std::string pub = hex_encode_bytes(master_keypair_.public_key.data(), 32);
        std::ofstream of(master_identity_path());
        if (of) {
            nlohmann::json j = {{"public_key", pub}, {"secret_key", priv}};
            of << j.dump(2);
        }
    }

    registry_.load(master_registry_path());
    load_events();
}

void RelayServer::save_master_state() {
    if (!config_.master_mode) return;
    registry_.save(master_registry_path());
}

// Register a subnode (kind 30021). Replies to the registering session with a
// signed REGISTER_ACK (kind 30022) event.
void RelayServer::handle_register_event(std::shared_ptr<ClientSession> session,
                                        const std::string& pubkey,
                                        const StoredEvent& se,
                                        const std::string& ip) {
    if (!config_.master_mode) return;

    std::string display_name, hostname, distro, wm, passphrase;
    try {
        auto c = nlohmann::json::parse(se.content);
        display_name = c.value("display_name", "");
        hostname = c.value("hostname", "");
        distro = c.value("distro", "");
        wm = c.value("wm", "");
        passphrase = c.value("passphrase", "");
    } catch (...) {}

    bool ok = config_.registration_passphrase.empty() ||
              passphrase == config_.registration_passphrase;
    std::string msg = ok ? "registered" : "registration denied: wrong passphrase";

    if (ok) {
        Subnode sub;
        sub.pubkey = pubkey;
        sub.display_name = display_name.empty() ? pubkey.substr(0, 8) : display_name;
        sub.hostname = hostname;
        sub.distro = distro;
        sub.wm = wm;
        sub.ip = ip;
        sub.registered_at = se.created_at;
        sub.last_seen = se.created_at;
        sub.active = true;
        registry_.register_subnode(sub);
        save_master_state();
        std::cout << "Subnode registered: " << sub.display_name << " (" << pubkey.substr(0, 12) << "...)"
                  << " [" << ip << "] registry=" << registry_.size() << std::endl;
    }

    if (session) {
        auto ack = create_register_ack(pubkey, ok, msg, master_keypair_);
        nlohmann::json j;
        j["id"] = ack.id;
        j["pubkey"] = ack.pubkey;
        j["kind"] = ack.kind;
        j["content"] = ack.content;
        j["created_at"] = ack.created_at;
        j["tags"] = ack.tags;
        j["sig"] = ack.sig;
        nlohmann::json m = nlohmann::json::array();
        m.push_back("EVENT");
        m.push_back(ack.id);
        m.push_back(j);
        session->send_ws_text(m.dump());
    }
}

void RelayServer::handle_heartbeat_event(const StoredEvent& se) {
    if (!config_.master_mode) return;
    if (registry_.is_registered(se.pubkey)) {
        registry_.heartbeat(se.pubkey, se.created_at);
    }
}

RelayServer::~RelayServer() {
    stop();
}

void RelayServer::start() {
    running_ = true;
    accept_thread_ = std::thread([this]() { accept_connection(); });
    prune_thread_ = std::thread([this]() {
        int elapsed = 0;
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (++elapsed >= 1200) {  // ~300s
                prune_old_events();
                if (config_.master_mode) {
                    registry_.prune_inactive(time(nullptr), 120);
                }
                elapsed = 0;
            }
        }
    });

    if (config_.master_mode && !config_.loopback_only) {
        DiscoveryConfig dc;
        dc.name = config_.master_name.empty() ? "LinuxLive Master" : config_.master_name;
        dc.relay_port = port_;
        dc.master_pubkey = hex_encode_bytes(master_keypair_.public_key.data(), 32);
        dc.passphrase_required = !config_.registration_passphrase.empty();
        discovery_ = std::make_unique<DiscoveryResponder>(9042, std::move(dc));
        discovery_->start();
    }
}

void RelayServer::stop() {
    running_ = false;
    if (discovery_) discovery_->stop();
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& session : sessions_) {
            session->stop();
        }
        sessions_.clear();
    }
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

bool RelayServer::has_event(const std::string& event_id) const {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    for (const auto& e : events_) {
        if (e.id == event_id) return true;
    }
    return false;
}

bool RelayServer::is_parameterized_replaceable(uint16_t kind) const {
    return kind >= 30000 && kind <= 39999;
}

// NIP-01: parameterized-replaceable events (kinds 30000-39999) replace any
// prior event with the same (kind, pubkey, 'd' tag).
void RelayServer::replace_parameterized(const StoredEvent& event) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    std::string d = d_tag_of(event.tags);
    events_.erase(
        std::remove_if(events_.begin(), events_.end(),
            [&](const StoredEvent& e) {
                return e.kind == event.kind && e.pubkey == event.pubkey &&
                       d_tag_of(e.tags) == d;
            }),
        events_.end());
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

void RelayServer::handle_client_message(std::shared_ptr<ClientSession> session, const std::string& raw_msg) {
    nlohmann::json msg;
    try {
        msg = nlohmann::json::parse(raw_msg);
    } catch (...) {
        return;
    }

    if (!msg.is_array() || msg.size() < 2) return;

    std::string type = msg[0].get<std::string>();

    if (type == "EVENT" && msg.size() >= 2) {
        auto& event = msg[1];
        std::string pubkey = event.value("pubkey", "");
        uint16_t kind = event.value("kind", 0);
        std::string evid = event.value("id", "");

        auto send_ok = [&](bool success, const std::string& reason) {
            nlohmann::json ok = nlohmann::json::array();
            ok.push_back("OK");
            ok.push_back(evid);
            ok.push_back(success);
            ok.push_back(reason);
            session->send_ws_text(ok.dump());
        };

        // NIP-01 ingest validation: recompute the id and verify the signature.
        if (!event.is_object() || pubkey.size() != 64) {
            send_ok(false, "invalid: malformed event");
            return;
        }
        auto parsed = lili::Event::deserialize(event.dump());
        if (!parsed || !parsed->verify() || parsed->id != evid) {
            send_ok(false, "invalid: signature verification failed");
            return;
        }
        if (is_pubkey_banned(pubkey)) { send_ok(false, "blocked: pubkey is banned"); return; }
        if (!is_event_allowed(kind)) { send_ok(false, "blocked: kind not allowed"); return; }
        if (!check_rate_limit(pubkey)) { send_ok(false, "rate-limited: slow down"); return; }

        StoredEvent se;
        se.id = evid;
        se.pubkey = pubkey;
        se.kind = kind;
        se.content = event.value("content", "");
        se.created_at = event.value("created_at", 0);
        se.sig = event.value("sig", "");
        se.size_bytes = raw_msg.size();
        if (event.contains("tags")) {
            for (auto& tag : event["tags"]) {
                std::vector<std::string> t;
                for (auto& e : tag) t.push_back(e.get<std::string>());
                se.tags.push_back(t);
            }
        }

        // Control events handled only by a master. In a non-master relay these
        // fall through and are stored as ordinary events.
        if (config_.master_mode) {
            if (kind == static_cast<uint16_t>(lili::Event::Kind::REGISTER)) {
                handle_register_event(session, pubkey, se, session->peer_ip());
                send_ok(true, "registered");
                return;
            }
            if (kind == static_cast<uint16_t>(lili::Event::Kind::HEARTBEAT)) {
                handle_heartbeat_event(se);
                send_ok(true, "heartbeat");
                return;
            }
        }

        if (has_event(se.id)) {
            send_ok(true, "duplicate: already have this event");
            return;
        }
        if (is_parameterized_replaceable(se.kind)) {
            replace_parameterized(se);   // free the slot for the same (kind,pubkey,d)
        }
        if (!can_store_event(se)) { send_ok(false, "blocked: storage limits exceeded"); return; }

        store_event(se);
        record_event(pubkey);
        send_ok(true, "saved");
        if (config_.master_mode) save_events();

        // Copy under the lock, then send outside it (avoid holding the lock
        // during socket I/O and avoid the accept-thread data race).
        std::vector<std::shared_ptr<ClientSession>> targets;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            for (auto& s : sessions_) {
                if (s != session && s->is_websocket() && s->is_running()) {
                    targets.push_back(s);
                }
            }
        }
        for (auto& s : targets) {
            s->send_ws_text(raw_msg);
        }

    } else if (type == "REQ" && msg.size() >= 2) {
        std::string sub_id = msg[1].get<std::string>();

        std::vector<nlohmann::json> filters;
        if (msg.size() >= 3) {
            if (msg[2].is_array()) filters = msg[2].get<std::vector<nlohmann::json>>();
            else filters.push_back(msg[2]);
        } else {
            filters.push_back(nlohmann::json::object());
        }

        // Collect matches without holding the lock during I/O.
        std::vector<StoredEvent> results;
        {
            std::lock_guard<std::mutex> lock(storage_mutex_);
            for (const auto& e : events_) {
                for (const auto& f : filters) {
                    if (filter_matches(f, e)) { results.push_back(e); break; }
                }
            }
        }

        // NIP-01 limit: keep the newest N (tail of the chronological deque).
        // -1 means "not specified"; limit=0 explicitly means "return none".
        int limit = -1;
        for (const auto& f : filters)
            if (f.contains("limit")) limit = std::max(limit, f["limit"].get<int>());
        if (limit >= 0 && static_cast<int>(results.size()) > limit) {
            results.erase(results.begin(), results.begin() + (results.size() - limit));
        }

        for (const auto& e : results) {
            nlohmann::json ev;
            ev["id"] = e.id;
            ev["pubkey"] = e.pubkey;
            ev["kind"] = e.kind;
            ev["content"] = e.content;
            ev["created_at"] = e.created_at;
            ev["sig"] = e.sig;
            ev["tags"] = e.tags;

            nlohmann::json m = nlohmann::json::array();
            m.push_back("EVENT");
            m.push_back(sub_id);
            m.push_back(ev);
            session->send_ws_text(m.dump());
        }

        nlohmann::json eose = nlohmann::json::array();
        eose.push_back("EOSE");
        eose.push_back(sub_id);
        session->send_ws_text(eose.dump());

    } else if (type == "CLOSE" && msg.size() >= 2) {
    }
}

void RelayServer::accept_connection() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Make accept() return periodically so stop() can join the accept thread
    // instead of blocking on a socket that is never woken.
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;  // 200 ms
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = config_.loopback_only ? htonl(INADDR_LOOPBACK) : INADDR_ANY;
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
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int client_fd = accept(server_fd, (struct sockaddr*)&cli, &cli_len);
        if (client_fd >= 0) {
            auto session = std::make_shared<ClientSession>(client_fd);
            session->set_peer_ip(inet_ntoa(cli.sin_addr));
            session->set_on_message([this, session](const std::string& msg) {
                handle_client_message(session, msg);
            });
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                sessions_.push_back(session);
                sessions_.erase(
                    std::remove_if(sessions_.begin(), sessions_.end(),
                        [](const std::shared_ptr<ClientSession>& s) {
                            return !s->is_running();
                        }),
                    sessions_.end());
            }
            session->start();
        }
    }

    close(server_fd);
}

}
