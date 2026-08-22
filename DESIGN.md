# LinuxLive — Master/Subnode + NIP-01 Nostr Design

Status: design approved; implementation in progress.
**Progress (verified):**
- [x] Phase 1 — NIP-01 core (canonical id/signing/verification). Compiles + passes tests, including an independent cross-check against `pynacl` (byte-identical id/pubkey/sig for a fixed seed).
- [x] Phase 2 — NIP-01 relay (ingest sig validation, dedupe, parameterized-replaceable, full filters incl. `#e`/since/until/limit, NIP-20 OK). Compiles + passes an end-to-end WebSocket integration test.
- [x] Phase 3 — Master registration/trust (REGISTER/ACK/HEARTBEAT kinds, SubnodeRegistry, passphrase, disk persistence, signed ACK). Compiles + passes integration test.
- [x] Phase 4 — LAN discovery (UDP probe/response). Compiles + passes integration test.
- [x] Phase 5 — Subnode client (`RelayClient::register_subnode/send_heartbeat/sync_achievements`, ACK handling). Compiles.
- [x] Phase 5b — Achievement sync merge wired into `app.cpp` (`sync_achievements_from_relay`, `PassiveScanner::mark_unlocked`, `30079` event handling).
- [x] Phase 6 — GUI subnode-settings UI (scan for masters, register, heartbeat, status). Written for GTK4; must be compiled on a machine with GTK4 (not buildable in this sandbox).
- [x] Phase 7 — Tests & hardening:
  - `sessions_` access now guarded by `sessions_mutex_` (accept-thread data race fixed).
  - WebSocket frame-size cap (16 MiB) added to both server and client (DoS fix).
  - Heartbeat timeout: master marks subnodes inactive via `prune_inactive` every ~300s.
  - Event store persisted to `~/.lili-master/<id>/events.json` and reloaded on restart (master stays the source of truth) — verified by `persistence_integration.py`.
  - Removed unused `fin` warning in `ws_client.cpp`.

**Verified in this environment** (g++ + nlohmann single-header + pynacl/websockets): all protocol/signing/SDK C and C++ tests pass, plus end-to-end integration tests for relay, master registration, LAN discovery, and master event persistence. The GUI (GTK4) cannot be compiled here; it needs the real `cmake` build on a machine with GTK4.

Phase 8 — Role-based GUI (in progress):
- [x] `lili-relay-core` is a static library the GUI now links (so it can embed the master relay).
- [x] `RelayServer` dashboard accessors: `port()`, `subnode_count()`, `get_subnodes()`, `get_events(kinds, author)`.
- [x] Persisted role (`~/.lili/master_config.json`: `role`, `master_name`, `master_port`, `master_passphrase`) with an in-app switch (one active role per process).
- [x] SUBNODE mode = normal client UI; its "Register with a Master" section connects the master as the relay for chat + achievements (hub architecture). Includes a "Switch to Master mode" button.
- [x] MASTER mode = dashboard replacing the normal UI: header (name · port · online count), a live list of registered subnodes (name · pubkey · IP · online/offline, refreshed every 3s), per-subnode stored achievements on selection, and a Master Settings page (name/port/passphrase, apply/start/stop, switch to subnode).
- [x] GUI **compiled and linked against real GTK4 4.22.4** in this sandbox (fixed GTK4 API issues: `gtk_editable_set_text/get_text`, `gtk_list_box_remove` signature, unused-param warnings). It also builds via the full `cmake` project.

Phase 9 — CLI parity (complete, verified in this sandbox):
- [x] `lili-cli` — a unified CLI with full GUI parity across both roles, linking `lili-core` + `lili-relay-core`. Commands:
  - `identity generate|import|show`
  - `discover [--timeout MS] [--json]`
  - `achievements scan|list|export|import|publish|sync <relay-url>`
  - `node create <name>|node list` (published to a master)
  - `chat send|receive <relay-url> <node_id>` (routed through the master; added `RelayClient::subscribe_tag` for proper `#e` filtering)
  - `relay list|add <url>`
  - `subnode register|heartbeat <relay-url> [--passphrase]`
  - `master run [--port|--name|--passphrase]` (embedded relay) and `master status` (registered subnodes + per-subnode achievements)
