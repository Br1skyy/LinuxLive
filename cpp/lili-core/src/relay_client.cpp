#include "lili-core/relay_client.hpp"
#include "lili-core/ws_client.hpp"
#include "lili-protocol/signing.hpp"
#include "lili-protocol/subnode.hpp"
#include "lili-core/encryption.hpp"
#include "lili-core/identity.hpp"
#include <nlohmann/json.hpp>
#include <cstring>
#include <sstream>
#include <iostream>

namespace lili {

struct RelayClient::RelayClientImpl {
    WsClient ws;
    std::string url;
    RelayClient::EventCallback event_cb;
    RelayClient::ConnectCallback connect_cb;
    RelayClient::RegisterAckCallback register_ack_cb;
    std::string sub_id;
};

RelayClient::RelayClient() : impl_(std::make_unique<RelayClientImpl>()) {}
RelayClient::~RelayClient() { disconnect(); }

void RelayClient::set_event_callback(EventCallback cb) { impl_->event_cb = cb; }
void RelayClient::set_connect_callback(ConnectCallback cb) { impl_->connect_cb = cb; }
void RelayClient::set_register_ack_callback(RegisterAckCallback cb) { impl_->register_ack_cb = cb; }

bool RelayClient::connect(const std::string& url, bool tor_proxy) {
    impl_->url = url;

    impl_->ws.set_on_message([this](const std::string& msg) {
        try {
            auto j = nlohmann::json::parse(msg);
            if (j.is_array() && j.size() >= 2) {
                std::string type = j[0].get<std::string>();
                if (type == "EVENT" && j.size() >= 3) {
                    auto& ev = j[2];
                    RelayEvent re;
                    re.id = ev.value("id", "");
                    re.pubkey = ev.value("pubkey", "");
                    re.kind = ev.value("kind", 0);
                    re.content = ev.value("content", "");
                    re.created_at = ev.value("created_at", 0);
                    re.sig = ev.value("sig", "");
                    if (ev.contains("tags")) {
                        for (auto& tag : ev["tags"]) {
                            std::vector<std::string> t;
                            for (auto& e : tag) t.push_back(e.get<std::string>());
                            re.tags.push_back(t);
                        }
                    }
                    if (impl_->event_cb) impl_->event_cb(re);

                    if (re.kind == static_cast<int>(lili::Event::Kind::REGISTER_ACK)) {
                        bool accepted = false;
                        std::string ack_msg;
                        try {
                            auto c = nlohmann::json::parse(re.content);
                            accepted = c.value("status", "") == "accepted";
                            ack_msg = c.value("message", "");
                        } catch (...) {}
                        if (impl_->register_ack_cb) impl_->register_ack_cb(accepted, ack_msg);
                    }
                }
            }
        } catch (...) {}
    });

    impl_->ws.set_on_close([this]() {
        if (impl_->connect_cb) impl_->connect_cb(false);
    });

    impl_->ws.set_on_error([this](const std::string& err) {
        std::cerr << "WebSocket error: " << err << std::endl;
    });

    bool ok = impl_->ws.connect(url, tor_proxy);
    if (impl_->connect_cb) impl_->connect_cb(ok);
    return ok;
}

void RelayClient::disconnect() {
    impl_->ws.disconnect();
    if (impl_->connect_cb) impl_->connect_cb(false);
}

bool RelayClient::is_connected() const {
    return impl_->ws.is_connected();
}

void RelayClient::subscribe(const std::vector<int>& kinds,
                             const std::string& author_pubkey,
                             const std::string& event_id) {
    nlohmann::json filter = nlohmann::json::object();
    if (!kinds.empty()) filter["kinds"] = kinds;
    if (!author_pubkey.empty()) filter["authors"] = nlohmann::json::array({author_pubkey});
    if (!event_id.empty()) filter["ids"] = nlohmann::json::array({event_id});

    impl_->sub_id = "sub_" + std::to_string(time(nullptr));
    nlohmann::json req = nlohmann::json::array();
    req.push_back("REQ");
    req.push_back(impl_->sub_id);
    req.push_back(filter);

    impl_->ws.send_text(req.dump());
}

void RelayClient::subscribe_tag(const std::vector<int>& kinds,
                                const char* tag,
                                const std::string& tag_value) {
    nlohmann::json filter = nlohmann::json::object();
    if (!kinds.empty()) filter["kinds"] = kinds;
    filter[std::string("#") + tag] = nlohmann::json::array({tag_value});

    impl_->sub_id = "sub_" + std::to_string(time(nullptr));
    nlohmann::json req = nlohmann::json::array();
    req.push_back("REQ");
    req.push_back(impl_->sub_id);
    req.push_back(filter);

    impl_->ws.send_text(req.dump());
}

bool RelayClient::publish_event(const std::string& event_json) {
    nlohmann::json msg = nlohmann::json::array();
    msg.push_back("EVENT");
    try {
        msg.push_back(nlohmann::json::parse(event_json));
    } catch (...) {
        return false;
    }
    return impl_->ws.send_text(msg.dump());
}

bool RelayClient::send_channel_message(const std::string& channel_id,
                                        const std::string& content,
                                        const std::string& sender_privkey,
                                        const std::string& sender_pubkey) {
    uint8_t privkey[32], pubkey[32];
    if (!IdentityManager::hex_to_key(sender_privkey, privkey, 32)) return false;
    if (!IdentityManager::hex_to_key(sender_pubkey, pubkey, 32)) return false;

    lili::Event native_event;
    native_event.id.clear();
    native_event.created_at = static_cast<uint64_t>(time(nullptr));
    native_event.kind = 42;
    native_event.content = content;
    native_event.pubkey = sender_pubkey;
    native_event.tags = {{"e", channel_id}};

    lili::KeyPair kp;
    memcpy(kp.secret_key.data(), privkey, 32);
    memcpy(kp.public_key.data(), pubkey, 32);

    auto signed_event = lili::sign_event(native_event, kp);

    nlohmann::json j;
    j["id"] = signed_event.id;
    j["pubkey"] = signed_event.pubkey;
    j["kind"] = signed_event.kind;
    j["content"] = signed_event.content;
    j["created_at"] = signed_event.created_at;
    j["tags"] = signed_event.tags;
    j["sig"] = signed_event.sig;

    return publish_event(j.dump());
}

std::string RelayClient::publish_room(const std::string& name,
                                      const std::string& sender_privkey,
                                      const std::string& sender_pubkey) {
    uint8_t privkey[32], pubkey[32];
    if (!IdentityManager::hex_to_key(sender_privkey, privkey, 32)) return "";
    if (!IdentityManager::hex_to_key(sender_pubkey, pubkey, 32)) return "";

    lili::Event native_event;
    native_event.id.clear();
    native_event.created_at = static_cast<uint64_t>(time(nullptr));
    native_event.kind = static_cast<uint16_t>(lili::Event::Kind::NODE);
    native_event.content = name;
    native_event.pubkey = sender_pubkey;
    native_event.tags = {{"name", name}};

    lili::KeyPair kp;
    memcpy(kp.secret_key.data(), privkey, 32);
    memcpy(kp.public_key.data(), pubkey, 32);

    auto signed_event = lili::sign_event(native_event, kp);

    nlohmann::json j;
    j["id"] = signed_event.id;
    j["pubkey"] = signed_event.pubkey;
    j["kind"] = signed_event.kind;
    j["content"] = signed_event.content;
    j["created_at"] = signed_event.created_at;
    j["tags"] = signed_event.tags;
    j["sig"] = signed_event.sig;

    if (!publish_event(j.dump())) return "";
    return signed_event.id;
}

bool RelayClient::send_stats(const SystemStats& stats,
                             const std::string& sender_privkey,
                             const std::string& sender_pubkey) {
    uint8_t privkey[32], pubkey[32];
    if (!IdentityManager::hex_to_key(sender_privkey, privkey, 32)) return false;
    if (!IdentityManager::hex_to_key(sender_pubkey, pubkey, 32)) return false;

    nlohmann::json c;
    c["hostname"] = stats.hostname;
    c["distro"] = stats.distro;
    c["kernel"] = stats.kernel;
    c["cpu"] = stats.cpu;
    c["cores"] = stats.cores;
    c["mem_total_mb"] = stats.mem_total_mb;
    c["mem_used_mb"] = stats.mem_used_mb;
    c["uptime_seconds"] = stats.uptime_seconds;
    c["commands"] = stats.commands;
    c["ach_unlocked"] = stats.ach_unlocked;

    lili::Event native_event;
    native_event.id.clear();
    native_event.created_at = static_cast<uint64_t>(time(nullptr));
    native_event.kind = static_cast<uint16_t>(lili::Event::Kind::STATS);
    native_event.content = c.dump();
    native_event.pubkey = sender_pubkey;
    native_event.tags = {};

    lili::KeyPair kp;
    memcpy(kp.secret_key.data(), privkey, 32);
    memcpy(kp.public_key.data(), pubkey, 32);

    auto signed_event = lili::sign_event(native_event, kp);

    nlohmann::json j;
    j["id"] = signed_event.id;
    j["pubkey"] = signed_event.pubkey;
    j["kind"] = signed_event.kind;
    j["content"] = signed_event.content;
    j["created_at"] = signed_event.created_at;
    j["tags"] = signed_event.tags;
    j["sig"] = signed_event.sig;

    return publish_event(j.dump());
}

bool RelayClient::send_dm(const std::string& recipient_pubkey,
                           const std::string& content,
                           const std::string& sender_privkey,
                           const std::string& sender_pubkey) {
    uint8_t privkey[32], pubkey[32], recip_key[32];
    if (!IdentityManager::hex_to_key(sender_privkey, privkey, 32)) return false;
    if (!IdentityManager::hex_to_key(sender_pubkey, pubkey, 32)) return false;
    if (!IdentityManager::hex_to_key(recipient_pubkey, recip_key, 32)) return false;

    std::string encrypted = Encryption::encrypt(privkey, recip_key, content);

    lili::Event native_event;
    native_event.id.clear();
    native_event.created_at = static_cast<uint64_t>(time(nullptr));
    native_event.kind = 4;
    native_event.content = encrypted;
    native_event.pubkey = sender_pubkey;
    native_event.tags = {{"p", recipient_pubkey}};

    lili::KeyPair kp;
    memcpy(kp.secret_key.data(), privkey, 32);
    memcpy(kp.public_key.data(), pubkey, 32);

    auto signed_event = lili::sign_event(native_event, kp);

    nlohmann::json j;
    j["id"] = signed_event.id;
    j["pubkey"] = signed_event.pubkey;
    j["kind"] = signed_event.kind;
    j["content"] = signed_event.content;
    j["created_at"] = signed_event.created_at;
    j["tags"] = signed_event.tags;
    j["sig"] = signed_event.sig;

    return publish_event(j.dump());
}

bool RelayClient::send_profile(const std::string& display_name,
                               const std::string& bio,
                               const std::string& sender_privkey,
                               const std::string& sender_pubkey) {
    uint8_t privkey[32], pubkey[32];
    if (!IdentityManager::hex_to_key(sender_privkey, privkey, 32)) return false;
    if (!IdentityManager::hex_to_key(sender_pubkey, pubkey, 32)) return false;

    nlohmann::json content;
    content["name"] = display_name;
    content["about"] = bio;

    lili::Event native_event;
    native_event.id.clear();
    native_event.created_at = static_cast<uint64_t>(time(nullptr));
    native_event.kind = 0;
    native_event.content = content.dump();
    native_event.pubkey = sender_pubkey;

    lili::KeyPair kp;
    memcpy(kp.secret_key.data(), privkey, 32);
    memcpy(kp.public_key.data(), pubkey, 32);

    auto signed_event = lili::sign_event(native_event, kp);

    nlohmann::json j;
    j["id"] = signed_event.id;
    j["pubkey"] = signed_event.pubkey;
    j["kind"] = signed_event.kind;
    j["content"] = signed_event.content;
    j["created_at"] = signed_event.created_at;
    j["tags"] = signed_event.tags;
    j["sig"] = signed_event.sig;

    return publish_event(j.dump());
}

bool RelayClient::send_achievement(const std::string& achievement_id,
                                   const std::string& name,
                                   const std::string& description,
                                   const std::string& icon,
                                   const std::string& sender_privkey,
                                   const std::string& sender_pubkey) {
    uint8_t privkey[32], pubkey[32];
    if (!IdentityManager::hex_to_key(sender_privkey, privkey, 32)) return false;
    if (!IdentityManager::hex_to_key(sender_pubkey, pubkey, 32)) return false;

    nlohmann::json content;
    content["achievement_id"] = achievement_id;
    content["name"] = name;
    content["description"] = description;
    content["icon"] = icon;

    lili::Event native_event;
    native_event.id.clear();
    native_event.created_at = static_cast<uint64_t>(time(nullptr));
    native_event.kind = 30079;
    native_event.content = content.dump();
    native_event.pubkey = sender_pubkey;
    native_event.tags = {
        {"d", achievement_id},
        {"achievement", achievement_id}
    };

    lili::KeyPair kp;
    memcpy(kp.secret_key.data(), privkey, 32);
    memcpy(kp.public_key.data(), pubkey, 32);

    auto signed_event = lili::sign_event(native_event, kp);

    nlohmann::json j;
    j["id"] = signed_event.id;
    j["pubkey"] = signed_event.pubkey;
    j["kind"] = signed_event.kind;
    j["content"] = signed_event.content;
    j["created_at"] = signed_event.created_at;
    j["tags"] = signed_event.tags;
    j["sig"] = signed_event.sig;

    return publish_event(j.dump());
}

bool RelayClient::register_subnode(const std::string& display_name,
                                   const std::string& hostname,
                                   const std::string& distro,
                                   const std::string& wm,
                                   const std::string& passphrase,
                                   const std::string& sender_privkey,
                                   const std::string& sender_pubkey) {
    uint8_t privkey[32], pubkey[32];
    if (!IdentityManager::hex_to_key(sender_privkey, privkey, 32)) return false;
    if (!IdentityManager::hex_to_key(sender_pubkey, pubkey, 32)) return false;

    lili::KeyPair kp;
    memcpy(kp.secret_key.data(), privkey, 32);
    memcpy(kp.public_key.data(), pubkey, 32);

    lili::RegistrationInfo info;
    info.display_name = display_name;
    info.hostname = hostname;
    info.distro = distro;
    info.wm = wm;
    info.client_version = "0.1.0";

    auto signed_event = lili::create_register_event(info, passphrase, kp);

    nlohmann::json j;
    j["id"] = signed_event.id;
    j["pubkey"] = signed_event.pubkey;
    j["kind"] = signed_event.kind;
    j["content"] = signed_event.content;
    j["created_at"] = signed_event.created_at;
    j["tags"] = signed_event.tags;
    j["sig"] = signed_event.sig;

    return publish_event(j.dump());
}

bool RelayClient::send_heartbeat(uint64_t uptime_seconds,
                                 uint32_t achievements_unlocked,
                                 const std::string& sender_privkey,
                                 const std::string& sender_pubkey) {
    uint8_t privkey[32], pubkey[32];
    if (!IdentityManager::hex_to_key(sender_privkey, privkey, 32)) return false;
    if (!IdentityManager::hex_to_key(sender_pubkey, pubkey, 32)) return false;

    lili::KeyPair kp;
    memcpy(kp.secret_key.data(), privkey, 32);
    memcpy(kp.public_key.data(), pubkey, 32);

    auto signed_event = lili::create_heartbeat_event(uptime_seconds, achievements_unlocked, kp);

    nlohmann::json j;
    j["id"] = signed_event.id;
    j["pubkey"] = signed_event.pubkey;
    j["kind"] = signed_event.kind;
    j["content"] = signed_event.content;
    j["created_at"] = signed_event.created_at;
    j["tags"] = signed_event.tags;
    j["sig"] = signed_event.sig;

    return publish_event(j.dump());
}

bool RelayClient::sync_achievements(const std::string& self_pubkey) {
    if (self_pubkey.empty()) return false;
    subscribe({static_cast<int>(lili::Event::Kind::ACHIEVEMENT)},
              self_pubkey, "");
    return true;
}

}
