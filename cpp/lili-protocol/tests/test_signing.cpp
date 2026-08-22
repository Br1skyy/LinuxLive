#include "lili-protocol/signing.hpp"
#include "lili-protocol/event.hpp"
#include <openssl/evp.h>
#include <cassert>
#include <iostream>
#include <regex>

static void assert_64hex(const std::string& s, const char* what) {
    assert(s.size() == 64);
    assert(std::regex_match(s, std::regex("[0-9a-f]{64}")));
    std::cout << "  " << what << " is 64 lowercase hex chars\n";
}

void test_keypair_generation() {
    auto kp = lili::generate_keypair();
    assert(kp.secret_key.size() == 32);
    assert(kp.public_key.size() == 32);
    std::cout << "Keypair generation test passed\n";
}

void test_event_signing_roundtrip() {
    auto kp = lili::generate_keypair();
    lili::Event event;
    event.created_at = static_cast<uint64_t>(time(nullptr));
    event.kind = 1;
    event.content = "Test event";
    event.tags = {{"p", "deadbeef"}};

    lili::Event signed_event = lili::sign_event(event, kp);
    assert(!signed_event.sig.empty());
    assert_64hex(signed_event.id, "event id");
    assert_64hex(signed_event.pubkey, "pubkey");
    assert(signed_event.sig.size() == 128);
    assert(std::regex_match(signed_event.sig, std::regex("[0-9a-f]{128}")));
    std::cout << "  sig is 128 lowercase hex chars\n";

    // A NIP-01 id is a full 64-char hex, NOT a short integer.
    assert(signed_event.id != "0");
    assert(signed_event.id.size() == 64);

    assert(lili::verify_signature(signed_event));
    std::cout << "Event signing roundtrip passed\n";
}

void test_tamper_detection() {
    auto kp = lili::generate_keypair();
    lili::Event event;
    event.created_at = static_cast<uint64_t>(time(nullptr));
    event.kind = 1;
    event.content = "Original content";

    lili::Event signed_event = lili::sign_event(event, kp);
    assert(lili::verify_signature(signed_event));

    // Content tampering MUST make verification fail, even if id/sig are
    // left untouched. The old implementation failed this check.
    lili::Event tampered = signed_event;
    tampered.content = "Tampered content";
    assert(!lili::verify_signature(tampered));
    std::cout << "Tamper detection (content) passed\n";

    // Forging the id field must also fail verification.
    lili::Event bad_id = signed_event;
    bad_id.id = std::string(64, 'a');
    assert(!lili::verify_signature(bad_id));
    std::cout << "Tamper detection (id) passed\n";

    // Forging the pubkey must also fail (signature key mismatch).
    lili::Event bad_pubkey = signed_event;
    bad_pubkey.pubkey[0] = bad_pubkey.pubkey[0] == '0' ? '1' : '0';
    assert(!lili::verify_signature(bad_pubkey));
    std::cout << "Tamper detection (pubkey) passed\n";
}

void test_id_is_deterministic_and_binds_fields() {
    auto kp = lili::generate_keypair();
    lili::Event a;
    a.created_at = 1234567;
    a.kind = 30079;
    a.content = "{\"name\":\"x\"}";

    lili::Event b = a;
    assert(lili::sign_event(a, kp).id == lili::sign_event(b, kp).id);
    std::cout << "Deterministic id passed\n";

    // The id must change when any serialized field changes (content here).
    lili::Event c = a;
    c.content = "{\"name\":\"y\"}";
    assert(lili::sign_event(c, kp).id != lili::sign_event(a, kp).id);
    std::cout << "id binds content passed\n";

    // The id must depend on pubkey: same content, different key -> different id.
    auto kp2 = lili::generate_keypair();
    assert(lili::sign_event(a, kp).id != lili::sign_event(a, kp2).id);
    std::cout << "id binds pubkey passed\n";
}

void test_nip01_known_answer() {
    // Fixed Ed25519 seed + fixed event. Expected id/pubkey/sig were produced
    // by an INDEPENDENT reference implementation (pynacl, RFC 8032 Ed25519)
    // for the canonical NIP-01 serialization. This proves genuine Nostr
    // compatibility, not just internal self-consistency.
    const char* seed_hex =
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

    lili::KeyPair kp;
    for (size_t i = 0; i < 32; ++i) {
        char b[3] = {seed_hex[i * 2], seed_hex[i * 2 + 1], 0};
        kp.secret_key[i] = static_cast<uint8_t>(std::strtoul(b, nullptr, 16));
    }
    // Derive the public key from the seed, exactly as real key usage does.
    {
        EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(
            EVP_PKEY_ED25519, nullptr, kp.secret_key.data(), 32);
        size_t len = 32;
        EVP_PKEY_get_raw_public_key(pkey, kp.public_key.data(), &len);
        EVP_PKEY_free(pkey);
    }
    lili::Event seed_evt;
    seed_evt.created_at = 1690000000;
    seed_evt.kind = 1;
    seed_evt.content = "test";
    seed_evt.tags = {};
    auto e = lili::sign_event(seed_evt, kp);

    assert(e.pubkey ==
           "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8");
    assert(e.id ==
           "87c0bf643c06447a38dbb30e7f6569ad8635ab23248c5ba5e9018ce043510b27");
    assert(e.sig ==
           "1c83efb1088925474c76b2fa8f39c940333df0cad0c960cd7d29bda610f74fc0"
           "30a99713b2abede2692f18368e731e1361415f183b8bfc87c9bd7d6d26826601");
    assert(lili::verify_signature(e));
    std::cout << "NIP-01 known-answer (independent reference) passed\n";
}

int main() {
    test_keypair_generation();
    test_event_signing_roundtrip();
    test_tamper_detection();
    test_id_is_deterministic_and_binds_fields();
    test_nip01_known_answer();
    std::cout << "All signing tests passed!\n";
    return 0;
}
