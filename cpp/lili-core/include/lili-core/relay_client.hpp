#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>

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

    bool publish_event(const std::string& event_json);

    bool send_channel_message(const std::string& channel_id,
                              const std::string& content,
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

    using EventCallback = std::function<void(const RelayEvent&)>;
    void set_event_callback(EventCallback cb);
    using ConnectCallback = std::function<void(bool connected)>;
    void set_connect_callback(ConnectCallback cb);

private:
    struct RelayClientImpl;
    std::unique_ptr<RelayClientImpl> impl_;
};

}
