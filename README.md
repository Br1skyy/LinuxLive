# LinuxLive

A Nostr-based social platform for Linux with achievements.

Your identity is a cryptographic keypair. No accounts made, no central servers, no one in between able to see your messages. Posts, achievements, and chat messages are signed events. Relays are easy to swap in and out of run your own or use someone else's.

The app passively scans your system and unlocks achievements for being a Linux user. Like steam achievements, but for your distro and what you do on it.

## What's here

```
cpp/
  lili-protocol/     # Ed25519 signing, Nostr events, NIP-58 achievements
  lili-relay/        # Relay server with rate limiting, event TTL, banning
  lili-sdk/          # C-ABI shared lib for game devs to hook into achievements
  lili-core/         # Identity, persistence, passive scanner, relay client
  lili-gui/          # GTK4 desktop app
achievements/        # YAML definitions (20 achievements across 4 categories)
```

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

### Dependencies

- CMake 3.20+
- GTK4 4.10+
- OpenSSL 3.x
- nlohmann/json (auto-fetched via CMake FetchContent)

## Running

```bash
./build/cpp/lili-gui/lili-gui
```

On first launch, generate a keypair. Back up your key it's the only way to recover your identity.

## Relay server

A standalone relay server is included:

```bash
./build/cpp/lili-relay/lili-relay --port 7777
```

Features: per-kind TTL, rate limiting, pubkey banning, event pruning.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

## Status

Early alpha. Identity and achievement detection work. Relay connectivity is basic. Chat and social features are wired up but not fully finished.

