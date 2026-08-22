#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace lili {

class Encryption {
public:
    // NIP-04: AES-256-CBC with X25519 ECDH shared secret
    static std::string encrypt(
        const uint8_t sender_privkey[32],
        const uint8_t recipient_pubkey[32],
        const std::string& plaintext);

    static std::string decrypt(
        const uint8_t recipient_privkey[32],
        const uint8_t sender_pubkey[32],
        const std::string& ciphertext_base64);

    static std::array<uint8_t, 32> derive_shared_secret(
        const uint8_t privkey[32],
        const uint8_t pubkey[32]);

    static std::string base64_encode(const uint8_t* data, size_t len);
    static std::vector<uint8_t> base64_decode(const std::string& encoded);
};

}
