#include "lili-core/identity.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cstring>

namespace lili {

static const char* DATA_DIR_NAME = ".lili";

IdentityManager::IdentityManager() {}

std::string IdentityManager::data_dir() {
    const char* home = getenv("HOME");
    if (!home) home = getenv("USER");
    return std::string(home ? home : "/tmp") + "/" + DATA_DIR_NAME;
}

void IdentityManager::ensure_data_dir() {
    std::filesystem::create_directories(data_dir());
}

std::string IdentityManager::identity_path() const {
    return data_dir() + "/identity.json";
}

bool IdentityManager::has_identity() const {
    return std::filesystem::exists(const_cast<IdentityManager*>(this)->identity_path());
}

std::string IdentityManager::key_to_hex(const uint8_t* key, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(key[i]);
    return oss.str();
}

bool IdentityManager::hex_to_key(const std::string& hex, uint8_t* out, size_t len) {
    if (hex.size() != len * 2) return false;
    for (size_t i = 0; i < len; ++i) {
        char byte[3] = {hex[i*2], hex[i*2+1], 0};
        out[i] = static_cast<uint8_t>(std::strtoul(byte, nullptr, 16));
    }
    return true;
}

std::string IdentityManager::pubkey_hex(const Identity& id) {
    return key_to_hex(id.public_key.data(), 32);
}

std::string IdentityManager::privkey_hex(const Identity& id) {
    return key_to_hex(id.secret_key.data(), 32);
}

Identity IdentityManager::generate() {
    return generate("User");
}

Identity IdentityManager::generate(const std::string& display_name) {
    Identity id;

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_keygen(ctx, &pkey);

    size_t len = 32;
    EVP_PKEY_get_raw_public_key(pkey, id.public_key.data(), &len);
    len = 32;
    EVP_PKEY_get_raw_private_key(pkey, id.secret_key.data(), &len);

    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);

    id.display_name = display_name;
    id.created_at = static_cast<uint64_t>(time(nullptr));
    current_ = id;
    logged_in_ = true;

    return id;
}

std::optional<Identity> IdentityManager::login(const std::string& private_key_hex) {
    Identity id;

    if (!hex_to_key(private_key_hex, id.secret_key.data(), 32)) {
        return std::nullopt;
    }

    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                    id.secret_key.data(), 32);
    if (!pkey) return std::nullopt;

    size_t len = 32;
    EVP_PKEY_get_raw_public_key(pkey, id.public_key.data(), &len);
    EVP_PKEY_free(pkey);

    id.display_name = "User";
    id.created_at = static_cast<uint64_t>(time(nullptr));

    ensure_data_dir();
    auto loaded = load();
    if (loaded && loaded->public_key == id.public_key) {
        id.display_name = loaded->display_name;
        id.created_at = loaded->created_at;
    }

    current_ = id;
    logged_in_ = true;
    return id;
}

// XOR obfuscation with SHA-256 derived pad - not real encryption, just prevents plaintext on disk
static void obfuscate(uint8_t* data, size_t len, const uint8_t* key, size_t key_len) {
    for (size_t i = 0; i < len; ++i)
        data[i] ^= key[i % key_len];
}

bool IdentityManager::save(const Identity& id) {
    ensure_data_dir();

    uint8_t pad[32];
    SHA256(id.secret_key.data(), 32, pad);

    uint8_t enc_priv[32];
    memcpy(enc_priv, id.secret_key.data(), 32);
    obfuscate(enc_priv, 32, pad, 32);

    std::ofstream f(identity_path());
    if (!f) return false;

    f << "{\n";
    f << "  \"pubkey\": \"" << key_to_hex(id.public_key.data(), 32) << "\",\n";
    f << "  \"enc_privkey\": \"" << key_to_hex(enc_priv, 32) << "\",\n";
    f << "  \"obf_pad\": \"" << key_to_hex(pad, 32) << "\",\n";
    f << "  \"display_name\": \"" << id.display_name << "\",\n";
    f << "  \"created_at\": " << id.created_at << "\n";
    f << "}\n";

    return f.good();
}

std::optional<Identity> IdentityManager::load() {
    std::ifstream f(identity_path());
    if (!f) return std::nullopt;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    Identity id;

    auto extract = [&](const char* key) -> std::string {
        std::string search = std::string("\"") + key + "\"";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return "";
        pos = content.find("\"", pos + search.size());
        if (pos == std::string::npos) return "";
        pos++;
        size_t end = content.find("\"", pos);
        if (end == std::string::npos) return "";
        return content.substr(pos, end - pos);
    };

    auto extract_num = [&](const char* key) -> uint64_t {
        std::string search = std::string("\"") + key + "\"";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return 0;
        pos = content.find(":", pos + search.size());
        if (pos == std::string::npos) return 0;
        pos++;
        while (pos < content.size() && content[pos] == ' ') pos++;
        std::string num;
        while (pos < content.size() && std::isdigit(content[pos])) {
            num += content[pos++];
        }
        return std::strtoull(num.c_str(), nullptr, 10);
    };

    std::string pub_hex = extract("pubkey");
    std::string priv_hex = extract("enc_privkey");
    std::string pad_hex = extract("obf_pad");

    if (pub_hex.empty() || priv_hex.empty()) return std::nullopt;

    if (!hex_to_key(pub_hex, id.public_key.data(), 32)) return std::nullopt;
    if (!hex_to_key(priv_hex, id.secret_key.data(), 32)) return std::nullopt;

    uint8_t pad[32];
    if (!pad_hex.empty() && hex_to_key(pad_hex, pad, 32)) {
        obfuscate(id.secret_key.data(), 32, pad, 32);
    }

    id.display_name = extract("display_name");
    id.created_at = extract_num("created_at");

    current_ = id;
    logged_in_ = true;
    return id;
}

bool IdentityManager::clear() {
    if (std::filesystem::exists(identity_path())) {
        return std::filesystem::remove(identity_path());
    }
    return true;
}

}
