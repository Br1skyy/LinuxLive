#include "lili-core/identity.hpp"
#include <cassert>
#include <iostream>
#include <filesystem>

void test_generate_identity() {
    lili::IdentityManager mgr;
    auto id = mgr.generate("TestUser");

    assert(!id.public_key.empty());
    assert(!id.secret_key.empty());
    assert(id.display_name == "TestUser");

    std::string pub_hex = lili::IdentityManager::pubkey_hex(id);
    assert(pub_hex.size() == 64);
    std::cout << "Generate identity test passed (pubkey: " << pub_hex.substr(0, 16) << "...)\n";
}

void test_save_load_identity() {
    lili::IdentityManager mgr;
    auto id = mgr.generate("SaveTest");

    assert(mgr.save(id));
    assert(mgr.has_identity());

    auto loaded = mgr.load();
    assert(loaded.has_value());
    assert(loaded->public_key == id.public_key);
    assert(loaded->display_name == "SaveTest");

    std::string orig_priv = lili::IdentityManager::privkey_hex(id);
    std::string loaded_priv = lili::IdentityManager::privkey_hex(*loaded);
    assert(orig_priv == loaded_priv);

    std::cout << "Save/load identity test passed\n";
}

void test_login_with_key() {
    lili::IdentityManager mgr;
    auto id = mgr.generate("LoginTest");
    std::string priv_hex = lili::IdentityManager::privkey_hex(id);

    lili::IdentityManager mgr2;
    auto logged_in = mgr2.login(priv_hex);
    assert(logged_in.has_value());
    assert(logged_in->public_key == id.public_key);

    std::cout << "Login with key test passed\n";
}

void test_invalid_key_login() {
    lili::IdentityManager mgr;
    auto result = mgr.login("not_a_valid_hex_key");
    assert(!result.has_value());
    std::cout << "Invalid key login test passed\n";
}

void test_clear_identity() {
    lili::IdentityManager mgr;
    auto id = mgr.generate("ClearTest");
    mgr.save(id);
    assert(mgr.has_identity());

    mgr.clear();
    assert(!mgr.has_identity());
    std::cout << "Clear identity test passed\n";
}

int main() {
    lili::IdentityManager mgr;
    mgr.clear();

    test_generate_identity();
    test_save_load_identity();
    test_login_with_key();
    test_invalid_key_login();
    test_clear_identity();

    mgr.clear();

    std::cout << "All identity tests passed!\n";
    return 0;
}
