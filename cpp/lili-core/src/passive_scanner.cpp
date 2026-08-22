#include "lili-core/passive_scanner.hpp"
#include "lili-core/identity.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <unistd.h>

namespace lili {

PassiveScanner::PassiveScanner() {}
PassiveScanner::~PassiveScanner() { stop(); }

void PassiveScanner::set_persistence(Persistence* p) { persistence_ = p; }
void PassiveScanner::set_unlock_callback(UnlockCallback cb) { unlock_callback_ = cb; }

void PassiveScanner::load_definitions() {
    struct Def { const char* id; const char* name; const char* desc; const char* tier; const char* cat; const char* icon; };
    static const Def defs[] = {
        {"arch-warrior",    "Arch Warrior",    "Running Arch Linux - by the way",                         "silver",   "distro",     "\xF0\x9F\x8F\x94\xEF\xB8\x8F"},
        {"ubuntu-user",     "Ubuntu User",     "Running Ubuntu Linux",                                    "bronze",   "distro",     "Circle of Friends"},
        {"fedora-user",     "Fedora User",     "Running Fedora Linux - the cutting edge",                 "bronze",   "distro",     "\xF0\x9F\x8E\xA9"},
        {"void-walker",     "Void Walker",     "Running Void Linux - the init-less path",                 "silver",   "distro",     "\xE2\x9A\xAB"},
        {"debian-stable",   "Debian Stable",   "Running Debian - the universal OS",                       "bronze",   "distro",     "Debian Swirl"},
        {"distro-hopper",   "Distro Hopper",   "Detected 3+ different distros on this machine's history","gold",     "distro",     "\xF0\x9F\x94\x84"},
        {"tiling-lord",     "Tiling Lord",     "Running a tiling window manager",                         "silver",   "system",     "\xF0\x9F\xAA\x9F"},
        {"neovim-user",     "Neovim User",     "Neovim is installed - your fingers thank you",            "bronze",   "system",     "Green Box"},
        {"x11-veteran",     "X11 Veteran",     "Still running X11 - respect the classics",                "bronze",   "system",     "\xF0\x9F\x93\xBA"},
        {"wayland-pioneer", "Wayland Pioneer", "Running a Wayland display server",                        "bronze",   "system",     "\xF0\x9F\x8C\x8A"},
        {"docker-captain",  "Docker Captain",  "Docker is installed and running",                         "bronze",   "system",     "\xF0\x9F\x90\xB3"},
        {"systemd-lord",    "Systemd Lord",    "Managing 10 or more active services",                     "silver",   "system",     "\xE2\x9A\x99\xEF\xB8\x8F"},
        {"kernel-tamer",    "Kernel Tamer",    "Running a custom-compiled kernel",                        "gold",     "system",     "\xF0\x9F\x94\xA7"},
        {"platinum-linuxer","Platinum Linuxer", "Unlocked 90% of all achievements",                       "platinum", "milestones", "\xF0\x9F\x92\x8E"},
        {"night-owl",       "Night Owl",       "Using LinuxLive past midnight",                            "bronze",   "milestones", "\xF0\x9F\xA6\x89"},
        {"git-veteran",     "Git Veteran",     "Git is installed with 100+ commits in any repo",          "silver",   "milestones", "\xF0\x9F\x93\x8B"},
        {"first-boot",      "First Boot",      "Completed your first boot into LinuxLive",                "bronze",   "milestones", "\xF0\x9F\x9A\x80"},
        {"node-founder",    "Node Founder",    "Created your first chat node",                            "bronze",   "social",     "\xF0\x9F\x8F\xA0"},
        {"chatterbox",      "Chatterbox",      "Sent 50 messages on LinuxLive",                           "silver",   "social",     "\xF0\x9F\x92\xAC"},
        {"first-friend",    "First Friend",    "Added your first friend on LinuxLive",                    "bronze",   "social",     "\xF0\x9F\xA4\x9D"},
    };

    for (const auto& d : defs) {
        StoredAchievement a;
        a.id = d.id;
        a.name = d.name;
        a.description = d.desc;
        a.tier = d.tier;
        a.category = d.cat;
        a.icon = d.icon;
        a.unlocked = false;
        a.unlocked_at = 0;

        if (persistence_) {
            auto saved = persistence_->load_achievements();
            for (const auto& s : saved) {
                if (s.id == a.id && s.unlocked) {
                    a.unlocked = true;
                    a.unlocked_at = s.unlocked_at;
                }
            }
        }
        achievements_.push_back(a);
    }
}

void PassiveScanner::start(int interval_seconds) {
    scan_interval_seconds_ = interval_seconds;
    running_ = true;
    scan_thread_ = std::thread(&PassiveScanner::scan_thread_func, this);
}

void PassiveScanner::stop() {
    running_ = false;
    if (scan_thread_.joinable()) scan_thread_.join();
}

void PassiveScanner::scan_thread_func() {
    while (running_) {
        scan_now();
        for (int i = 0; i < scan_interval_seconds_ && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

std::vector<StoredAchievement> PassiveScanner::scan_now() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& ach : achievements_) {
        if (ach.unlocked) continue;

        std::string type = ach.category;
        bool unlocked = false;

        if (ach.id == "first-boot") {
            unlocked = true;
        } else if (type == "distro") {
            unlocked = check_os({.distro = ach.id});
        } else if (type == "system") {
            if (ach.id == "docker-captain") unlocked = check_binary({.binary_name = "docker"});
            else if (ach.id == "neovim-user") unlocked = check_binary({.binary_name = "nvim"});
            else if (ach.id == "wayland-pioneer") unlocked = check_display_server({.display_protocol = "wayland"});
            else if (ach.id == "x11-veteran") unlocked = check_display_server({.display_protocol = "x11"});
            else if (ach.id == "systemd-lord") unlocked = check_services({.min_services = 10});
            else if (ach.id == "kernel-tamer") unlocked = check_kernel({.is_custom_kernel = true});
            else if (ach.id == "tiling-lord") unlocked = check_window_manager({.wm_class = "tiling"});
        } else if (type == "milestones") {
            if (ach.id == "git-veteran") unlocked = check_git({.min_commits = 100});
            else if (ach.id == "night-owl") unlocked = check_time({.time_after_hour = 0, .time_before_hour = 5});
            else if (ach.id == "first-boot") unlocked = true;
        } else if (type == "social") {
            continue;
        }

        if (unlocked) {
            ach.unlocked = true;
            ach.unlocked_at = static_cast<uint64_t>(time(nullptr));
            if (unlock_callback_) unlock_callback_(ach);
        }
    }

    save_state();
    return achievements_;
}

std::vector<StoredAchievement> PassiveScanner::get_achievements() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return achievements_;
}

int PassiveScanner::get_unlocked_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::count_if(achievements_.begin(), achievements_.end(),
        [](const StoredAchievement& a) { return a.unlocked; });
}

