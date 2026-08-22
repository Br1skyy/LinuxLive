#include "lili-protocol/signing.hpp"
#include "lili-protocol/event.hpp"
#include <cassert>
#include <iostream>

void test_keypair_generation() {
    auto kp = lili::generate_keypair();
    assert(!kp.secret_key.empty());
    assert(!kp.public_key.empty());
    std::cout << "Keypair generation test passed\n";
}

void test_event_signing() {
    auto kp = lili::generate_keypair();
    lili::Event event;
    event.created_at = static_cast<uint64_t>(time(nullptr));
    event.kind = 1;
    event.content = "Test event";

    lili::Event signed_event = lili::sign_event(event, kp);
    assert(!signed_event.sig.empty());
    assert(signed_event.id != 0);
    std::cout << "Event signing test passed\n";
}

void test_tamper_detection() {
    auto kp = lili::generate_keypair();
    lili::Event event;
    event.created_at = static_cast<uint64_t>(time(nullptr));
    event.kind = 1;
    event.content = "Original content";

    lili::Event signed_event = lili::sign_event(event, kp);

    lili::Event tampered = signed_event;
    tampered.content = "Tampered content";

    assert(tampered.content != signed_event.content);
    std::cout << "Tamper detection test passed\n";
}

int main() {
    test_keypair_generation();
    test_event_signing();
    test_tamper_detection();
    std::cout << "All signing tests passed!\n";
    return 0;
}
