#include "lili-protocol/event.hpp"
#include "lili-protocol/signing.hpp"
#include <cassert>
#include <iostream>

void test_event_creation() {
    lili::Event event;
    event.created_at = static_cast<uint64_t>(time(nullptr));
    event.kind = 1;
    event.content = "Hello, LAN!";
    event.pubkey = "test_pubkey";

    assert(!event.content.empty());
    assert(event.kind == 1);
    std::cout << "Event creation test passed\n";
}

void test_event_serialization() {
    lili::Event event;
    event.id = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    event.created_at = static_cast<uint64_t>(time(nullptr));
    event.kind = 1;
    event.content = "Test content";
    event.pubkey = "test_key";
    event.sig = "test_sig";

    std::string serialized = event.serialize();
    assert(!serialized.empty());

    auto deserialized = lili::Event::deserialize(serialized);
    assert(deserialized.has_value());
    assert(deserialized->id == event.id);
    assert(deserialized->content == event.content);
    std::cout << "Event serialization test passed\n";
}

void test_invalid_deserialization() {
    auto result = lili::Event::deserialize("invalid json");
    assert(!result.has_value());
    std::cout << "Invalid deserialization test passed\n";
}

int main() {
    test_event_creation();
    test_event_serialization();
    test_invalid_deserialization();
    std::cout << "All event tests passed!\n";
    return 0;
}
