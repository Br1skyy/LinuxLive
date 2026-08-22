#include "lili-core/encryption.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <cstring>

namespace lili {

std::array<uint8_t, 32> Encryption::derive_shared_secret(
    const uint8_t privkey[32], const uint8_t pubkey[32]) {
    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, privkey, 32);
    EVP_PKEY* peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, pubkey, 32);

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    EVP_PKEY_derive_init(ctx);
    EVP_PKEY_derive_set_peer(ctx, peer);

    size_t len = 32;
    std::array<uint8_t, 32> secret;
    EVP_PKEY_derive(ctx, secret.data(), &len);

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    EVP_PKEY_free(peer);

    // hash with SHA-256 for NIP-04 compatibility
    std::array<uint8_t, 32> hashed;
    SHA256(secret.data(), 32, hashed.data());
    return hashed;
}

std::string Encryption::encrypt(
    const uint8_t sender_privkey[32],
    const uint8_t recipient_pubkey[32],
    const std::string& plaintext) {
    auto shared = derive_shared_secret(sender_privkey, recipient_pubkey);

    uint8_t iv[16];
    RAND_bytes(iv, 16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, shared.data(), iv);

    std::vector<uint8_t> ciphertext(plaintext.size() + 32);
    int out_len = 0, total = 0;
    EVP_EncryptUpdate(ctx, ciphertext.data(), &out_len,
                      reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size());
    total = out_len;
    EVP_EncryptFinal_ex(ctx, ciphertext.data() + total, &out_len);
    total += out_len;
    EVP_CIPHER_CTX_free(ctx);

    ciphertext.resize(total);

    // format: iv (16 bytes) + ciphertext
    std::vector<uint8_t> result(16 + total);
    memcpy(result.data(), iv, 16);
    memcpy(result.data() + 16, ciphertext.data(), total);

    return base64_encode(result.data(), result.size());
}

std::string Encryption::decrypt(
    const uint8_t recipient_privkey[32],
    const uint8_t sender_pubkey[32],
    const std::string& ciphertext_base64) {
    auto shared = derive_shared_secret(recipient_privkey, sender_pubkey);

    auto decoded = base64_decode(ciphertext_base64);
    if (decoded.size() < 16) return "";

    uint8_t* iv = decoded.data();
    uint8_t* ciphertext = decoded.data() + 16;
    size_t ciphertext_len = decoded.size() - 16;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, shared.data(), iv);

    std::vector<uint8_t> plaintext(ciphertext_len + 32);
    int out_len = 0, total = 0;
    EVP_DecryptUpdate(ctx, plaintext.data(), &out_len, ciphertext, ciphertext_len);
    total = out_len;
    EVP_DecryptFinal_ex(ctx, plaintext.data() + total, &out_len);
    total += out_len;
    EVP_CIPHER_CTX_free(ctx);

    return std::string(reinterpret_cast<char*>(plaintext.data()), total);
}

std::string Encryption::base64_encode(const uint8_t* data, size_t len) {
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, data, len);
    BIO_flush(bio);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(bio, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(bio);
    return result;
}

std::vector<uint8_t> Encryption::base64_decode(const std::string& encoded) {
    BIO* bio = BIO_new_mem_buf(encoded.data(), encoded.size());
    BIO* b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    std::vector<uint8_t> result(encoded.size());
    int len = BIO_read(bio, result.data(), result.size());
    BIO_free_all(bio);

    if (len > 0) result.resize(len);
    else result.clear();
    return result;
}

}
