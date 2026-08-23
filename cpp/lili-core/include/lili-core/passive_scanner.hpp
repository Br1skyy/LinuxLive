#pragma once

#include "lili-core/persistence.hpp"
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <unordered_map>

namespace lili {

struct ScanCriteria {
    std::string type;
    std::string distro;
    std::string binary_name;
    std::string display_protocol;
    std::string wm_class;
    int min_services = 0;
    int min_commits = 0;
    bool is_custom_kernel = false;
    int time_after_hour = 0;
    int time_before_hour = 0;
};

class PassiveScanner {
public:
    PassiveScanner();
    ~PassiveScanner();

    void load_definitions();
    void start(int interval_seconds = 60);
    void stop();

    std::vector<StoredAchievement> scan_now();
    std::vector<StoredAchievement> get_achievements() const;
    int get_unlocked_count() const;
    float get_progress_percent() const;

    using UnlockCallback = std::function<void(const StoredAchievement&)>;
    void set_unlock_callback(UnlockCallback cb);

    void set_persistence(Persistence* persistence);
    void save_state();

private:
    void scan_thread_func();

    std::string cached_distro_;
    bool cached_distro_initialized_ = false;
    std::string cached_display_server_;
    bool cached_display_initialized_ = false;

    bool check_os(const ScanCriteria& c);
    bool check_binary(const ScanCriteria& c);
    bool check_services(const ScanCriteria& c);
    bool check_display_server(const ScanCriteria& c);
    bool check_window_manager(const ScanCriteria& c);
    bool check_kernel(const ScanCriteria& c);
    bool check_git(const ScanCriteria& c);
    bool check_time(const ScanCriteria& c);

    std::vector<StoredAchievement> achievements_;
    std::unordered_map<std::string, ScanCriteria> criteria_by_id_;
    std::atomic<bool> running_{false};
    std::thread scan_thread_;
    mutable std::mutex mutex_;
    int scan_interval_seconds_ = 60;
    Persistence* persistence_ = nullptr;
    UnlockCallback unlock_callback_;
};

}
