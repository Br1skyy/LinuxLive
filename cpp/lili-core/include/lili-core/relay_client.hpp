#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>

#include "lili-core/system_stats.hpp"

namespace lili {

struct RelayEvent {
    std::string id;
    std::string pubkey;
    int kind;
    std::string content;
    std::vector<std::vector<std::string>> tags;
    uint64_t created_at;
    std::string sig;
};

class RelayClient {
public:
    RelayClient();
    ~RelayClient();

    bool connect(const std::string& url, bool tor_proxy = false);
    // TODO: auto-reconnect with exponential backoff on disconnect
    void disconnect();
    bool is_connected() const;

    void subscribe(const std::vector<int>& kinds = {},
                   const std::string& author_pubkey = "",
                   const std::string& event_id = "");

    // Subscribe with a single-letter tag filter, e.g. subscribe_tag({42}, "e",
    // node_id) for a channel's messages.
    void subscribe_tag(const std::vector<int>& kinds,
                       const char* tag,
                       const std::string& tag_value);

    bool publish_event(const std::string& event_json);

    bool send_channel_message(const std::string& channel_id,
                              const std::string& content,
                              const std::string& sender_privkey,
                              const std::string& sender_pubkey);

    // Publish a chat-room announcement (kind NODE) to the relay; returns the
    // room id (the signed event id). Rooms are hosted on the master/relay, not
    // locally, so this is how a subnode adds a room.
    std::string publish_room(const std::string& name,
                             const std::string& sender_privkey,
                             const std::string& sender_pubkey);

    // Publish this subnode's system/terminal stats (kind STATS) to the relay.
    bool send_stats(const SystemStats& stats,
                    const std::string& sender_privkey,
                    const std::string& sender_pubkey);

    bool send_dm(const std::string& recipient_pubkey,
                 const std::string& content,
                 const std::string& sender_privkey,
                 const std::string& sender_pubkey);

    bool send_profile(const std::string& display_name,
                      const std::string& bio,
                      const std::string& sender_privkey,
                      const std::string& sender_pubkey);

    bool send_achievement(const std::string& achievement_id,
                          const std::string& name,
                          const std::string& description,
                          const std::string& icon,
                          const std::string& sender_privkey,
                          const std::string& sender_pubkey);

    // Master/subnode operations.
    bool register_subnode(const std::string& display_name,
                          const std::string& hostname,
                          const std::string& distro,
                          const std::string& wm,
                          const std::string& passphrase,
                          const std::string& sender_privkey,
                          const std::string& sender_pubkey);
    bool send_heartbeat(uint64_t uptime_seconds,
                        uint32_t achievements_unlocked,
                        const std::string& sender_privkey,
                        const std::string& sender_pubkey);
    // Pull this subnode's own achievements from the relay (kinds 30079).
    bool sync_achievements(const std::string& self_pubkey);

    using EventCallback = std::function<void(const RelayEvent&)>;
    void set_event_callback(EventCallback cb);
    using ConnectCallback = std::function<void(bool connected)>;
    void set_connect_callback(ConnectCallback cb);
    using RegisterAckCallback = std::function<void(bool accepted, const std::string& message)>;
    void set_register_ack_callback(RegisterAckCallback cb);

private:
    struct RelayClientImpl;
    std::unique_ptr<RelayClientImpl> impl_;
};

}
