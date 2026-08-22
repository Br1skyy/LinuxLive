#include "lili-sdk/sdk.hpp"
#include <cassert>
#include <iostream>

void test_keypair_generation() {
    auto kp = lili_generate_keypair();
    assert(kp != nullptr);
    lili_free_keypair(kp);
    std::cout << "SDK keypair generation test passed\n";
}

void test_create_achievement() {
    auto event = lili_create_achievement("Test Achievement", "A test achievement", 0);
    assert(event != nullptr);
    assert(event->kind == 30079);
    lili_free_event(event);
    std::cout << "SDK create achievement test passed\n";
}

void test_sign_event() {
    auto kp = lili_generate_keypair();
    auto event = lili_create_achievement("Test", "Test", 0);
    auto signed_event = lili_sign_event(event, kp);
    assert(signed_event != nullptr);
    assert(signed_event->sig != nullptr);
    lili_free_event(signed_event);
    lili_free_keypair(kp);
    std::cout << "SDK sign event test passed\n";
}

void test_verify_event() {
    auto kp = lili_generate_keypair();
    auto event = lili_create_achievement("Test", "Test", 0);
    auto signed_event = lili_sign_event(event, kp);
    assert(lili_verify_event(signed_event) == 1);
    lili_free_event(signed_event);
    lili_free_keypair(kp);
    std::cout << "SDK verify event test passed\n";
}

void test_serialize_event() {
    auto event = lili_create_achievement("Test", "Test", 0);
    auto serialized = lili_serialize_event(event);
    assert(serialized != nullptr);
    lili_free_string(serialized);
    lili_free_event(event);
    std::cout << "SDK serialize event test passed\n";
}

int main() {
    test_keypair_generation();
    test_create_achievement();
    test_sign_event();
    test_verify_event();
    test_serialize_event();
    std::cout << "All SDK tests passed!\n";
    return 0;
}