float PassiveScanner::get_progress_percent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (achievements_.empty()) return 0.0f;
    int unlocked = std::count_if(achievements_.begin(), achievements_.end(),
        [](const StoredAchievement& a) { return a.unlocked; });
    return (unlocked * 100.0f) / achievements_.size();
}

void PassiveScanner::save_state() {
    if (persistence_) persistence_->save_achievements(achievements_);
}

std::string detect_distro() {
    std::ifstream f("/etc/os-release");
    if (f) {
        std::string line;
        while (std::getline(f, line)) {
            if (line.substr(0, 3) == "ID=") {
                std::string val = line.substr(3);
                if (!val.empty() && val.front() == '"') val = val.substr(1);
                if (!val.empty() && val.back() == '"') val.pop_back();
                std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                return val;
            }
        }
    }
    return "unknown";
}

bool PassiveScanner::check_os(const ScanCriteria& c) {
    if (!cached_distro_initialized_) {
        cached_distro_ = detect_distro();
        cached_distro_initialized_ = true;
    }

    static const std::pair<std::string, std::string> distro_map[] = {
        {"fedora-user", "fedora"},
        {"ubuntu-user", "ubuntu"},
        {"arch-warrior", "arch"},
        {"debian-stable", "debian"},
        {"void-walker", "void"},
    };

    for (const auto& [id, distro] : distro_map) {
        if (c.distro == id && cached_distro_ == distro) return true;
    }
    return false;
}

