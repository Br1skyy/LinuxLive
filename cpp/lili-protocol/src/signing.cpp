#include "lili-protocol/signing.hpp"
#include "lili-protocol/event.hpp"
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>

namespace lili {

KeyPair generate_keypair() {
    KeyPair kp;
    RAND_bytes(kp.secret_key.data(), 32);

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_keygen(ctx, &pkey);

    size_t len = 32;
    EVP_PKEY_get_raw_public_key(pkey, kp.public_key.data(), &len);
    EVP_PKEY_get_raw_private_key(pkey, kp.secret_key.data(), &len);

    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);

    return kp;
}

static uint64_t compute_event_id(const Event& event) {
    nlohmann::json j;
    j.push_back(0);
    j.push_back(event.created_at);
    j.push_back(event.kind);
    j.push_back(event.tags);
    j.push_back(event.content);

    std::string serialized = j.dump();
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(serialized.data()), serialized.size(), hash);

    uint64_t id = 0;
    for (int i = 0; i < 8; ++i) {
        id = (id << 8) | hash[i];
    }
    return id;
}

Event sign_event(Event& event, const KeyPair& keypair) {
    event.id = compute_event_id(event);
    event.pubkey = [&keypair]() {
        std::ostringstream oss;
        for (size_t i = 0; i < 32; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(keypair.public_key[i]);
        }
        return oss.str();
    }();

    std::string serialized = std::to_string(event.id) + "\n" +
                            std::to_string(event.created_at) + "\n" +
                            std::to_string(event.kind) + "\n";

    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                   keypair.secret_key.data(), 32);

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestSignInit(mdctx, nullptr, nullptr, nullptr, pkey);

    size_t siglen = 64;
    std::vector<unsigned char> sig(siglen);
    EVP_DigestSign(mdctx, sig.data(), &siglen,
                   reinterpret_cast<const unsigned char*>(serialized.data()), serialized.size());

    std::ostringstream oss;
    for (size_t i = 0; i < siglen; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(sig[i]);
    }
    event.sig = oss.str();

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);

    return event;
}

bool verify_signature(const Event& event) {
    if (event.sig.empty() || event.pubkey.empty()) return false;

    if (event.pubkey.size() != 64) return false;
    unsigned char pubkey_bytes[32];
    for (int i = 0; i < 32; ++i) {
        char byte[3] = {event.pubkey[i*2], event.pubkey[i*2+1], 0};
        pubkey_bytes[i] = static_cast<unsigned char>(std::strtoul(byte, nullptr, 16));
    }

    // must match sign_event
    std::string serialized = std::to_string(event.id) + "\n" +
                            std::to_string(event.created_at) + "\n" +
                            std::to_string(event.kind) + "\n";

    if (event.sig.size() != 128) return false;
    unsigned char sig_bytes[64];
    for (int i = 0; i < 64; ++i) {
        char byte[3] = {event.sig[i*2], event.sig[i*2+1], 0};
        sig_bytes[i] = static_cast<unsigned char>(std::strtoul(byte, nullptr, 16));
    }

    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, pubkey_bytes, 32);
    if (!pkey) return false;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestVerifyInit(mdctx, nullptr, nullptr, nullptr, pkey);

    int result = EVP_DigestVerify(mdctx, sig_bytes, 64,
                   reinterpret_cast<const unsigned char*>(serialized.data()), serialized.size());

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);

    return result == 1;
}

}
