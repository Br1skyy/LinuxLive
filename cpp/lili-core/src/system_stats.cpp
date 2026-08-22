#include "lili-core/system_stats.hpp"
#include "lili-core/identity.hpp"
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/utsname.h>
#include <cstdlib>
#include <map>
#include <nlohmann/json.hpp>

namespace lili {

static std::string read_file(const char* path) {
    std::ifstream f(path);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string get_hostname() {
    char buf[256] = {0};
    if (gethostname(buf, sizeof(buf)) == 0) return buf;
    return "unknown";
}

static std::string get_distro() {
    std::string s = read_file("/etc/os-release");
    size_t p = s.find("PRETTY_NAME=");
    if (p != std::string::npos) {
        std::string v = s.substr(p + 12);
        size_t nl = v.find('\n');
        if (nl != std::string::npos) v = v.substr(0, nl);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
            v = v.substr(1, v.size() - 2);
        return trim(v);
    }
    return "unknown";
}

static std::string get_kernel() {
    struct utsname u;
    if (uname(&u) == 0) return u.release;
    return "unknown";
}

static std::string get_cpu() {
    std::string s = read_file("/proc/cpuinfo");
    size_t p = s.find("model name");
    if (p != std::string::npos) {
        size_t c = s.find(':', p);
        if (c != std::string::npos) {
            size_t nl = s.find('\n', c);
            std::string v = s.substr(c + 1, nl == std::string::npos
                ? std::string::npos : nl - c - 1);
            return trim(v);
        }
    }
    return "unknown";
}

static int get_cores() {
    std::string s = read_file("/proc/cpuinfo");
    int c = 0;
    size_t p = 0;
    while ((p = s.find("processor", p)) != std::string::npos) { c++; p += 9; }
    return c;
}

SystemStats collect_system_stats() {
    SystemStats st;
    st.hostname = get_hostname();
    st.distro = get_distro();
    st.kernel = get_kernel();
    st.cpu = get_cpu();
    st.cores = get_cores();

    std::string mi = read_file("/proc/meminfo");
    auto parse_mem = [&](const char* key) -> uint64_t {
        size_t p = mi.find(key);
        if (p == std::string::npos) return 0;
        size_t c = mi.find(':', p);
        if (c == std::string::npos) return 0;
        return static_cast<uint64_t>(std::atoll(mi.c_str() + c + 1)); // kB
    };
    uint64_t total_kb = parse_mem("MemTotal");
    uint64_t avail_kb = parse_mem("MemAvailable");
    st.mem_total_mb = total_kb / 1024;
    st.mem_used_mb = (total_kb > avail_kb) ? (total_kb - avail_kb) / 1024 : 0;

    std::string up = read_file("/proc/uptime");
    {
        std::istringstream iss(up);
        double sec = 0;
        if (iss >> sec) st.uptime_seconds = static_cast<uint64_t>(sec);
    }

    st.commands = load_command_count();
    return st;
}

static std::string command_count_path() {
    return IdentityManager::data_dir() + "/terminal.json";
}

uint64_t load_command_count() {
    std::ifstream f(command_count_path());
    if (!f) return 0;
    uint64_t v = 0;
    f >> v;
    return v;
}

uint64_t bump_command_count(uint64_t amount) {
    uint64_t v = load_command_count() + amount;
    std::ofstream f(command_count_path());
    f << v;
    return v;
}

SystemStats parse_stats(const std::string& content) {
    SystemStats st;
    try {
        auto c = nlohmann::json::parse(content);
        st.hostname = c.value("hostname", "");
        st.distro = c.value("distro", "");
        st.kernel = c.value("kernel", "");
        st.cpu = c.value("cpu", "");
        st.cores = c.value("cores", 0);
        st.mem_total_mb = c.value("mem_total_mb", 0ULL);
        st.mem_used_mb = c.value("mem_used_mb", 0ULL);
        st.uptime_seconds = c.value("uptime_seconds", 0ULL);
        st.commands = c.value("commands", 0ULL);
        st.ach_unlocked = c.value("ach_unlocked", 0ULL);
    } catch (...) {}
    return st;
}

uint64_t leaderboard_score(const SystemStats& st, uint64_t achievements, bool active) {
    uint64_t uptime_h = st.uptime_seconds / 3600;
    return st.commands + uptime_h * 100 + achievements * 500 + (active ? 1000 : 0);
}

std::vector<LeaderboardEntry> aggregate_leaderboard(const std::vector<StatsEvent>& events) {
    std::map<std::string, std::pair<uint64_t, SystemStats>> latest;
    for (const auto& e : events) {
        SystemStats s = parse_stats(e.content);
        auto it = latest.find(e.pubkey);
        if (it == latest.end() || e.created_at > it->second.first)
            latest[e.pubkey] = {e.created_at, s};
    }
    std::vector<LeaderboardEntry> out;
    out.reserve(latest.size());
    for (auto& kv : latest) {
        LeaderboardEntry le;
        le.pubkey = kv.first;
        le.stats = kv.second.second;
        out.push_back(le);
    }
    return out;
}

}
