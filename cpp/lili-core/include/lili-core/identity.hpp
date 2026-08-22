#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <optional>

namespace lili {

struct Identity {
    std::array<uint8_t, 32> public_key;
    std::array<uint8_t, 32> secret_key;
    std::string display_name;
    uint64_t created_at;
};

class IdentityManager {
public:
    IdentityManager();

    Identity generate();
    Identity generate(const std::string& display_name);

    std::optional<Identity> login(const std::string& private_key_hex);

    bool save(const Identity& id);
    std::optional<Identity> load();
    bool has_identity() const;
    bool clear();

    static std::string key_to_hex(const uint8_t* key, size_t len);
    static bool hex_to_key(const std::string& hex, uint8_t* out, size_t len);
    static std::string pubkey_hex(const Identity& id);
    static std::string privkey_hex(const Identity& id);

    static std::string data_dir();
    static void ensure_data_dir();

private:
    Identity current_;
    bool logged_in_ = false;

    std::string identity_path() const;
};

}
