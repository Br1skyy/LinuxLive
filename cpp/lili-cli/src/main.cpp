// lili-cli — full CLI parity with the LinuxLive GUI, across both roles.
//
// Everything the GUI can do, the CLI can do:
//   identity, discover, achievements, chat, relay, subnode, master.
//
// Subcommands:
//   identity generate [--name NAME]
//   identity import <privkey_hex> [--name NAME]
//   identity show
//   discover [--timeout MS] [--json]
//   achievements scan
//   achievements list [--json]
//   achievements export <file>
//   achievements import <file>
//   achievements sync <relay-url>
//   chat create <name> <relay-url>
//   chat list <relay-url>
//   chat send <relay-url> <room_id> <message>
//   chat receive <relay-url> <room_id> [--seconds N]
//   stats report <relay-url>
//   stats bump
//   relay list
//   relay add <url>
//   subnode register <relay-url> [--passphrase P] [--name NAME]
//   subnode heartbeat <relay-url>
//   master run [--port N] [--name NAME] [--passphrase P] [--loopback]
//   master status
//   help

#include "lili-core/identity.hpp"
#include "lili-core/persistence.hpp"
#include "lili-core/passive_scanner.hpp"
#include "lili-core/relay_client.hpp"
#include "lili-core/system_stats.hpp"
#include <algorithm>
#include "lili-core/discoverer.hpp"
#include "lili-protocol/signing.hpp"
#include "lili-relay/relay_server.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <random>
#include <sstream>
#include <iomanip>

using lili::IdentityManager;
using lili::RelayClient;
using lili::RelayEvent;
using lili::RelayConfig;
using lili::RelayServer;
using lili::DiscoveredMaster;
using lili::StoredAchievement;

namespace {

std::vector<std::string> g_args;

std::string arg(size_t i, const std::string& def = "") {
    return (i < g_args.size()) ? g_args[i] : def;
}

const char* flag_value(const std::string& name, const char* def = nullptr) {
    for (size_t i = 0; i < g_args.size(); ++i) {
        if (g_args[i] == name && i + 1 < g_args.size()) return g_args[i + 1].c_str();
    }
    return def;
}

bool has_flag(const std::string& name) {
    for (auto& a : g_args) if (a == name) return true;
    return false;
}

void die(const std::string& msg) {
    std::cerr << "error: " << msg << "\n";
    exit(1);
}

std::string load_privkey() {
    auto id = lili::IdentityManager().load();
    if (!id) die("no identity. run `lili-cli identity generate` first");
    return IdentityManager::privkey_hex(*id);
}
std::string load_pubkey() {
    auto id = lili::IdentityManager().load();
    if (!id) die("no identity. run `lili-cli identity generate` first");
    return IdentityManager::pubkey_hex(*id);
}

// -------------------------------------------------------------------------
// identity
// -------------------------------------------------------------------------
int cmd_identity(const std::string& sub) {
    IdentityManager im;
    if (sub == "generate") {
        std::string name = flag_value("--name", "LinuxLive User");
        auto id = im.generate(name);
        im.save(id);
        std::cout << "Identity generated and saved to " << IdentityManager::data_dir() << "\n";
        std::cout << "  display name : " << id.display_name << "\n";
        std::cout << "  public key   : " << IdentityManager::pubkey_hex(id) << "\n";
        std::cout << "  private key  : " << IdentityManager::privkey_hex(id) << "\n";
        std::cout << "Back up the private key; it is the only way to restore your identity.\n";
    } else if (sub == "import") {
        std::string key = arg(1);
        if (key.empty()) die("usage: identity import <privkey_hex>");
        auto id = im.login(key);
        if (!id) die("invalid private key");
        if (const char* n = flag_value("--name")) id->display_name = n;
        im.save(*id);
        std::cout << "Identity imported.\n";
        std::cout << "  public key   : " << IdentityManager::pubkey_hex(*id) << "\n";
    } else if (sub == "show") {
        auto id = im.load();
        if (!id) die("no identity saved");
        std::cout << "display name : " << id->display_name << "\n";
        std::cout << "public key   : " << IdentityManager::pubkey_hex(*id) << "\n";
        std::cout << "created      : " << id->created_at << "\n";
    } else {
        die("unknown identity subcommand: " + sub);
    }
    return 0;
}

// -------------------------------------------------------------------------
// discover
// -------------------------------------------------------------------------
int cmd_discover() {
    int timeout = 1500;
    if (const char* t = flag_value("--timeout")) timeout = atoi(t);
    auto found = lili::Discoverer().discover(9042, timeout);
    if (has_flag("--json")) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& m : found) {
            arr.push_back({{"name", m.name}, {"host", m.host}, {"port", m.port},
                           {"pubkey", m.pubkey},
                           {"passphrase_required", m.passphrase_required}});
        }
        std::cout << arr.dump(2) << "\n";
        return 0;
    }
    if (found.empty()) { std::cout << "No masters found.\n"; return 0; }
    for (auto& m : found) {
        std::cout << m.name << "  (" << m.host << ":" << m.port << ")"
                  << (m.passphrase_required ? "  [passphrase required]" : "")
                  << "\n  pubkey: " << m.pubkey << "\n";
    }
    return 0;
}

