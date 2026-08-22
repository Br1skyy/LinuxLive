#include "lili-sdk/sdk.hpp"
#include "lili-protocol/signing.hpp"
#include "lili-protocol/nip58.hpp"
#include <cstring>

extern "C" {

lili_keypair_t* lili_generate_keypair() {
    auto kp = lili::generate_keypair();
    auto result = new lili_keypair_t;
    memcpy(result->secret_key, kp.secret_key.data(), 32);
    memcpy(result->public_key, kp.public_key.data(), 32);
    return result;
}

void lili_free_keypair(lili_keypair_t* kp) {
    delete kp;
}

lili_event_t* lili_create_achievement(const char* name, const char* description, int tier) {
    lili::Achievement achievement;
    achievement.name = name;
    achievement.description = description;
    achievement.tier = static_cast<lili::AchievementTier>(tier);

    auto kp = lili::generate_keypair();
    auto event = lili::create_achievement_event(achievement, kp);

    auto result = new lili_event_t;
    result->id = event.id;
    result->created_at = event.created_at;
    result->kind = event.kind;
    result->content = strdup(event.content.c_str());
    result->pubkey = strdup(event.pubkey.c_str());
    result->sig = strdup(event.sig.c_str());

    return result;
}

lili_event_t* lili_sign_event(lili_event_t* event, const lili_keypair_t* kp) {
    lili::Event native_event;
    native_event.id = event->id;
    native_event.created_at = event->created_at;
    native_event.kind = event->kind;
    native_event.content = event->content;
    native_event.pubkey = event->pubkey;

    lili::KeyPair native_kp;
    memcpy(native_kp.secret_key.data(), kp->secret_key, 32);
    memcpy(native_kp.public_key.data(), kp->public_key, 32);

    auto signed_event = lili::sign_event(native_event, native_kp);

    event->id = signed_event.id;
    free(event->pubkey);
    event->pubkey = strdup(signed_event.pubkey.c_str());
    free(event->sig);
    event->sig = strdup(signed_event.sig.c_str());

    return event;
}

void lili_free_event(lili_event_t* event) {
    free(event->content);
    free(event->pubkey);
    free(event->sig);
    delete event;
}

int lili_verify_event(const lili_event_t* event) {
    lili::Event native_event;
    native_event.id = event->id;
    native_event.created_at = event->created_at;
    native_event.kind = event->kind;
    native_event.content = event->content;
    native_event.pubkey = event->pubkey;
    native_event.sig = event->sig;

    return native_event.verify() ? 1 : 0;
}

char* lili_serialize_event(const lili_event_t* event) {
    lili::Event native_event;
    native_event.id = event->id;
    native_event.created_at = event->created_at;
    native_event.kind = event->kind;
    native_event.content = event->content;
    native_event.pubkey = event->pubkey;
    native_event.sig = event->sig;

    auto serialized = native_event.serialize();
    return strdup(serialized.c_str());
}

void lili_free_string(char* str) {
    free(str);
}

} // extern "C"
