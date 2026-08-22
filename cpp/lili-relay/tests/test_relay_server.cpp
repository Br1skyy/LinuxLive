#include "lili-relay/relay_server.hpp"
#include <cassert>
#include <iostream>

void test_relay_server_creation() {
    lili::RelayServer server(8080);
    assert(server.is_event_allowed(1));
    assert(server.is_event_allowed(30079));
    assert(server.is_event_allowed(30080));
    std::cout << "Relay server creation test passed\n";
}

void test_event_kind_management() {
    lili::RelayServer server(8080);
    assert(!server.is_event_allowed(999));
    server.allow_event_kind(999);
    assert(server.is_event_allowed(999));
    std::cout << "Event kind management test passed\n";
}

int main() {
    test_relay_server_creation();
    test_event_kind_management();
    std::cout << "All relay server tests passed!\n";
    return 0;
}
