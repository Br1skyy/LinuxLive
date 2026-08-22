#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace lili {

// Snapshot of a machine's system + terminal activity, reported to the master
// as a STATS (30090) event so the master can build a global leaderboard.
struct SystemStats {
    std::string hostname;
    std::string distro;       // "Fedora Linux 40 (Workstation Edition)"
    std::string kernel;       // "6.9.3-200.fc40.x86_64"
    std::string cpu;          // "Intel(R) Core(TM) i7-9750H CPU @ 2.60GHz"
    int cores = 0;            // online CPUs
    uint64_t mem_total_mb = 0;
    uint64_t mem_used_mb = 0;
    uint64_t uptime_seconds = 0;
    uint64_t commands = 0;    // cumulative terminal-command counter
    uint64_t ach_unlocked = 0;
};

// Read live system stats from /proc, /etc/os-release, uname.
SystemStats collect_system_stats();

// Cumulative terminal-command counter, persisted under the lili data dir.
uint64_t load_command_count();
// Increment the cumulative counter (wired into a shell PROMPT_COMMAND hook).
uint64_t bump_command_count(uint64_t amount = 1);

// A raw STATS event as stored on the relay (content is JSON of SystemStats).
struct StatsEvent {
    std::string pubkey;
    uint64_t created_at;
    std::string content;
};

// One node's entry on the master leaderboard.
struct LeaderboardEntry {
    std::string pubkey;
    std::string display_name;   // resolved by the caller (subnode registry)
    SystemStats stats;
    uint64_t achievements = 0;
    bool active = false;
    uint64_t score = 0;
};

// Parse a STATS event content JSON into a SystemStats.
SystemStats parse_stats(const std::string& content_json);

// Keep the newest STATS event per pubkey; caller sets achievements/active/score.
std::vector<LeaderboardEntry> aggregate_leaderboard(const std::vector<StatsEvent>& events);

// Gamified score: terminal commands + uptime + achievements (+ online bonus).
uint64_t leaderboard_score(const SystemStats& st, uint64_t achievements, bool active);

}
