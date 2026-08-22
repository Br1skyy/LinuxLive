#pragma once

#include <stdint.h>
#include <stddef.h>

// This header is C-compatible: a C compiler can include it to use the SDK.
// The C++ API surface is only the struct + function declarations below.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t secret_key[32];
    uint8_t public_key[32];
} lili_keypair_t;

typedef struct {
    char* id;               // NIP-01 64-char hex event id (owned, heap)
    uint64_t created_at;
    uint16_t kind;
    char* content;
    char* pubkey;
    char* sig;
} lili_event_t;

lili_keypair_t* lili_generate_keypair();
void lili_free_keypair(lili_keypair_t* kp);

lili_event_t* lili_create_achievement(const char* name, const char* description, int tier);
lili_event_t* lili_sign_event(lili_event_t* event, const lili_keypair_t* kp);
void lili_free_event(lili_event_t* event);

int lili_verify_event(const lili_event_t* event);
char* lili_serialize_event(const lili_event_t* event);
void lili_free_string(char* str);

#ifdef __cplusplus
}
#endif