// -------------------------------------------------------------------------
// achievements
// -------------------------------------------------------------------------
// "  unlocked 2024-05-01 14:30" for unlocked achievements, else "".
static std::string unlock_suffix(const lili::StoredAchievement& a) {
    if (!a.unlocked || a.unlocked_at == 0) return "";
    char dt[40];
    time_t t = (time_t)a.unlocked_at;
    std::strftime(dt, sizeof(dt), "  unlocked %Y-%m-%d %H:%M", localtime(&t));
    return dt;
}

int cmd_achievements(const std::string& sub) {
    lili::PassiveScanner scanner;
    lili::Persistence persistence;
    scanner.set_persistence(&persistence);
    scanner.load_definitions();
    scanner.scan_now();
    auto achs = scanner.get_achievements();

    if (sub == "scan") {
        int unlocked = scanner.get_unlocked_count();
        std::cout << unlocked << "/" << achs.size() << " unlocked\n";
        for (auto& a : achs)
            std::cout << (a.unlocked ? "[X] " : "[ ] ") << a.id << "  " << a.name
                      << unlock_suffix(a) << "\n";
    } else if (sub == "list") {
        if (has_flag("--json")) {
            nlohmann::json arr = nlohmann::json::array();
            for (auto& a : achs) {
                arr.push_back({{"id", a.id}, {"name", a.name}, {"description", a.description},
                               {"tier", a.tier}, {"category", a.category}, {"icon", a.icon},
                               {"unlocked", a.unlocked}, {"unlocked_at", a.unlocked_at}});
            }
            std::cout << arr.dump(2) << "\n";
            return 0;
        }
        for (auto& a : achs)
            std::cout << (a.unlocked ? "[X] " : "[ ] ") << a.id << "  " << a.name
                      << "  (" << a.tier << ")" << unlock_suffix(a) << "\n";
    } else if (sub == "export") {
        std::string file = arg(1);
        if (file.empty()) die("usage: achievements export <file>");
        if (!persistence.export_achievements(file, achs, load_pubkey()))
            die("failed to export");
        std::cout << "Exported " << achs.size() << " achievements to " << file << "\n";
    } else if (sub == "import") {
        std::string file = arg(1);
        if (file.empty()) die("usage: achievements import <file>");
        auto imported = persistence.import_achievements(file);
        std::cout << "Imported " << imported.size() << " achievements from " << file << "\n";
    } else if (sub == "sync") {
        std::string url = arg(1);
        if (url.empty()) die("usage: achievements sync <relay-url>");
        RelayClient rc;
        rc.connect(url);
        if (!rc.is_connected()) die("could not connect to " + url);
        std::vector<RelayEvent> got;
        rc.set_event_callback([&](const RelayEvent& e) {
            if (e.kind == static_cast<int>(lili::Event::Kind::ACHIEVEMENT))
                got.push_back(e);
        });
        rc.sync_achievements(load_pubkey());
        std::this_thread::sleep_for(std::chrono::seconds(3));
        rc.disconnect();
        std::cout << "Pulled " << got.size() << " achievement event(s) from " << url << "\n";
        for (auto& e : got) {
            std::string nm = e.content;
            try {
                auto c = nlohmann::json::parse(e.content);
                nm = c.value("name", c.value("achievement_id", e.content));
            } catch (...) {}
            std::cout << "  - " << nm << "  (" << e.id.substr(0, 12) << "...)\n";
        }
    } else if (sub == "publish") {
        std::string url = arg(1);
        if (url.empty()) die("usage: achievements publish <relay-url>");
        RelayClient rc;
        rc.connect(url);
        if (!rc.is_connected()) die("could not connect to " + url);
        std::string priv = load_privkey(), pub = load_pubkey();
        int sent = 0;
        for (auto& a : achs) {
            if (!a.unlocked) continue;
            if (rc.send_achievement(a.id, a.name, a.description, a.icon, priv, pub)) sent++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        rc.disconnect();
        std::cout << "Published " << sent << " unlocked achievement(s) to " << url << "\n";
    } else {
        die("unknown achievements subcommand: " + sub);
    }
    return 0;
}

// -------------------------------------------------------------------------
// node / chat
// -------------------------------------------------------------------------
int cmd_chat(const std::string& sub) {
    if (sub == "create") {
        std::string name = arg(1);
        std::string url = arg(2);
        if (name.empty() || url.empty()) die("usage: chat create <name> <relay-url>");
        RelayClient rc;
        rc.connect(url);
        if (!rc.is_connected()) die("could not connect to " + url);
        // Rooms are hosted on the master; creating one publishes a NODE event.
        std::string id = rc.publish_room(name, load_privkey(), load_pubkey());
        rc.disconnect();
        if (id.empty()) die("failed to publish room to " + url);
        std::cout << "Room '" << name << "' created on " << url << "\n";
        std::cout << "  id   : " << id << "\n";
        return 0;
    }
    if (sub == "list") {
        std::string url = arg(1);
        if (url.empty()) die("usage: chat list <relay-url>");
        RelayClient rc;
        rc.connect(url);
        if (!rc.is_connected()) die("could not connect to " + url);
        std::vector<RelayEvent> rooms;
        rc.set_event_callback([&](const RelayEvent& e) {
            if (e.kind == static_cast<int>(lili::Event::Kind::NODE)) rooms.push_back(e);
        });
        rc.subscribe({static_cast<int>(lili::Event::Kind::NODE)});
        for (int i = 0; i < 50 && rooms.empty(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        rc.disconnect();
        if (rooms.empty()) { std::cout << "No rooms on " << url << ".\n"; return 0; }
        for (auto& r : rooms)
            std::cout << r.content << "  (" << r.id.substr(0, 16) << "...)\n";
        return 0;
    }

    std::string url = arg(1);
    std::string node_id = arg(2);
    if (url.empty() || node_id.empty())
        die("usage: chat <send|receive> <relay-url> <room_id> [...]");
    RelayClient rc;
    rc.connect(url);
    if (!rc.is_connected()) die("could not connect to " + url);

    if (sub == "send") {
        std::string msg = arg(3);
        if (msg.empty()) die("usage: chat send <relay-url> <room_id> <message>");
        bool ok = rc.send_channel_message(node_id, msg, load_privkey(), load_pubkey());
        rc.disconnect();
        std::cout << (ok ? "sent" : "failed to send") << "\n";
    } else if (sub == "receive") {
        int seconds = 5;
        if (const char* s = flag_value("--seconds")) seconds = atoi(s);
        std::vector<RelayEvent> got;
        rc.set_event_callback([&](const RelayEvent& e) {
            if (e.kind == 42) got.push_back(e);
        });
        rc.subscribe_tag({42}, "e", node_id);
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        rc.disconnect();
        if (got.empty()) { std::cout << "No messages.\n"; return 0; }
        for (auto& e : got) {
            char t[32];
            time_t ts = (time_t)e.created_at;
            std::strftime(t, sizeof(t), "%H:%M:%S", localtime(&ts));
            std::cout << "[" << t << "] " << e.pubkey.substr(0, 12) << ": "
                      << e.content << "\n";
        }
    } else {
        die("unknown chat subcommand: " + sub);
    }
    return 0;
}

// -------------------------------------------------------------------------
// stats
// -------------------------------------------------------------------------
int cmd_stats(const std::string& sub) {
    if (sub == "bump") {
        // Wire this into your shell prompt to count terminal commands, e.g.:
        //   bash: PROMPT_COMMAND='lili-cli stats bump >/dev/null 2>&1'
        uint64_t v = lili::bump_command_count();
        std::cout << "command count: " << v << "\n";
        return 0;
    }
    if (sub == "report") {
        std::string url = arg(1);
        if (url.empty()) die("usage: stats report <relay-url>");
        lili::SystemStats st = lili::collect_system_stats();
        RelayClient rc;
        rc.connect(url);
        if (!rc.is_connected()) die("could not connect to " + url);
        bool ok = rc.send_stats(st, load_privkey(), load_pubkey());
        rc.disconnect();
        if (!ok) die("failed to send stats to " + url);

        std::cout << st.hostname << " (" << st.distro << ")\n";
        std::cout << "  kernel      : " << st.kernel << "\n";
        std::cout << "  cpu         : " << st.cpu << " x" << st.cores << "\n";
        std::cout << "  memory      : " << st.mem_used_mb << " MiB / "
                  << st.mem_total_mb << " MiB\n";
        std::cout << "  uptime      : " << st.uptime_seconds << " s\n";
        std::cout << "  commands    : " << st.commands << "\n";
        std::cout << "  reported to : " << url << "\n";
        return 0;
    }
    die("usage: stats <bump | report <relay-url>>");
    return 0;
}

// -------------------------------------------------------------------------
// relay
// -------------------------------------------------------------------------
int cmd_relay(const std::string& sub) {
    lili::Persistence persistence;
    if (sub == "list") {
        std::string url = persistence.load_master_url();
        if (url.empty()) { std::cout << "No master configured.\n"; return 0; }
        std::cout << url << "\n";
    } else if (sub == "add") {
        std::string url = arg(1);
        if (url.empty()) die("usage: relay add <master-url>");
        persistence.save_master_url(url);
        persistence.save_relay_url(url);   // backward-compatible default
        std::cout << "Set master: " << url << "\n";
    } else {
        die("unknown relay subcommand: " + sub);
    }
    return 0;
}

// -------------------------------------------------------------------------
// subnode
// -------------------------------------------------------------------------
int cmd_subnode(const std::string& sub) {
    std::string url = arg(1);
    if (url.empty()) die("usage: subnode <register|heartbeat> <relay-url> [...]");

    if (sub == "register") {
        std::string name = flag_value("--name", "");
        std::string pass = flag_value("--passphrase", "");
        RelayClient rc;
        rc.connect(url);
        if (!rc.is_connected()) die("could not connect to " + url);

        std::atomic<bool> ack_done(false), ack_ok(false);
        std::string ack_msg;
        rc.set_register_ack_callback([&](bool ok, const std::string& m) {
            ack_ok = ok; ack_msg = m; ack_done = true;
        });
        if (name.empty()) {
            auto id = lili::IdentityManager().load();
            name = id ? id->display_name : "subnode";
        }
        bool sent = rc.register_subnode(name, "", "", "", pass, load_privkey(), load_pubkey());
        if (!sent) die("failed to send registration");

        for (int i = 0; i < 30 && !ack_done; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        rc.disconnect();
        if (!ack_done) { std::cout << "No ACK from master (is it running? wrong port?)\n"; return 1; }
        std::cout << (ack_ok ? "registered: " : "denied: ") << ack_msg << "\n";
        return ack_ok ? 0 : 1;
    } else if (sub == "heartbeat") {
        RelayClient rc;
        rc.connect(url);
        if (!rc.is_connected()) die("could not connect to " + url);
        lili::PassiveScanner scanner;
        lili::Persistence persistence;
        scanner.set_persistence(&persistence);
        scanner.load_definitions();
        bool ok = rc.send_heartbeat(0, (uint32_t)scanner.get_unlocked_count(),
                                    load_privkey(), load_pubkey());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        rc.disconnect();
        std::cout << (ok ? "heartbeat sent" : "failed to send heartbeat") << "\n";
        return ok ? 0 : 1;
    } else {
        die("unknown subnode subcommand: " + sub);
    }
    return 0;
}

// -------------------------------------------------------------------------
// master
// -------------------------------------------------------------------------
std::atomic<bool> g_stop(false);
void on_signal(int) { g_stop = true; }

RelayConfig load_master_config(lili::Persistence& p) {
    RelayConfig cfg;
    cfg.master_mode = true;
    cfg.master_name = p.load_master_name();
    cfg.registration_passphrase = p.load_master_passphrase();
    return cfg;
}

int cmd_master(const std::string& sub) {
    lili::Persistence persistence;
    if (sub == "run") {
        uint16_t port = persistence.load_master_port();
        if (const char* p = flag_value("--port")) port = (uint16_t)atoi(p);
        persistence.save_master_port(port);   // so `master status` reflects it
        if (const char* n = flag_value("--name")) persistence.save_master_name(n);
        if (const char* p = flag_value("--passphrase")) persistence.save_master_passphrase(p);
        RelayConfig cfg = load_master_config(persistence);
        cfg.loopback_only = (flag_value("--loopback") != nullptr);  // hybrid-style private hub
        std::cout << "Starting master '" << cfg.master_name << "' on port "
                  << (unsigned)port << (cfg.registration_passphrase.empty()
                      ? " (open registration)" : " (passphrase required)")
                  << (cfg.loopback_only
                      ? " [private loopback hub - only this machine can connect]"
                      : " [LAN hub]")
                  << ". Ctrl-C to stop.\n";
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        RelayServer server(port, cfg);
        server.start();
        while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        server.stop();
        std::cout << "\nMaster stopped.\n";
    } else if (sub == "status") {
        uint16_t port = persistence.load_master_port();
        RelayConfig cfg = load_master_config(persistence);
        // Constructing a master-mode server loads the persisted registry and
        // event store without binding/listening.
        RelayServer server(port, cfg);
        auto subs = server.get_subnodes();
        std::cout << "Master '" << cfg.master_name << "' on port " << (unsigned)port << "\n";
        if (subs.empty()) { std::cout << "No registered subnodes.\n"; }
        for (auto& s : subs) {
            std::cout << (s.active ? "[online ] " : "[offline] ")
                      << s.display_name << "  " << s.pubkey.substr(0, 16)
                      << "  " << (s.ip.empty() ? "?" : s.ip) << "\n";
            auto evs = server.get_events(
                {static_cast<uint16_t>(lili::Event::Kind::ACHIEVEMENT)}, s.pubkey);
            for (auto& e : evs) {
                std::string nm = e.content;
                try {
                    auto c = nlohmann::json::parse(e.content);
                    nm = c.value("name", c.value("achievement_id", e.content));
                } catch (...) {}
                std::cout << "      achievement: " << nm << "\n";
            }
        }
        // Global stats leaderboard from stored STATS events.
        std::vector<lili::StatsEvent> sevs;
        for (auto& e : server.get_events(
                {static_cast<uint16_t>(lili::Event::Kind::STATS)}, "")) {
            lili::StatsEvent se; se.pubkey = e.pubkey;
            se.created_at = e.created_at; se.content = e.content;
            sevs.push_back(se);
        }
        auto board = lili::aggregate_leaderboard(sevs);
        for (auto& le : board) {
            for (auto& s : subs) {
                if (s.pubkey == le.pubkey) { le.display_name = s.display_name; le.active = s.active; break; }
            }
            if (le.display_name.empty()) le.display_name = le.stats.hostname;
            le.achievements = server.get_events(
                {static_cast<uint16_t>(lili::Event::Kind::ACHIEVEMENT)}, le.pubkey).size();
            le.score = lili::leaderboard_score(le.stats, le.achievements, le.active);
        }
        if (!board.empty()) {
            std::cout << "\n--- GLOBAL LEADERBOARD ---\n";
            std::sort(board.begin(), board.end(),
                [](const lili::LeaderboardEntry& a, const lili::LeaderboardEntry& b) {
                    return a.score > b.score; });
            int rank = 1;
            for (auto& le : board) {
                std::cout << "  #" << rank << " " << le.display_name
                          << "  (score " << le.score << ", "
                          << le.achievements << " achievements"
                          << (le.active ? ", online" : "") << ")\n";
                std::cout << "      uptime " << le.stats.uptime_seconds / 3600 << "h"
                          << "  commands " << le.stats.commands
                          << "  mem " << le.stats.mem_used_mb << "/"
                          << le.stats.mem_total_mb << "MiB"
                          << "  [" << le.stats.distro << "]\n";
                rank++;
            }
        }
    } else {
        die("unknown master subcommand: " + sub);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "LinuxLive CLI — full parity with the GUI.\n"
                     "usage: lili-cli <command> [args]\n\n"
                     "  identity generate|import|show\n"
                     "  discover [--timeout MS] [--json]\n"
                     "  achievements scan|list|export|import|sync <relay-url>\n"
                     "  chat create <name> <relay-url> | chat list <relay-url> | chat send|receive <relay-url> <room_id> [...]\n"
                     "  stats report <relay-url> | stats bump\n"
                     "  relay list|add <master-url>\n"
                     "  subnode register|heartbeat <relay-url> [...]\n"
                     "  master run [--loopback] | master status\n"
                     "    master run --loopback  = private hub (hybrid-style, only this machine)\n"
                     "    on a private hub: subnode register/chat/stats report ws://127.0.0.1:PORT\n";
        return 1;
    }
    for (int i = 2; i < argc; ++i) g_args.push_back(argv[i]);
    std::string cmd = argv[1];
    std::string sub = argc > 2 ? argv[2] : "";

    if (cmd == "identity") return cmd_identity(sub);
    if (cmd == "discover") return cmd_discover();
    if (cmd == "achievements") return cmd_achievements(sub);
    if (cmd == "chat") return cmd_chat(sub);
    if (cmd == "stats") return cmd_stats(sub);
    if (cmd == "relay") return cmd_relay(sub);
    if (cmd == "subnode") return cmd_subnode(sub);
    if (cmd == "master") return cmd_master(sub);
    std::cerr << "unknown command: " << cmd << "\n";
    return 1;
}