bool PassiveScanner::check_binary(const ScanCriteria& c) {
    const char* path_env = getenv("PATH");
    if (!path_env) return false;

    std::string paths(path_env);
    size_t start = 0;
    while (start < paths.size()) {
        size_t colon = paths.find(':', start);
        std::string dir = (colon == std::string::npos) ? paths.substr(start) : paths.substr(start, colon - start);
        std::string full = dir + "/" + c.binary_name;
        if (access(full.c_str(), X_OK) == 0) return true;
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return false;
}

bool PassiveScanner::check_services(const ScanCriteria& c) {
    FILE* pipe = popen(
        "systemctl list-units --type=service --state=active --no-legend --no-pager 2>/dev/null | wc -l", "r");
    if (!pipe) return false;

    char buf[32];
    int count = 0;
    if (fgets(buf, sizeof(buf), pipe)) count = atoi(buf);
    pclose(pipe);
    return count >= c.min_services;
}

bool PassiveScanner::check_display_server(const ScanCriteria& c) {
    if (!cached_display_initialized_) {
        cached_display_server_ = "unknown";
        const char* wayland = getenv("WAYLAND_DISPLAY");
        const char* session = getenv("XDG_SESSION_TYPE");
        if (wayland && strlen(wayland) > 0) cached_display_server_ = "wayland";
        else if (session && std::string(session) == "x11") cached_display_server_ = "x11";
        cached_display_initialized_ = true;
    }

    return cached_display_server_ == c.display_protocol;
}

bool PassiveScanner::check_window_manager(const ScanCriteria& c) {
    if (c.wm_class == "tiling") {
        const char* desktop = getenv("XDG_CURRENT_DESKTOP");
        if (!desktop) return false;
        std::string d = desktop;
        std::transform(d.begin(), d.end(), d.begin(), ::tolower);

        const char* tiling_wms[] = {"sway", "i3", "bspwm", "dwm", "awesome",
            "herbstluftwm", "xmonad", "hyprland", "river", "qtile", "niri"};
        for (const auto* wm : tiling_wms) {
            if (d.find(wm) != std::string::npos) return true;
        }
    }
    return false;
}

bool PassiveScanner::check_kernel(const ScanCriteria& c) {
    if (!c.is_custom_kernel) return false;

    FILE* pipe = popen("uname -r 2>/dev/null", "r");
    if (!pipe) return false;

    char buf[256];
    std::string release;
    if (fgets(buf, sizeof(buf), pipe)) {
        release = buf;
        release.erase(std::remove(release.begin(), release.end(), '\n'), release.end());
    }
    pclose(pipe);

    const char* indicators[] = {"-custom", "-zen", "-lqx", "-tkg", "-clear", "-bore", "-edge"};
    for (const auto* ind : indicators) {
        if (release.find(ind) != std::string::npos) return true;
    }
    return false;
}

bool PassiveScanner::check_git(const ScanCriteria& c) {
    if (c.min_commits <= 0) return false;

    const char* home = getenv("HOME");
    if (!home) return false;

    std::string cmd = std::string("find ") + home +
        " -maxdepth 4 -name .git -type d 2>/dev/null | "
        "head -50 | while read d; do "
        "git -C \"$(dirname $d)\" rev-list --count HEAD 2>/dev/null; "
        "done | awk '{s+=$1} END {print s+0}'";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;

    char buf[32];
    int total = 0;
    if (fgets(buf, sizeof(buf), pipe)) total = atoi(buf);
    pclose(pipe);
    return total >= c.min_commits;
}

bool PassiveScanner::check_time(const ScanCriteria& c) {
    time_t now = time(nullptr);
    int hour = localtime(&now)->tm_hour;
    if (c.time_after_hour <= c.time_before_hour) {
        return hour >= c.time_after_hour && hour < c.time_before_hour;
    }
    return hour >= c.time_after_hour || hour < c.time_before_hour;
}

}
