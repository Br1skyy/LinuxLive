#include "lili-protocol/signing.hpp"
#include "lili-protocol/event.hpp"
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace lili {

KeyPair generate_keypair() {
    KeyPair kp;

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (!ctx) return kp;
    if (EVP_PKEY_keygen_init(ctx) != 1) { EVP_PKEY_CTX_free(ctx); return kp; }
    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(ctx, &pkey) != 1) { EVP_PKEY_CTX_free(ctx); return kp; }

    size_t len = 32;
    if (EVP_PKEY_get_raw_public_key(pkey, kp.public_key.data(), &len) != 1) {
        EVP_PKEY_free(pkey); EVP_PKEY_CTX_free(ctx); return kp;
    }
    len = 32;
    if (EVP_PKEY_get_raw_private_key(pkey, kp.secret_key.data(), &len) != 1) {
        EVP_PKEY_free(pkey); EVP_PKEY_CTX_free(ctx); return kp;
    }

    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    return kp;
}

// ---------------------------------------------------------------------------
// NIP-01 canonical serialization: the JSON array
//   [0, <pubkey>, <created_at>, <kind>, <tags>, <content>]
// serialized with NO whitespace. This exact byte string is what the event id
// hashes and what the Ed25519 signature covers.
// ---------------------------------------------------------------------------
static std::string canonical_serialization(const Event& event) {
    nlohmann::json j;
    j.push_back(0);
    j.push_back(event.pubkey);
    j.push_back(event.created_at);
    j.push_back(event.kind);
    j.push_back(event.tags);
    j.push_back(event.content);
    return j.dump();
}

static std::string hex_encode(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(data[i]);
    }
    return oss.str();
}

static std::string sha256_hex(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
    return hex_encode(hash, SHA256_DIGEST_LENGTH);
}

// NIP-01 event id = 64-char lowercase hex SHA-256 of the canonical serialization.
static std::string compute_event_id(const Event& event) {
    return sha256_hex(canonical_serialization(event));
}

static bool hex_decode(const std::string& hex, uint8_t* out, size_t expected) {
    if (hex.size() != expected * 2) return false;
    for (size_t i = 0; i < expected; ++i) {
        char byte[3] = {hex[i * 2], hex[i * 2 + 1], 0};
        out[i] = static_cast<uint8_t>(std::strtoul(byte, nullptr, 16));
    }
    return true;
}

Event sign_event(Event& event, const KeyPair& keypair) {
    event.pubkey = hex_encode(keypair.public_key.data(), 32);
    event.id = compute_event_id(event);

    std::string serialized = canonical_serialization(event);

    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                   keypair.secret_key.data(), 32);
    if (!pkey) return event;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    size_t siglen = 64;
    std::vector<unsigned char> sig(64);
    if (EVP_DigestSignInit(mdctx, nullptr, nullptr, nullptr, pkey) == 1) {
        if (EVP_DigestSign(mdctx, sig.data(), &siglen,
                           reinterpret_cast<const unsigned char*>(serialized.data()),
                           serialized.size()) == 1) {
            event.sig = hex_encode(sig.data(), siglen);
        }
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return event;
}

bool verify_signature(const Event& event) {
    if (event.pubkey.size() != 64) return false;
    if (event.sig.size() != 128) return false;
    if (event.id.empty()) return false;

    // 1) The id must be a genuine NIP-01 id: recompute it from the fields and
    //    require a match. This is what prevents content/tags tampering from
    //    slipping through with a still-valid signature.
    if (event.id != compute_event_id(event)) return false;

    // 2) Verify the Ed25519 signature over the canonical serialization.
    std::string serialized = canonical_serialization(event);

    unsigned char pubkey_bytes[32];
    unsigned char sig_bytes[64];
    if (!hex_decode(event.pubkey, pubkey_bytes, 32)) return false;
    if (!hex_decode(event.sig, sig_bytes, 64)) return false;

    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                                  pubkey_bytes, 32);
    if (!pkey) return false;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    int result = -1;
    if (EVP_DigestVerifyInit(mdctx, nullptr, nullptr, nullptr, pkey) == 1) {
        result = EVP_DigestVerify(mdctx, sig_bytes, 64,
                       reinterpret_cast<const unsigned char*>(serialized.data()),
                       serialized.size());
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return result == 1;
}

}
