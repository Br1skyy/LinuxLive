#pragma once

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "lili-core/identity.hpp"
#include "lili-core/persistence.hpp"
#include "lili-core/passive_scanner.hpp"
#include "lili-core/relay_client.hpp"

namespace lili {

struct ChatMessage {
    std::string sender;
    std::string content;
    uint64_t timestamp;
    bool is_encrypted;
};

struct NodeInfo {
    std::string id;
    std::string name;
    std::string description;
    std::string creator_pubkey;
    std::string admin_privkey;
    std::string relay_url;
    uint64_t created_at;
    int member_count;
    bool is_local;
    bool running;
};

class App {
public:
    App();
    ~App();

    void run();

    static void build_ui(GtkApplication* app, gpointer data);

    static void on_generate_keypair(GtkWidget* w, gpointer d);
    static void on_login(GtkWidget* w, gpointer d);
    static void on_backup_done(GtkWidget* w, gpointer d);
    static void on_copy_private_key(GtkWidget* w, gpointer d);
    static void on_copy_public_key(GtkWidget* w, gpointer d);

    static void on_set_display_name(GtkWidget* w, gpointer d);

    static void on_scan_now(GtkWidget* w, gpointer d);
    static void on_achievement_selected(GtkTreeView* view, gpointer d);
    static void on_export_achievements(GtkWidget* w, gpointer d);
    static void on_import_achievements(GtkWidget* w, gpointer d);

    static void on_create_node(GtkWidget* w, gpointer d);
    static void on_node_toggle(GtkWidget* w, gpointer d);
    static void on_node_info(GtkWidget* w, gpointer d);
    static void on_node_delete(GtkWidget* w, gpointer d);
    static void on_chat_node_clicked(GtkWidget* w, gpointer d);
    static void on_back_to_nodes(GtkWidget* w, gpointer d);

    static void on_send_message(GtkWidget* w, gpointer d);

    static void on_relay_url_changed(GtkEditable* ed, gpointer d);
    static void on_connect_relay(GtkWidget* w, gpointer d);
    static void on_add_relay(GtkWidget* w, gpointer d);
    static void on_remove_relay(GtkWidget* w, gpointer d);

private:
    void build_profile_page(GtkWidget* nb);
    void build_achievements_page(GtkWidget* nb);
    void build_nodes_page(GtkWidget* nb);
    void build_settings_page(GtkWidget* nb);

    void switch_to_main();
    void switch_to_node_list();
    void switch_to_node_chat();
    void init_session();
    void connect_to_relay(const std::string& url);
    void publish_profile();
    void publish_achievement(const StoredAchievement& ach);
    void sync_achievements_from_relay();
    void refresh_achievements();
    void refresh_nodes();
    void refresh_chat();
    void update_profile_summary();
    void load_persisted_data();
    void save_nodes();
    void refresh_relay_list();

    std::string resolve_display_name(const std::string& pubkey) const;

    void on_new_achievement(const StoredAchievement& ach);
    void show_achievement_notification(const StoredAchievement& ach);
    void update_relay_status(bool connected);

    int find_node_index(const char* node_id) const;

    GtkWidget* window_ = nullptr;
    GtkWidget* auth_stack_ = nullptr;
    GtkWidget* main_stack_ = nullptr;

    GtkWidget* login_key_entry_ = nullptr;
    GtkWidget* backup_key_label_ = nullptr;
    GtkWidget* backup_key_copy_btn_ = nullptr;
    GtkWidget* auth_status_label_ = nullptr;

    GtkWidget* profile_name_entry_ = nullptr;
    GtkWidget* profile_pubkey_label_ = nullptr;
    GtkWidget* profile_pubkey_copy_btn_ = nullptr;
    GtkWidget* profile_ach_summary_ = nullptr;

    GtkWidget* ach_list_ = nullptr;
    GtkWidget* ach_detail_icon_ = nullptr;
    GtkWidget* ach_detail_name_ = nullptr;
    GtkWidget* ach_detail_tier_ = nullptr;
    GtkWidget* ach_detail_desc_ = nullptr;
    GtkWidget* ach_progress_label_ = nullptr;
    GtkListStore* ach_store_ = nullptr;

    GtkWidget* node_stack_ = nullptr;
    GtkWidget* node_scroll_ = nullptr;
    GtkWidget* node_box_ = nullptr;

    GtkWidget* chat_title_ = nullptr;
    GtkWidget* chat_view_ = nullptr;
    GtkWidget* chat_input_ = nullptr;
    GtkListStore* chat_store_ = nullptr;

    GtkWidget* relay_url_entry_ = nullptr;
    GtkWidget* relay_list_box_ = nullptr;

    GtkWidget* status_bar_ = nullptr;
    GtkWidget* relay_status_dot_ = nullptr;
    GtkWidget* relay_status_label_ = nullptr;

    IdentityManager identity_;
    Persistence persistence_;
    PassiveScanner scanner_;
    RelayClient relay_;
    std::vector<NodeInfo> nodes_;
    std::vector<ChatMessage> messages_;
    std::vector<std::string> relay_list_;
    std::string current_node_id_;
    std::string current_privkey_hex_;
    std::string current_pubkey_hex_;
    std::string current_relay_url_;
    bool logged_in_ = false;

    mutable std::map<std::string, std::string> display_name_cache_;
};

}
