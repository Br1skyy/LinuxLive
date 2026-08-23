#include "lili-core/passive_scanner.hpp"
#include "lili-core/identity.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <map>
#include <unistd.h>

namespace lili {

static std::string find_achievements_dir() {
    namespace fs = std::filesystem;

    if (const char* env = getenv("LILI_ACHIEVEMENTS_DIR")) {
        if (fs::exists(env)) return env;
    }

    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        fs::path dir = fs::path(buf).parent_path();
        for (int up = 0; up <= 5; ++up) {
            fs::path candidate = dir / "achievements";
            if (fs::exists(candidate)) return candidate.string();
            dir = dir.parent_path();
        }
    }

#ifdef LILI_ACHIEVEMENTS_DIR
    return LILI_ACHIEVEMENTS_DIR;
#else
    return "achievements";
#endif
}

// Minimal reader for the flat key/value YAML used by achievements/*.yaml:
// top-level scalars plus one nested block under "criteria:".
using YamlMap = std::map<std::string, std::string>;

static std::string yaml_trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string yaml_unquote(const std::string& v) {
    if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') ||
                          (v.front() == '\'' && v.back() == '\''))) {
        return v.substr(1, v.size() - 2);
    }
    return v;
}

static bool parse_achievement_yaml(const std::string& path, YamlMap& out) {
    std::ifstream f(path);
    if (!f) return false;

    std::string section;
    std::string line;
    while (std::getline(f, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos && (hash == 0 || line[hash - 1] == ' '))
            line = line.substr(0, hash);

        bool nested = !line.empty() && (line[0] == ' ' || line[0] == '\t');
        std::string content = yaml_trim(line);
        if (content.empty()) continue;

        size_t colon = content.find(':');
        if (colon == std::string::npos) continue;

        std::string key = yaml_trim(content.substr(0, colon));
        std::string value = yaml_trim(content.substr(colon + 1));

        if (nested) {
            if (!section.empty()) out[section + "." + key] = yaml_unquote(value);
        } else if (value.empty()) {
            section = key;
        } else {
            section.clear();
            out[key] = yaml_unquote(value);
        }
    }
    return true;
}

PassiveScanner::PassiveScanner() {}
PassiveScanner::~PassiveScanner() { stop(); }

void PassiveScanner::set_persistence(Persistence* p) { persistence_ = p; }
void PassiveScanner::set_unlock_callback(UnlockCallback cb) { unlock_callback_ = cb; }

void PassiveScanner::load_definitions() {
    namespace fs = std::filesystem;

    achievements_.clear();
    criteria_by_id_.clear();

    std::vector<StoredAchievement> saved;
    if (persistence_) saved = persistence_->load_achievements();

    std::string dir = find_achievements_dir();
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        std::cerr << "LinuxLive: achievements directory not found: " << dir << "\n";
        return;
    }

    for (const auto& category : fs::directory_iterator(dir, ec)) {
        if (!category.is_directory()) continue;

        for (const auto& entry : fs::directory_iterator(category.path())) {
            std::string ext = entry.path().extension().string();
            if (ext != ".yaml" && ext != ".yml") continue;

            YamlMap y;
            if (!parse_achievement_yaml(entry.path().string(), y) || !y.count("id")) continue;

            StoredAchievement a;
            a.id = y["id"];
            a.name = y.count("name") ? y["name"] : a.id;
            a.description = y.count("description") ? y["description"] : "";
            a.tier = y.count("tier") ? y["tier"] : "bronze";
            a.category = y.count("category") ? y["category"] : "";
            a.icon = y.count("icon") ? y["icon"] : "";
            a.unlocked = false;
            a.unlocked_at = 0;

            ScanCriteria c;
            c.type = y["criteria.type"];
            c.distro = y["criteria.distro"];
            c.binary_name = y["criteria.name"];
            c.display_protocol = y["criteria.protocol"];
            c.wm_class = y["criteria.class"];
            c.min_services = atoi(y["criteria.min_services"].c_str());
            c.min_commits = atoi(y["criteria.min_commits"].c_str());
            c.is_custom_kernel = y["criteria.custom"] == "true";
            c.time_after_hour = atoi(y["criteria.after_hour"].c_str());
            c.time_before_hour = atoi(y["criteria.before_hour"].c_str());

            for (const auto& s : saved) {
                if (s.id == a.id && s.unlocked) {
                    a.unlocked = true;
                    a.unlocked_at = s.unlocked_at;
                }
            }

            criteria_by_id_[a.id] = c;
            achievements_.push_back(a);
        }
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

        auto it = criteria_by_id_.find(ach.id);
        if (it == criteria_by_id_.end()) continue;

        const ScanCriteria& c = it->second;
        bool unlocked = false;

        if (c.type == "always") {
            unlocked = true;
        } else if (c.type == "os_detect") {
            unlocked = check_os(c);
        } else if (c.type == "binary") {
            unlocked = check_binary(c);
        } else if (c.type == "display_server") {
            unlocked = check_display_server(c);
        } else if (c.type == "window_manager") {
            unlocked = check_window_manager(c);
        } else if (c.type == "kernel") {
            unlocked = check_kernel(c);
        } else if (c.type == "git") {
            unlocked = check_git(c);
        } else if (c.type == "time") {
            unlocked = check_time(c);
        } else if (c.type == "services") {
            unlocked = check_services(c);
        } else if (c.type == "social") {
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

    return !c.distro.empty() && cached_distro_ == c.distro;
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