- [x] Verified end-to-end (`cli_e2e.sh`): identity, register accepted + wrong-passphrase denied, heartbeat, achievements scan/publish/sync, master status (subnodes + achievements + port), node create + chat send/receive through the master, relay add/list — ALL PASS.
- Note: `discover` needs a real LAN (broadcast doesn't loop back in the sandbox); verified it runs and returns `[]`/`No masters found` here.

Phase 10 — Single-master model (complete):
- The subnode's Settings **Relay List / Default Relay URL** UI is removed. A subnode now has **one master** (persisted `master_url` in `master_config.json`): a "Master (your hub)" section lets you enter the master URL and connect, and registering with a discovered master auto-sets it.
- The subnode connects to its master on login, on switching to subnode mode, and after registration; all chat + achievements route through that one master.
- New chat nodes use the master URL as their relay (GUI and CLI).
- CLI `relay add/list` now operates on the single master URL (`save_master_url`).

Phase 11 — Plain chat rooms, hosted on the MASTER (complete):
- The "Chat Nodes" concept is removed. A chat room is a NODE event **stored on the master**; a subnode never stores rooms locally. No per-room keypair / admin key, no NODE-identity event, no running/stop/start state.
- Subnode lists rooms by subscribing to NODE kind on its master; creating a room publishes a NODE event to the master (`RelayClient::publish_room`). Messages are kind-42 events tagged with the room id, stored on the master. `NodeInfo`/`StoredNode` = `{id, name, description, creator_pubkey, created_at}`.
- GUI: page renamed **"Chat"** → **Chat Rooms**. The subnode has **no room-creation button** — it only lists/joins rooms from the master (a note reads "Rooms are hosted on your master. This subnode only joins them."). Rows are Name + Open / Info. Room creation is a **master-side** action, available both in the CLI (`chat create <name> <relay-url>`) and in the **master GUI** (Master Settings → Chat Rooms → "Create Room", which publishes a NODE event to the master's own embedded relay) — full GUI/CLI parity.
- CLI: `chat create <name> <relay-url>` publishes the room to the master; `chat list <relay-url>` lists rooms from the master; `chat send|receive <relay-url> <room_id> <msg>` unchanged.
- Verified: a separate subnode (fresh machine/identity, no shared state) lists a room another subnode created on the master and chats in it (`master_rooms_test.sh`).


Scope decisions (locked):

- **Nostr:** spec-compliant events + spec-compliant relay; the LAN master is a private relay, not a public one. No public-relay interop in v1.
- **Achievements:** keep custom kinds `30079`/`30080` (valid Nostr parameterized-replaceable events with `d` tags). No NIP-58 badge migration.
- **Hub architecture (single secure endpoint):** the master is the one machine every subnode talks to. ALL traffic — chat (kind `42`), achievements (`30079`/`30080`), metadata, registration/heartbeat — routes through the master's relay and is persisted there. Subnodes are pure clients and do NOT connect to arbitrary public relays. This is the key property behind "secure one machine": security (TLS, passphrase/authenticated channel) is applied once, on the master; subnodes trust their master.
- **Master role:** a private, spec-compliant relay + authority + storage hub for its registered subnodes; in the GUI it presents a dashboard, not a chat client.
- **Subnode role:** client that registers with one master and uses that master as its relay for chat + achievements.
- **Discovery:** UDP probe/response + periodic beacon, with manual IP fallback.
- **Trust:** open registration by default, optional shared passphrase.
- **Master form:** embeddable — a linkable library (`lili-relay-core`) used by both the headless `lili-relay` binary and the GUI's master dashboard.
- **Role selection:** persistent `~/.lili/master_config.json` setting (`master`/`subnode`) with an in-app switch; one active role per GUI process (multiple masters = multiple processes/ports).
- **Terminology:** "master" / "subnode" = machines (peers), distinct from the existing chat "node" concept.


---

## 1. NIP-01 compatibility (the protocol fix)

The current protocol is not Nostr-compatible. Requirements:

- **Event id** = lowercase 64-char hex SHA-256 of the canonical JSON array
  `[0, <pubkey>, <created_at>, <kind>, <tags>, <content>]` serialized with **no whitespace**.
- **Signature** = Ed25519 over the **same canonical serialization string** used for the id.
- **Verification** = recompute the id from the fields, compare to `event.id`, then Ed25519-verify the sig over the serialization with `event.pubkey`.
- `Event::id` becomes `std::string` (64 hex), replacing the current truncated `uint64_t`.

### Current bugs being fixed
- `id` currently truncates SHA-256 to 8 bytes and **omits pubkey**.
- Signature currently covers `"id\ncreated_at\nkind\n"` — content/tags are not bound, and id is taken as-given (never recomputed), so content can be tampered while a sig still verifies.
- `test_tamper_detection` only checks inequality, never calls `verify()` — must be rewritten so a tampered event fails verification.

### Files
- `cpp/lili-protocol/include/lili-protocol/event.hpp` — `id` → `std::string`.
- `cpp/lili-protocol/src/signing.cpp` — `compute_event_id`, `sign_event`, `verify_signature` rewritten to NIP-01 canonical form.
- `cpp/lili-protocol/src/event.cpp` — serialize/deserialize `id` as hex string.
- `cpp/lili-protocol/tests/test_signing.cpp` — real tamper-detection test (tamper ⇒ verify fails); add a known-answer vector.
- `cpp/lili-sdk/include/lili-sdk/sdk.hpp` + `src/sdk.cpp` — `lili_event_t.id` becomes `char*` (string), update C ABI.
- `cpp/lili-core/src/relay_client.cpp`, `cpp/lili-gui/src/app.cpp` — drop `std::to_string(signed_event.id)`; use the string id.
- `cpp/lili-core/src/persistence.cpp` — export/import use string ids.

## 2. NIP-01 compliant relay

- **Ingest validation:** recompute id and verify sig on every `EVENT`; reject invalid with NIP-20 `["OK", id, false, "invalid: ..."]`.
- **Dedupe by id** (replace parameterized-replaceable `30079/30080` on `(kind, pubkey, d-tag)`).
- **Full REQ filters:** `ids`, `authors`, `kinds`, `#<single-letter>` tag filters, `since`, `until`, `limit`.
- Optional NIP-11 info document (HTTP GET `/`).
- Master mode gates private registration; the relay stays chat-capable by default, `--master` adds the authority behavior.

### Files
- `cpp/lili-relay/src/relay_server.cpp` — ingest validation, dedupe, filter expansion, NIP-20 OK.
- `cpp/lili-relay/src/client_session.cpp` — unchanged (transport already works).
- `cpp/lili-relay/src/main.cpp` — `--master`, `--name`, `--passphrase`, `--discovery-port`.

## 3. Registration / trust (master authority)

New protocol kinds in `Event::Kind`:

| Kind | Name | Direction | Content | Tags |
|---|---|---|---|---|
| `30021` | `REGISTER` | sub→master | `{display_name, hostname, distro, wm, client_version}` | `d` = subnode pubkey |
| `30022` | `REGISTER_ACK` | master→sub | `{status, registry_size, message}` | `d` = subnode pubkey |
| `30023` | `HEARTBEAT` | sub→master | `{uptime, achievements_unlocked}` | `d` = subnode pubkey |

- Subnode identity = event `pubkey` (its Ed25519 key). Trust depends on Phase 2 sig verification.
- Master verifies sig, checks optional passphrase, adds to registry, replies with a signed `30022`.
- `HEARTBEAT` updates `last_seen`; subnodes drop to inactive after N misses.
- New `SubnodeRegistry` class + disk persistence (`~/.lili-master/<master_pubkey>/registry.json`, `subnodes/<pubkey>/achievements.json`).

### Files
- `cpp/lili-protocol/include/lili-protocol/event.hpp` — add kinds.
- `cpp/lili-protocol/src/subnode.cpp` (new) — register/heartbeat event builders.
- `cpp/lili-relay/include/lili-relay/subnode_registry.hpp` + `src/subnode_registry.cpp` (new).
- `cpp/lili-relay/src/master_store.cpp` (new) — authoritative JSON persistence.
- `cpp/lili-core/include/lili-core/relay_client.hpp` + `src/relay_client.cpp` — `register_subnode()`, `send_heartbeat()`, `query_subnodes()`, working `sync_achievements()`.

## 4. LAN discovery (UDP)

- Master opens a UDP socket on a well-known port (e.g. `9042`).
- Subnode broadcasts a probe; master replies `{name, port, master_pubkey}`; periodic beacon optional.
- Manual IP entry fallback in the GUI.

### Files
- `cpp/lili-relay/src/discovery.cpp` (new) — responder + beacon.
- `cpp/lili-core/include/lili-core/discoverer.hpp` + `src/discoverer.cpp` (new) — listener + probe.

## 5. Achievement sync

- Push on unlock (existing `send_achievement`, now NIP-01-valid).
- On connect, pull `REQ kinds=[30079], authors=[self]` and **merge** by id — implements the empty `App::sync_achievements_from_relay()`.
- Master is authoritative; subnode merges missing ids back.

## 6. GUI

- Settings: "Register with a master" — scan (discoverer), list, Register/Unregister, passphrase, sync status, manual address.
- Optional master-view tab (read registry over a connection).

---

## Phased order

1. **NIP-01 core** — canonical id/signing/verification + tests (foundation for everything; also the Phase-1 security fix).
2. **NIP-01 relay** — ingest validation, dedupe, full filters, NIP-20 OK.
3. **Registration/trust** — kinds, `SubnodeRegistry`, passphrase, persistence, signed ACK.
4. **LAN discovery** — UDP probe/response + beacon.
5. **Achievement sync** — push + pull/resync; implement `sync_achievements_from_relay`.
6. **GUI** — subnode settings, optional master view.
7. **Tests & hardening** — sig-rejection, registration auth, discovery parsing, sync merge, persistence round-trip, heartbeat timeout.

## Risks / notes
- Passphrase crosses the LAN unencrypted (plaintext WS). Acceptable for a home tool; authenticated channel (existing `Encryption` layer) is the long-term fix.
- Master advertises its pubkey in discovery and signs ACKs so subnodes can verify they're talking to the real master.
- Duplicate registration (same keypair) updates rather than duplicates.
- Restart must reload registry + achievements so subnodes can resync after a master reboot.
- UDP discovery port must be configurable if multiple masters run on one host.

## Phase 12 — Stats & gamified leaderboard (master) (complete)
- Subnodes publish live system stats + a cumulative terminal-command counter to the master as **STATS (30090)** events. New `lili::SystemStats` collector (`system_stats.cpp`) reads /proc (hostname, distro, kernel, CPU, cores, MemTotal/MemAvailable, uptime) plus the command counter.
- Terminal-command counting is cumulative and persisted to the lili data dir. CLI `stats bump` increments it, meant to be wired into a shell prompt hook (`PROMPT_COMMAND='lili-cli stats bump'` for bash). The subnode GUI also publishes stats every 60s.
- `RelayClient::send_stats` publishes the STATS event; `lili-core` exposes `parse_stats`, `aggregate_leaderboard` (newest STATS per pubkey), and `leaderboard_score` (commands + uptime*100 + achievements*500 + online bonus).
- Master aggregates the newest STATS per subnode into a **Leaderboard** tab (Dashboard / Settings / Leaderboard), ranked by score, with medals for the top 3 plus commands, uptime, achievements, memory and distro per node. The CLI `master status` prints the same leaderboard, so aggregation is verified end-to-end.
- Kind 30090 was added to the relay's default allowed_kinds and kind_ttl (never expires) so STATS events persist across restarts.
- Usage: on a subnode, `lili-cli stats bump` (shell hook) + `lili-cli stats report <master-url>` (or just leave the GUI open) to feed the leaderboard.

## Phase 13 — Hybrid mode (default + recommended) (complete)
- New role **hybrid**: one process is BOTH the master hub and a subnode. It starts the embedded master relay and also connects to itself as a subnode client, so every master function (Dashboard, Master Settings, Leaderboard, room creation) and every subnode function (Profile, Achievements, Chat, Settings, stats) works in a single instance.
- The hybrid UI is the union of the master and subnode tabs, but with a **tailored Settings page** instead of the two generic ones: tabs are Dashboard, Leaderboard, Profile, Achievements, Chat, and a single **Settings** page holding only what matters for a hybrid. Since a hybrid hub is private and only this machine connects to it, the hub **port and joining passphrase are irrelevant and are not shown** — Settings only has hub name (applied live, no restart), Start/Stop Hub + status, Create Room, and master/subnode role switching. The generic subnode Settings (external-master connect + discovery registration) and the full "Master Settings" page are NOT shown in hybrid.
- **CLI parity for hybrid:** `master run --loopback` hosts the same private loopback hub the GUI hybrid does (binds 127.0.0.1, no discovery beacon, banner says "[private loopback hub - only this machine can connect]"). The subnode side is the existing commands against `ws://127.0.0.1:PORT`: `subnode register`, `chat`, `stats report`, and `master status` prints the leaderboard.
- **Hybrid hub is private (loopback-only).** `RelayConfig::loopback_only` binds the embedded relay to 127.0.0.1 and skips discovery advertising, so a hybrid node hosts its hub *only for itself* — no other subnode can connect to a hybrid node (verified: LAN IP is refused, 127.0.0.1 works, no discovery beacon). A pure `master` keeps `loopback_only=false` (binds 0.0.0.0, advertises via UDP discovery) so real subnodes can join. `apply_role` stops and restarts the embedded relay on every role switch so the bind always matches the role (hybrid→master rebinds open, etc.).
- Role is persisted; `apply_role` defaults to **hybrid** when none is saved (explicit master/subnode choices are respected). Both pure-mode Settings pages offer "Switch to Hybrid mode (recommended)".
- Shared subnode setup is factored into `begin_subnode_work(report_to_self)` used by init_session and every role switch, so master/subnode/hybrid share the same code paths.
- CLI parity: the CLI already covers the hybrid equivalent — `master run` embeds the relay, and `subnode register` / `chat` / `stats report ws://127.0.0.1:<port>` make the same machine its own subnode.
