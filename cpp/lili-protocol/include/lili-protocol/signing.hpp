#pragma once

#include "event.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace lili {

struct KeyPair {
    std::array<uint8_t, 32> secret_key;
    std::array<uint8_t, 32> public_key;
};

KeyPair generate_keypair();
Event sign_event(Event& event, const KeyPair& keypair);
bool verify_signature(const Event& event);

}
