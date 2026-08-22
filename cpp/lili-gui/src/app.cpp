#include "lili-gui/app.hpp"
#include "lili-protocol/signing.hpp"
#include "lili-core/system_stats.hpp"
#include <nlohmann/json.hpp>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <thread>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <pango/pango.h>

namespace lili {

struct AppData { App* self; };
static AppData app_data;

App::App() { app_data.self = this; }
App::~App() {
    scanner_.stop();
    relay_.disconnect();
}

static GtkWidget* make_page(GtkWidget* nb, const char* title) {
    GtkWidget* p = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(p, 12);
    gtk_widget_set_margin_bottom(p, 12);
    gtk_widget_set_margin_start(p, 16);
    gtk_widget_set_margin_end(p, 16);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), p, gtk_label_new(title));
    return p;
}

void App::run() {
    GtkApplication* ga = gtk_application_new("com.linuxlive.app", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(ga, "activate", G_CALLBACK(build_ui), this);
    g_application_run(G_APPLICATION(ga), 0, nullptr);
    g_object_unref(ga);
}

void App::build_ui(GtkApplication* ga, gpointer ud) {
    auto* self = static_cast<App*>(ud);
    self->window_ = gtk_application_window_new(ga);
    gtk_window_set_title(GTK_WINDOW(self->window_), "LinuxLive");
    gtk_window_set_default_size(GTK_WINDOW(self->window_), 1000, 700);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(self->window_), vbox);

    self->auth_stack_ = gtk_stack_new();
    gtk_widget_set_vexpand(self->auth_stack_, TRUE);
    gtk_box_append(GTK_BOX(vbox), self->auth_stack_);

    GtkWidget* login_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(login_page, 60);
    gtk_widget_set_margin_start(login_page, 200);
    gtk_widget_set_margin_end(login_page, 200);
    gtk_stack_add_named(GTK_STACK(self->auth_stack_), login_page, "login");

    GtkWidget* title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='xx-large' weight='bold'>LinuxLive</span>\n"
        "<span size='medium'>Your decentralized Linux identity</span>");
    gtk_box_append(GTK_BOX(login_page), title);

    gtk_box_append(GTK_BOX(login_page), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget* new_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(new_label), "<span weight='bold'>New User</span>");
    gtk_label_set_xalign(GTK_LABEL(new_label), 0);
    gtk_box_append(GTK_BOX(login_page), new_label);

    GtkWidget* gen_btn = gtk_button_new_with_label("Generate New Identity");
    gtk_widget_add_css_class(gen_btn, "suggested-action");
    g_signal_connect(gen_btn, "clicked", G_CALLBACK(on_generate_keypair), self);
    gtk_box_append(GTK_BOX(login_page), gen_btn);

    self->backup_key_label_ = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(self->backup_key_label_), 0);
    gtk_label_set_wrap(GTK_LABEL(self->backup_key_label_), TRUE);
    gtk_widget_set_visible(self->backup_key_label_, FALSE);
    gtk_box_append(GTK_BOX(login_page), self->backup_key_label_);

    self->backup_key_copy_btn_ = gtk_button_new_with_label("Copy Private Key");
    gtk_widget_set_visible(self->backup_key_copy_btn_, FALSE);
    g_signal_connect(self->backup_key_copy_btn_, "clicked", G_CALLBACK(on_copy_private_key), self);
    gtk_box_append(GTK_BOX(login_page), self->backup_key_copy_btn_);

    GtkWidget* backup_done_btn = gtk_button_new_with_label("I've backed up my key - Continue");
    gtk_widget_set_visible(backup_done_btn, FALSE);
    g_signal_connect(backup_done_btn, "clicked", G_CALLBACK(on_backup_done), self);
    g_object_set_data(G_OBJECT(backup_done_btn), "self", self);
    gtk_box_append(GTK_BOX(login_page), backup_done_btn);
    g_object_set_data(G_OBJECT(self->backup_key_label_), "continue_btn", backup_done_btn);

    gtk_box_append(GTK_BOX(login_page), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget* login_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(login_label), "<span weight='bold'>Already have a key?</span>");
    gtk_label_set_xalign(GTK_LABEL(login_label), 0);
    gtk_box_append(GTK_BOX(login_page), login_label);

    self->login_key_entry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(self->login_key_entry_), "Paste your private key here...");
    gtk_box_append(GTK_BOX(login_page), self->login_key_entry_);

    GtkWidget* login_btn = gtk_button_new_with_label("Login");
    g_signal_connect(login_btn, "clicked", G_CALLBACK(on_login), self);
    gtk_box_append(GTK_BOX(login_page), login_btn);

    self->auth_status_label_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(self->auth_status_label_), 0);
    gtk_box_append(GTK_BOX(login_page), self->auth_status_label_);

    self->main_stack_ = gtk_stack_new();
    gtk_widget_set_vexpand(self->main_stack_, TRUE);
    gtk_stack_add_named(GTK_STACK(self->auth_stack_), self->main_stack_, "main");

    GtkWidget* hdr = gtk_header_bar_new();
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(hdr), gtk_label_new("LinuxLive"));
    gtk_window_set_titlebar(GTK_WINDOW(self->window_), hdr);

    self->notebook_ = gtk_notebook_new();
    gtk_widget_set_vexpand(self->notebook_, TRUE);
    gtk_stack_add_named(GTK_STACK(self->main_stack_), self->notebook_, "tabs");

    self->status_bar_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(self->status_bar_, 4);
    gtk_widget_set_margin_bottom(self->status_bar_, 4);
    gtk_widget_set_margin_start(self->status_bar_, 12);
    gtk_widget_set_margin_end(self->status_bar_, 12);
    gtk_box_append(GTK_BOX(vbox), self->status_bar_);

    self->relay_status_dot_ = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(self->relay_status_dot_), "<span color='#e74c3c'>\xe2\x97\x8f</span>");
    gtk_box_append(GTK_BOX(self->status_bar_), self->relay_status_dot_);

    GtkWidget* relay_lbl = gtk_label_new("Relay: disconnected");
    self->relay_status_label_ = relay_lbl;
    gtk_box_append(GTK_BOX(self->status_bar_), relay_lbl);

    if (self->identity_.has_identity()) {
        auto loaded = self->identity_.load();
        if (loaded) {
            self->logged_in_ = true;
            self->init_session();
            self->switch_to_main();
        }
    }

    gtk_window_present(GTK_WINDOW(self->window_));
}

void App::switch_to_main() {
    GtkWidget* child = gtk_stack_get_child_by_name(GTK_STACK(auth_stack_), "main");
    if (child) gtk_stack_set_visible_child(GTK_STACK(auth_stack_), child);
}

void App::init_session() {
    load_persisted_data();

    // Read persisted role and build the role-appropriate UI (starts the
    // embedded master relay if this machine is a master or hybrid).
    apply_role();

    // Pure master: an embedded hub only, no subnode work.
    if (role_ != "subnode" && role_ != "hybrid") return;

    begin_subnode_work(role_ == "hybrid");
}

// Set up this process as a subnode: scanner, achievements, chat, stats. When
// report_to_self is true (hybrid mode) the embedded master in this same process
// is this subnode's hub, so it connects and registers with itself.
void App::begin_subnode_work(bool report_to_self) {
    scanner_.set_persistence(&persistence_);
    scanner_.load_definitions();
    scanner_.start(60);
    scanner_.scan_now();

    refresh_achievements();
    refresh_nodes();
    update_profile_summary();

    std::string hub;
    if (report_to_self) {
        uint16_t port = master_server_ ? master_server_->port()
                                       : persistence_.load_master_port();
        hub = "ws://127.0.0.1:" + std::to_string(port);
    } else {
        hub = persistence_.load_master_url();
    }
    if (!hub.empty()) {
        connect_to_relay(hub);
        update_master_conn_status();
        if (report_to_self) self_register();
        publish_stats_now();   // populate the leaderboard immediately
    }

    // Publish live system/terminal stats to the master periodically.
    if (subnode_stats_source_ == 0) {
        subnode_stats_source_ = g_timeout_add_seconds(60, [](gpointer d) -> gboolean {
            static_cast<App*>(d)->publish_stats_now();
            return G_SOURCE_CONTINUE;
        }, this);
    }
}

// Send this machine's current system/terminal stats to its master (itself in
// hybrid mode) so the master's leaderboard stays fresh.
void App::publish_stats_now() {
    if (!relay_.is_connected()) return;
    auto id = identity_.load();
    if (!id) return;
    lili::SystemStats st = lili::collect_system_stats();
    st.ach_unlocked = static_cast<uint64_t>(scanner_.get_unlocked_count());
    relay_.send_stats(st,
        IdentityManager::privkey_hex(*id),
        IdentityManager::pubkey_hex(*id));
}

// Hybrid mode: register this process's own subnode identity with the embedded
// master it hosts, so it appears on its own dashboard and syncs achievements.
void App::self_register() {
    auto id = identity_.load();
    if (!id) return;
    std::string pass = persistence_.load_master_passphrase();
    uint16_t port = master_server_ ? master_server_->port()
                                   : persistence_.load_master_port();
    std::string url = "ws://127.0.0.1:" + std::to_string(port);

    relay_.set_register_ack_callback([this](bool accepted, const std::string&) {
        if (accepted) {
            registered_ = true;
            sync_achievements_from_relay();
            g_timeout_add_seconds(60, [](gpointer d2) -> gboolean {
                return static_cast<App*>(d2)->heartbeat_tick();
            }, this);
        }
    });
    relay_.register_subnode(id->display_name, "127.0.0.1", "", "", pass,
                            IdentityManager::privkey_hex(*id),
                            IdentityManager::pubkey_hex(*id));
}

void App::load_persisted_data() {
    // Rooms are hosted on the master and pulled in via NODE events on connect;
    // nothing room-related is persisted locally anymore.
    nodes_.clear();
}

void App::connect_to_relay(const std::string& url) {
    relay_.disconnect();
    current_relay_url_ = url;

    relay_.set_connect_callback([this](bool connected) {
        if (!window_) return;
        g_object_set_data(G_OBJECT(window_), "_relay_connected",
            connected ? GINT_TO_POINTER(1) : GINT_TO_POINTER(0));
        g_idle_add([](gpointer d) -> gboolean {
            auto* self = static_cast<App*>(d);
            if (!self->window_) return G_SOURCE_REMOVE;
            int val = GPOINTER_TO_INT(
                g_object_get_data(G_OBJECT(self->window_), "_relay_connected"));
            self->update_relay_status(val != 0);

            if (val != 0 && self->relay_.is_connected()) {
                self->publish_profile();

                self->sync_achievements_from_relay();

                self->relay_.subscribe(
                    {static_cast<int>(lili::Event::Kind::METADATA)});

                // Rooms are hosted on the master: subscribe to NODE events.
                self->relay_.subscribe(
                    {static_cast<int>(lili::Event::Kind::NODE)});

                if (!self->current_node_id_.empty()) {
                    self->relay_.subscribe(
                        {static_cast<int>(lili::Event::Kind::CHANNEL_MESSAGE)},
                        "", self->current_node_id_);
                }
            }
            return G_SOURCE_REMOVE;
        }, this);
    });

    relay_.set_event_callback([this](const RelayEvent& ev) {
        if (ev.kind == 0) {
            try {
                auto content = nlohmann::json::parse(ev.content);
                std::string name = content.value("name", "");
                if (!name.empty() && !ev.pubkey.empty()) {
                    display_name_cache_[ev.pubkey] = name;
                    g_idle_add([](gpointer d) -> gboolean {
                        auto* self = static_cast<App*>(d);
                        self->refresh_chat();
                        return G_SOURCE_REMOVE;
                    }, this);
                }
            } catch (...) {}
            return;
        }

        // A room announcement from the master (kind NODE). Rooms are hosted on
        // the master, so the room list is sourced here, not from local storage.
        if (ev.kind == static_cast<int>(lili::Event::Kind::NODE)) {
            bool found = false;
            for (auto& n : nodes_) {
                if (n.id == ev.id) { n.name = ev.content; found = true; break; }
            }
            if (!found && !ev.id.empty()) {
                NodeInfo n;
                n.id = ev.id;
                n.name = ev.content;
                n.creator_pubkey = ev.pubkey;
                n.created_at = ev.created_at;
                nodes_.push_back(n);
            }
            g_idle_add([](gpointer d) -> gboolean {
                auto* self = static_cast<App*>(d);
                self->refresh_nodes();
                return G_SOURCE_REMOVE;
            }, this);
            return;
        }

        if (ev.kind == 42 && !current_node_id_.empty()) {
            ChatMessage msg;
            msg.sender = resolve_display_name(ev.pubkey);
            msg.content = ev.content;
            msg.timestamp = ev.created_at;
            msg.is_encrypted = false;
            messages_.push_back(msg);
            g_idle_add([](gpointer d) -> gboolean {
                auto* self = static_cast<App*>(d);
                self->refresh_chat();
                return G_SOURCE_REMOVE;
            }, this);
        }

        // Merge achievements pulled back from a master/relay into local state.
        if (ev.kind == static_cast<int>(lili::Event::Kind::ACHIEVEMENT)) {
            try {
                auto content = nlohmann::json::parse(ev.content);
                std::string ach_id = content.value("achievement_id", "");
                if (!ach_id.empty()) {
                    scanner_.mark_unlocked(ach_id, ev.created_at);
                    g_idle_add([](gpointer d) -> gboolean {
                        auto* self = static_cast<App*>(d);
                        self->refresh_achievements();
                        self->update_profile_summary();
                        return G_SOURCE_REMOVE;
                    }, this);
                }
            } catch (...) {}
        }
    });

    bool ok = relay_.connect(url, true);
    update_relay_status(ok);
    if (!ok) {
        std::cerr << "LinuxLive: failed to connect to relay " << url << "\n";
    }
}

void App::update_relay_status(bool connected) {
    if (relay_status_dot_) {
        const char* color = connected ? "#27ae60" : "#e74c3c";
        char markup[64];
        snprintf(markup, sizeof(markup), "<span color='%s'>\xe2\x97\x8f</span>", color);
        gtk_label_set_markup(GTK_LABEL(relay_status_dot_), markup);
    }
    if (relay_status_label_) {
        if (connected && !current_relay_url_.empty()) {
            std::string short_url = current_relay_url_;
            size_t scheme = short_url.find("://");
            if (scheme != std::string::npos) short_url = short_url.substr(scheme + 3);
            if (short_url.size() > 30) short_url = short_url.substr(0, 27) + "...";
            char label[128];
            snprintf(label, sizeof(label), "Master: %s", short_url.c_str());
            gtk_label_set_text(GTK_LABEL(relay_status_label_), label);
        } else {
            gtk_label_set_text(GTK_LABEL(relay_status_label_),
                connected ? "Master: connected" : "Master: not connected");
        }
    }
}

void App::on_connect_master(GtkWidget*, gpointer d) {
    auto* self = static_cast<App*>(d);
    GtkEntryBuffer* buf = gtk_entry_get_buffer(GTK_ENTRY(self->master_url_entry_));
    const char* url = gtk_entry_buffer_get_text(buf);
    if (!url || strlen(url) == 0) return;
    self->persistence_.save_master_url(url);
    self->connect_to_relay(url);
    self->update_master_conn_status();
}

void App::on_generate_keypair(GtkWidget*, gpointer d) {
    auto* self = static_cast<App*>(d);
    auto id = self->identity_.generate("User");

    std::string priv_hex = IdentityManager::privkey_hex(id);
    std::string pub_hex = IdentityManager::pubkey_hex(id);
    self->current_privkey_hex_ = priv_hex;
    self->current_pubkey_hex_ = pub_hex;

    char msg[1024];
    snprintf(msg, sizeof(msg),
        "<span weight='bold' color='#e74c3c'>SAVE THIS KEY. YOU ONLY GET ONE CHANCE.</span>\n\n"
        "<span font_family='monospace' size='small'>%s</span>\n\n"
        "<span size='small'>This key is your login. Lose it and you lose everything.\n"
        "Write it down or save it in a password manager.</span>",
        priv_hex.c_str());

    gtk_label_set_markup(GTK_LABEL(self->backup_key_label_), msg);
    gtk_widget_set_visible(self->backup_key_label_, TRUE);
    gtk_widget_set_visible(self->backup_key_copy_btn_, TRUE);

    GtkWidget* continue_btn = static_cast<GtkWidget*>(
        g_object_get_data(G_OBJECT(self->backup_key_label_), "continue_btn"));
    gtk_widget_set_visible(continue_btn, TRUE);
}

void App::on_backup_done(GtkWidget*, gpointer d) {
    auto* self = static_cast<App*>(d);
    // Save exactly the keys shown on the backup screen, not a fresh pair
    auto id = self->identity_.login(self->current_privkey_hex_);
    if (!id) return;
    self->identity_.save(*id);
    self->logged_in_ = true;
    self->init_session();
    self->switch_to_main();
}

void App::on_copy_private_key(GtkWidget*, gpointer d) {
    auto* self = static_cast<App*>(d);
    if (self->current_privkey_hex_.empty()) return;
    GdkDisplay* display = gtk_widget_get_display(self->window_);
    GdkClipboard* clipboard = gdk_display_get_clipboard(display);
    gdk_clipboard_set_text(clipboard, self->current_privkey_hex_.c_str());
    gtk_button_set_label(GTK_BUTTON(self->backup_key_copy_btn_), "Copied!");
    g_timeout_add_seconds(2, [](gpointer d) -> gboolean {
        auto* btn = static_cast<GtkWidget*>(d);
        gtk_button_set_label(GTK_BUTTON(btn), "Copy Private Key");
        return G_SOURCE_REMOVE;
    }, self->backup_key_copy_btn_);
}

void App::on_copy_public_key(GtkWidget*, gpointer d) {
    auto* self = static_cast<App*>(d);
    if (self->current_pubkey_hex_.empty()) return;
    GdkDisplay* display = gtk_widget_get_display(self->window_);
    GdkClipboard* clipboard = gdk_display_get_clipboard(display);
    gdk_clipboard_set_text(clipboard, self->current_pubkey_hex_.c_str());
    gtk_button_set_label(GTK_BUTTON(self->profile_pubkey_copy_btn_), "Copied!");
    g_timeout_add_seconds(2, [](gpointer d) -> gboolean {
        auto* btn = static_cast<GtkWidget*>(d);
        gtk_button_set_label(GTK_BUTTON(btn), "Copy");
        return G_SOURCE_REMOVE;
    }, self->profile_pubkey_copy_btn_);
}

void App::on_login(GtkWidget*, gpointer d) {
    auto* self = static_cast<App*>(d);
    GtkEntryBuffer* buf = gtk_entry_get_buffer(GTK_ENTRY(self->login_key_entry_));
    const char* key_hex = gtk_entry_buffer_get_text(buf);

    if (!key_hex || strlen(key_hex) != 64) {
        gtk_label_set_markup(GTK_LABEL(self->auth_status_label_),
            "<span color='#e74c3c'>Invalid key - must be 64 hex characters</span>");
        return;
    }

    auto id = self->identity_.login(key_hex);
    if (!id) {
        gtk_label_set_markup(GTK_LABEL(self->auth_status_label_),
            "<span color='#e74c3c'>Failed to derive public key - invalid key</span>");
        return;
    }

    self->identity_.save(*id);
    self->current_privkey_hex_ = key_hex;
    self->current_pubkey_hex_ = IdentityManager::pubkey_hex(*id);
    self->logged_in_ = true;
    self->init_session();
    self->switch_to_main();
}

void App::build_profile_page(GtkWidget* nb) {
    GtkWidget* pg = make_page(nb, "Profile");

    GtkWidget* t = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(t), "<span size='xx-large' weight='bold'>Your Profile</span>");
    gtk_box_append(GTK_BOX(pg), t);
    gtk_box_append(GTK_BOX(pg), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    gtk_box_append(GTK_BOX(pg), gtk_label_new("Display Name:"));
    profile_name_entry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(profile_name_entry_), "Enter display name...");
    gtk_box_append(GTK_BOX(pg), profile_name_entry_);
    GtkWidget* name_btn = gtk_button_new_with_label("Save Name");
    g_signal_connect(name_btn, "clicked", G_CALLBACK(on_set_display_name), &app_data);
    gtk_box_append(GTK_BOX(pg), name_btn);

    gtk_box_append(GTK_BOX(pg), gtk_label_new("Your Public Key:"));
    GtkWidget* pubkey_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    profile_pubkey_label_ = gtk_label_new(NULL);
    gtk_widget_set_hexpand(profile_pubkey_label_, TRUE);
    gtk_label_set_xalign(GTK_LABEL(profile_pubkey_label_), 0);
    gtk_label_set_wrap(GTK_LABEL(profile_pubkey_label_), TRUE);
    gtk_label_set_markup(GTK_LABEL(profile_pubkey_label_), "<span font_family='monospace' size='small'>Not logged in</span>");
    gtk_box_append(GTK_BOX(pubkey_hbox), profile_pubkey_label_);
    profile_pubkey_copy_btn_ = gtk_button_new_with_label("Copy");
    g_signal_connect(profile_pubkey_copy_btn_, "clicked", G_CALLBACK(on_copy_public_key), this);
    gtk_box_append(GTK_BOX(pubkey_hbox), profile_pubkey_copy_btn_);
    gtk_box_append(GTK_BOX(pg), pubkey_hbox);

    gtk_box_append(GTK_BOX(pg), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    GtkWidget* at = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(at), "<span weight='bold'>Achievement Summary</span>");
    gtk_label_set_xalign(GTK_LABEL(at), 0);
    gtk_box_append(GTK_BOX(pg), at);
    profile_ach_summary_ = gtk_label_new("0 / 0 unlocked");
    gtk_label_set_xalign(GTK_LABEL(profile_ach_summary_), 0);
    gtk_box_append(GTK_BOX(pg), profile_ach_summary_);
}

void App::on_set_display_name(GtkWidget*, gpointer) {
    auto* self = app_data.self;
    GtkEntryBuffer* buf = gtk_entry_get_buffer(GTK_ENTRY(self->profile_name_entry_));
    const char* name = gtk_entry_buffer_get_text(buf);
    if (name && strlen(name) > 0) {
        auto id = self->identity_.load();
        if (id) {
            id->display_name = name;
            self->identity_.save(*id);
            self->persistence_.save_profile(
                IdentityManager::pubkey_hex(*id), name, "");
        }
    }
}

void App::build_achievements_page(GtkWidget* nb) {
    GtkWidget* pg = make_page(nb, "Achievements");
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_box_append(GTK_BOX(pg), hbox);

    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(left, 350, -1);
    gtk_box_append(GTK_BOX(hbox), left);

    GtkWidget* lt = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lt), "<span weight='bold' size='large'>Achievements</span>");
    gtk_label_set_xalign(GTK_LABEL(lt), 0);
    gtk_box_append(GTK_BOX(left), lt);

    ach_progress_label_ = gtk_label_new("0 / 0 unlocked");
    gtk_label_set_xalign(GTK_LABEL(ach_progress_label_), 0);
    gtk_box_append(GTK_BOX(left), ach_progress_label_);

    GtkWidget* sc = gtk_button_new_with_label("Scan Now");
    gtk_widget_add_css_class(sc, "suggested-action");
    g_signal_connect(sc, "clicked", G_CALLBACK(on_scan_now), &app_data);
    gtk_box_append(GTK_BOX(left), sc);

    ach_store_ = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);
    GtkWidget* sw = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_box_append(GTK_BOX(left), sw);

    ach_list_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ach_store_));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), ach_list_);

    GtkCellRenderer* ri = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(ach_list_),
        gtk_tree_view_column_new_with_attributes(" ", ri, "text", 0, nullptr));
    GtkCellRenderer* rn = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(ach_list_),
        gtk_tree_view_column_new_with_attributes("Achievement", rn, "text", 1, nullptr));
    GtkCellRenderer* rs = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(ach_list_),
        gtk_tree_view_column_new_with_attributes("Status", rs, "text", 4, nullptr));

    GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_vexpand(right, TRUE);
    gtk_box_append(GTK_BOX(hbox), right);

    ach_detail_icon_ = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(ach_detail_icon_), "<span size='xx-large'>🏆</span>");
    gtk_box_append(GTK_BOX(right), ach_detail_icon_);
    ach_detail_name_ = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(ach_detail_name_), "<span size='large' weight='bold'>Select an achievement</span>");
    gtk_label_set_xalign(GTK_LABEL(ach_detail_name_), 0);
    gtk_box_append(GTK_BOX(right), ach_detail_name_);
    ach_detail_tier_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(ach_detail_tier_), 0);
    gtk_box_append(GTK_BOX(right), ach_detail_tier_);
    ach_detail_desc_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(ach_detail_desc_), 0);
    gtk_label_set_wrap(GTK_LABEL(ach_detail_desc_), TRUE);
    gtk_box_append(GTK_BOX(right), ach_detail_desc_);

    g_signal_connect(ach_list_, "cursor-changed", G_CALLBACK(on_achievement_selected), &app_data);

    GtkWidget* export_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(export_box, 8);

    GtkWidget* export_btn = gtk_button_new_with_label("Export Achievements");
    g_signal_connect(export_btn, "clicked", G_CALLBACK(on_export_achievements), &app_data);
    gtk_box_append(GTK_BOX(export_box), export_btn);

    GtkWidget* import_btn = gtk_button_new_with_label("Import Achievements");
    g_signal_connect(import_btn, "clicked", G_CALLBACK(on_import_achievements), &app_data);
    gtk_box_append(GTK_BOX(export_box), import_btn);

    gtk_box_append(GTK_BOX(right), export_box);
}

void App::on_scan_now(GtkWidget*, gpointer) {
    auto* self = app_data.self;
    self->scanner_.scan_now();
    self->refresh_achievements();
}

void App::on_achievement_selected(GtkTreeView* view, gpointer) {
    auto* self = static_cast<App*>(app_data.self);
    GtkTreeSelection* sel = gtk_tree_view_get_selection(view);
    if (!sel) return;
    GtkTreeModel* model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

    GtkTreePath* path = gtk_tree_model_get_path(model, &iter);
    int idx = gtk_tree_path_get_indices(path)[0];
    gtk_tree_path_free(path);

    auto achs = self->scanner_.get_achievements();
    if (idx < 0 || idx >= (int)achs.size()) return;
    auto& ach = achs[idx];

    char m[64];
    snprintf(m, sizeof(m), "<span size='xx-large'>%s</span>", ach.icon.c_str());
    gtk_label_set_markup(GTK_LABEL(self->ach_detail_icon_), m);
    snprintf(m, sizeof(m), "<span size='large' weight='bold'>%s</span>", ach.name.c_str());
    gtk_label_set_markup(GTK_LABEL(self->ach_detail_name_), m);

    const char* tc[] = {"#cd7f32", "#c0c0c0", "#ffd700", "#e5e4e2"};
    const char* tn[] = {"Bronze", "Silver", "Gold", "Platinum"};
    int ti = 0;
    if (ach.tier == "silver") ti = 1; else if (ach.tier == "gold") ti = 2; else if (ach.tier == "platinum") ti = 3;
    char tm[256];
    if (ach.unlocked && ach.unlocked_at > 0) {
        char dt[64];
        time_t t = (time_t)ach.unlocked_at;
        std::strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", localtime(&t));
        snprintf(tm, sizeof(tm),
            "<span color='%s' weight='bold'>%s</span>  ✅ UNLOCKED\n"
            "<span size='small' color='#27ae60'>Unlocked %s</span>",
            tc[ti], tn[ti], dt);
    } else {
        snprintf(tm, sizeof(tm),
            "<span color='%s' weight='bold'>%s</span>  🔒 LOCKED",
            tc[ti], tn[ti]);
    }
    gtk_label_set_markup(GTK_LABEL(self->ach_detail_tier_), tm);
    gtk_label_set_text(GTK_LABEL(self->ach_detail_desc_), ach.description.c_str());
}

void App::build_nodes_page(GtkWidget* nb) {
    GtkWidget* pg = make_page(nb, "Chat");

    node_stack_ = gtk_stack_new();
    gtk_widget_set_vexpand(node_stack_, TRUE);
    gtk_box_append(GTK_BOX(pg), node_stack_);

    GtkWidget* list_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(list_page, 8);
    gtk_widget_set_margin_start(list_page, 8);
    gtk_widget_set_margin_end(list_page, 8);
    gtk_stack_add_named(GTK_STACK(node_stack_), list_page, "list");

    GtkWidget* t = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(t), "<span size='xx-large' weight='bold'>Chat Rooms</span>");
    gtk_box_append(GTK_BOX(list_page), t);

    GtkWidget* note = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(note), 0);
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_markup(GTK_LABEL(note),
        "<span size='small' color='#888'>Rooms are hosted on your master. "
        "This subnode only joins them.</span>");
    gtk_box_append(GTK_BOX(list_page), note);

    node_scroll_ = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(node_scroll_, TRUE);
    gtk_box_append(GTK_BOX(list_page), node_scroll_);

    node_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(node_scroll_), node_box_);

    GtkWidget* chat_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(chat_page, 8);
    gtk_widget_set_margin_start(chat_page, 8);
    gtk_widget_set_margin_end(chat_page, 8);
    gtk_stack_add_named(GTK_STACK(node_stack_), chat_page, "chat");

    GtkWidget* chat_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* back_btn = gtk_button_new_with_label("Back");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_back_to_nodes), this);
    gtk_box_append(GTK_BOX(chat_header), back_btn);

    chat_title_ = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(chat_title_),
        "<span size='large' weight='bold'>Chat</span>");
    gtk_widget_set_hexpand(chat_title_, TRUE);
    gtk_box_append(GTK_BOX(chat_header), chat_title_);
    gtk_box_append(GTK_BOX(chat_page), chat_header);

    gtk_box_append(GTK_BOX(chat_page), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    chat_store_ = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* sw = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_box_append(GTK_BOX(chat_page), sw);
    chat_view_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(chat_store_));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), chat_view_);
    GtkCellRenderer* rh = gtk_cell_renderer_text_new();
    g_object_set(rh, "weight", 700, nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(chat_view_),
        gtk_tree_view_column_new_with_attributes("From", rh, "text", 0, nullptr));
    GtkCellRenderer* rc = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(chat_view_),
        gtk_tree_view_column_new_with_attributes("Message", rc, "text", 1, nullptr));

    GtkWidget* ib = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    chat_input_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(chat_input_), "Type a message...");
    gtk_widget_set_hexpand(chat_input_, TRUE);
    gtk_widget_set_sensitive(chat_input_, FALSE);
    gtk_box_append(GTK_BOX(ib), chat_input_);
    GtkWidget* sb = gtk_button_new_with_label("Send");
    gtk_widget_add_css_class(sb, "suggested-action");
    g_signal_connect(sb, "clicked", G_CALLBACK(on_send_message), &app_data);
    g_signal_connect(chat_input_, "activate", G_CALLBACK(on_send_message), &app_data);
    gtk_box_append(GTK_BOX(ib), sb);
    gtk_box_append(GTK_BOX(chat_page), ib);
}

std::string App::resolve_display_name(const std::string& pubkey) const {
    auto it = display_name_cache_.find(pubkey);
    if (it != display_name_cache_.end()) return it->second;
    if (pubkey == current_pubkey_hex_) return "You";
    return std::string("@") + pubkey.substr(0, 8);
}

void App::publish_profile() {
    if (!relay_.is_connected()) return;
    auto id = identity_.load();
    if (!id) return;

    std::string display_name = id->display_name.empty() ? "User" : id->display_name;
    relay_.send_profile(display_name, "", IdentityManager::privkey_hex(*id), IdentityManager::pubkey_hex(*id));

    display_name_cache_[current_pubkey_hex_] = display_name;
}

void App::publish_achievement(const StoredAchievement& ach) {
    if (!relay_.is_connected()) return;
    auto id = identity_.load();
    if (!id) return;

    relay_.send_achievement(ach.id, ach.name, ach.description, ach.icon,
                            IdentityManager::privkey_hex(*id), IdentityManager::pubkey_hex(*id));
}

void App::sync_achievements_from_relay() {
    // Pull this subnode's own achievements from the relay; the event callback
    // merges any unlocked ones back into local state.
    if (!relay_.is_connected()) return;
    if (current_pubkey_hex_.empty()) return;
    relay_.sync_achievements(current_pubkey_hex_);
}

void App::on_export_achievements(GtkWidget*, gpointer d) {
    (void)d;
    auto* self = app_data.self;

    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Export Achievements");
    gtk_file_dialog_save(dialog, GTK_WINDOW(self->window_), nullptr,
        [](GObject* source, GAsyncResult* result, gpointer data) {
            auto* self = static_cast<App*>(data);
            GtkFileDialog* dlg = GTK_FILE_DIALOG(source);
            GFile* file = gtk_file_dialog_save_finish(dlg, result, nullptr);
            if (!file) return;

            char* path = g_file_get_path(file);
            if (!path) { g_object_unref(file); return; }

            auto achievements = self->scanner_.get_achievements();
            self->persistence_.export_achievements(path, achievements, self->current_pubkey_hex_);
            g_free(path);
            g_object_unref(file);
        }, self);
}

void App::on_import_achievements(GtkWidget*, gpointer d) {
    (void)d;
    auto* self = app_data.self;

    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Import Achievements");
    gtk_file_dialog_open(dialog, GTK_WINDOW(self->window_), nullptr,
        [](GObject* source, GAsyncResult* result, gpointer data) {
            auto* self = static_cast<App*>(data);
            GtkFileDialog* dlg = GTK_FILE_DIALOG(source);
            GFile* file = gtk_file_dialog_open_finish(dlg, result, nullptr);
            if (!file) return;

            char* path = g_file_get_path(file);
            if (!path) { g_object_unref(file); return; }

            auto imported = self->persistence_.import_achievements(path);
            g_free(path);
            g_object_unref(file);

            if (imported.empty()) return;

            auto local = self->persistence_.load_achievements();
            std::map<std::string, StoredAchievement> merged;
            for (auto& a : local) merged[a.id] = a;
            for (auto& a : imported) {
                if (merged.find(a.id) == merged.end()) {
                    merged[a.id] = a;
                }
            }

            std::vector<StoredAchievement> merged_list;
            for (auto& [id, a] : merged) merged_list.push_back(a);
            self->persistence_.save_achievements(merged_list);
            self->refresh_achievements();
        }, self);
}

void App::on_back_to_nodes(GtkWidget*, gpointer d) {
    static_cast<App*>(d)->switch_to_node_list();
}

void App::switch_to_node_list() {
    gtk_stack_set_visible_child_name(GTK_STACK(node_stack_), "list");
}

void App::switch_to_node_chat() {
    gtk_stack_set_visible_child_name(GTK_STACK(node_stack_), "chat");
}

void App::on_send_message(GtkWidget*, gpointer) {
    auto* self = app_data.self;
    if (self->current_node_id_.empty()) return;
    GtkEntryBuffer* buf = gtk_entry_get_buffer(GTK_ENTRY(self->chat_input_));
    const char* text = gtk_entry_buffer_get_text(buf);
    if (!text || strlen(text) == 0) return;

    ChatMessage msg;
    auto id = self->identity_.load();
    msg.sender = id ? id->display_name : "You";
    msg.content = text;
    msg.timestamp = static_cast<uint64_t>(time(nullptr));
    msg.is_encrypted = false;
    self->messages_.push_back(msg);

    std::vector<StoredMessage> stored;
    for (auto& m : self->messages_) {
        stored.push_back({"", m.sender, m.content, m.timestamp, self->current_node_id_, m.is_encrypted});
    }
    self->persistence_.save_messages(self->current_node_id_, stored);

    if (self->relay_.is_connected() && id) {
        self->relay_.send_channel_message(
            self->current_node_id_, text,
            IdentityManager::privkey_hex(*id),
            IdentityManager::pubkey_hex(*id));
    }

    self->refresh_chat();
    gtk_entry_buffer_set_text(buf, "", -1);
}

void App::build_settings_page(GtkWidget* nb) {
    GtkWidget* pg = make_page(nb, "Settings");
    GtkWidget* t = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(t), "<span size='xx-large' weight='bold'>Settings</span>");
    gtk_box_append(GTK_BOX(pg), t);

    GtkWidget* tor_info = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(tor_info), 0);
    gtk_label_set_markup(GTK_LABEL(tor_info),
        "<span weight='bold'>Privacy: All connections routed through Tor</span>\n"
        "<span size='small'>Your IP is never exposed to relays. "
        "Requires tor service running (apt install tor and systemctl start tor)</span>");
    gtk_box_append(GTK_BOX(pg), tor_info);
    gtk_box_append(GTK_BOX(pg), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget* master_hdr = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(master_hdr), 0);
    gtk_label_set_markup(GTK_LABEL(master_hdr),
        "<span weight='bold' size='large'>Master (your hub)</span>");
    gtk_box_append(GTK_BOX(pg), master_hdr);

    GtkWidget* master_desc = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(master_desc), 0);
    gtk_label_set_wrap(GTK_LABEL(master_desc), TRUE);
    gtk_label_set_markup(GTK_LABEL(master_desc),
        "<span size='small'>This machine is a subnode. Everything — chat, "
        "achievements, networking — routes through your one master. Enter its "
        "URL below, or scan and register with one below.</span>");
    gtk_box_append(GTK_BOX(pg), master_desc);

    GtkWidget* url_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    master_url_entry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(master_url_entry_), "ws://192.168.1.50:7777");
    gtk_widget_set_hexpand(master_url_entry_, TRUE);
    if (!persistence_.load_master_url().empty()) {
        gtk_editable_set_text(GTK_EDITABLE(master_url_entry_),
            persistence_.load_master_url().c_str());
    }
    gtk_box_append(GTK_BOX(url_box), master_url_entry_);

    GtkWidget* conn = gtk_button_new_with_label("Connect");
    gtk_widget_add_css_class(conn, "suggested-action");
    g_signal_connect(conn, "clicked", G_CALLBACK(on_connect_master), &app_data);
    gtk_box_append(GTK_BOX(url_box), conn);
    gtk_box_append(GTK_BOX(pg), url_box);

    master_conn_status_ = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(master_conn_status_), 0);
    gtk_label_set_wrap(GTK_LABEL(master_conn_status_), TRUE);
    gtk_box_append(GTK_BOX(pg), master_conn_status_);
    update_master_conn_status();

    gtk_box_append(GTK_BOX(pg), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // --- Register with a Master (subnode) ---
    GtkWidget* master_header = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(master_header), 0);
    gtk_label_set_markup(GTK_LABEL(master_header),
        "<span weight='bold' size='large'>Register with a Master</span>");
    gtk_box_append(GTK_BOX(pg), master_header);

    GtkWidget* master_sub = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(master_sub), 0);
    gtk_label_set_wrap(GTK_LABEL(master_sub), TRUE);
    gtk_label_set_markup(GTK_LABEL(master_sub),
        "<span size='small'>Scan your LAN for master nodes, then register this "
        "machine so the master stores and syncs your achievements.</span>");
    gtk_box_append(GTK_BOX(pg), master_sub);

    subnode_scan_list_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(pg), subnode_scan_list_);

    GtkWidget* disc_btn = gtk_button_new_with_label("Scan for Masters");
    gtk_widget_add_css_class(disc_btn, "suggested-action");
    g_signal_connect(disc_btn, "clicked", G_CALLBACK(on_discover_masters), &app_data);
    gtk_box_append(GTK_BOX(pg), disc_btn);

    GtkWidget* pass_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(pass_box), gtk_label_new("Passphrase (if required):"));
    subnode_register_passphrase_ = gtk_entry_new();
    gtk_widget_set_hexpand(subnode_register_passphrase_, TRUE);
    gtk_box_append(GTK_BOX(pass_box), subnode_register_passphrase_);
    gtk_box_append(GTK_BOX(pg), pass_box);

    subnode_register_status_ = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(subnode_register_status_), 0);
    gtk_label_set_wrap(GTK_LABEL(subnode_register_status_), TRUE);
    gtk_box_append(GTK_BOX(pg), subnode_register_status_);
    update_master_status("Not registered with any master.");

    gtk_box_append(GTK_BOX(pg), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget* role_hdr = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(role_hdr), 0);
    gtk_label_set_markup(GTK_LABEL(role_hdr),
        "<span weight='bold' size='large'>Role</span>");
    gtk_box_append(GTK_BOX(pg), role_hdr);

    GtkWidget* role_sub = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(role_sub), 0);
    gtk_label_set_wrap(GTK_LABEL(role_sub), TRUE);
    gtk_label_set_markup(GTK_LABEL(role_sub),
        "<span size='small'>This machine is a SUB node: it registers with a "
        "master and routes all chat + achievements through that master. "
        "Switch to master mode to host the hub for your LAN.</span>");
    gtk_box_append(GTK_BOX(pg), role_sub);

    GtkWidget* to_master = gtk_button_new_with_label("Switch to Master mode");
    g_signal_connect(to_master, "clicked", G_CALLBACK(on_role_switch_to_master), &app_data);
    gtk_widget_set_tooltip_text(to_master,
        "Make this machine the hub (master): embed the relay and host the "
        "rooms, leaderboard and other subnodes. You stop being a subnode.");
    gtk_box_append(GTK_BOX(pg), to_master);

    GtkWidget* to_hybrid = gtk_button_new_with_label("Switch to Hybrid mode (recommended)");
    gtk_widget_add_css_class(to_hybrid, "suggested-action");
    g_signal_connect(to_hybrid, "clicked", G_CALLBACK(on_role_switch_to_hybrid), &app_data);
    gtk_widget_set_tooltip_text(to_hybrid,
        "Run as BOTH master and subnode in one process: host the hub AND be "
        "your own node (chat, achievements, stats). Best for a "
        "single-instance setup.");
    gtk_box_append(GTK_BOX(pg), to_hybrid);

    GtkWidget* info = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(info), 0);
    gtk_label_set_wrap(GTK_LABEL(info), TRUE);
    gtk_label_set_markup(GTK_LABEL(info),
        "<span size='small'>Your key is saved locally in ~/.lili/ - never sent to any server.\n"
        "Copy your private key to move your identity to another machine.\n"
        "All relay connections are routed through Tor for privacy.</span>");
    gtk_box_append(GTK_BOX(pg), info);
}

void App::update_master_conn_status() {
    if (!master_conn_status_) return;
    std::string url = persistence_.load_master_url();
    if (url.empty()) {
        gtk_label_set_markup(GTK_LABEL(master_conn_status_),
            "<span size='small'>No master set. Enter a master URL above, or "
            "scan + register below to connect to one.</span>");
    } else if (relay_.is_connected() && current_relay_url_ == url) {
        std::string markup = "<span size='small' foreground='#2ecc71'>"
            "Connected to master: " + url + "</span>";
        gtk_label_set_markup(GTK_LABEL(master_conn_status_), markup.c_str());
    } else {
        std::string markup = "<span size='small' foreground='#e74c3c'>"
            "Not connected. Master: " + url + "</span>";
        gtk_label_set_markup(GTK_LABEL(master_conn_status_), markup.c_str());
    }
}

void App::refresh_achievements() {
    gtk_list_store_clear(ach_store_);
    auto achs = scanner_.get_achievements();
    int uc = 0;
    for (auto& a : achs) {
        GtkTreeIter it;
        gtk_list_store_append(ach_store_, &it);
        char status[96];
        if (a.unlocked && a.unlocked_at > 0) {
            char dt[32];
            time_t t = (time_t)a.unlocked_at;
            std::strftime(dt, sizeof(dt), "%Y-%m-%d", localtime(&t));
            snprintf(status, sizeof(status), "✅ %s", dt);
        } else {
            snprintf(status, sizeof(status), "🔒 Locked");
        }
        gtk_list_store_set(ach_store_, &it,
            0, a.icon.c_str(), 1, a.name.c_str(), 2, a.tier.c_str(),
            3, a.unlocked ? TRUE : FALSE, 4, status, -1);
        if (a.unlocked) uc++;
    }
    char p[64];
    snprintf(p, sizeof(p), "%d / %d unlocked (%.0f%%)",
        uc, (int)achs.size(), achs.empty() ? 0.0f : (uc * 100.0f / achs.size()));
    gtk_label_set_text(GTK_LABEL(ach_progress_label_), p);

    if (profile_ach_summary_) {
        char ac[64];
        snprintf(ac, sizeof(ac), "Unlocked: %d / %d", uc, (int)achs.size());
        gtk_label_set_text(GTK_LABEL(profile_ach_summary_), ac);
    }
}

int App::find_node_index(const char* node_id) const {
    for (int i = 0; i < (int)nodes_.size(); i++) {
        if (nodes_[i].id == node_id) return i;
    }
    return -1;
}

void App::refresh_nodes() {
    while (GtkWidget* child = gtk_widget_get_first_child(node_box_)) {
        gtk_box_remove(GTK_BOX(node_box_), child);
    }

    for (int i = 0; i < (int)nodes_.size(); i++) {
        auto& n = nodes_[i];

        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_top(row, 6);
        gtk_widget_set_margin_bottom(row, 6);
        gtk_widget_set_margin_start(row, 8);
        gtk_widget_set_margin_end(row, 8);

        char name_buf[256];
        snprintf(name_buf, sizeof(name_buf), "<b>%s</b>", n.name.c_str());
        GtkWidget* name_lbl = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(name_lbl), name_buf);
        gtk_label_set_xalign(GTK_LABEL(name_lbl), 0);
        gtk_widget_set_hexpand(name_lbl, TRUE);
        gtk_box_append(GTK_BOX(row), name_lbl);

        GtkWidget* chat_btn = gtk_button_new_with_label("Open");
        gtk_widget_add_css_class(chat_btn, "suggested-action");
        g_object_set_data(G_OBJECT(chat_btn), "node_idx", GINT_TO_POINTER(i));
        g_signal_connect(chat_btn, "clicked", G_CALLBACK(on_chat_node_clicked), this);
        gtk_box_append(GTK_BOX(row), chat_btn);

        GtkWidget* info_btn = gtk_button_new_with_label("Info");
        g_object_set_data(G_OBJECT(info_btn), "node_idx", GINT_TO_POINTER(i));
        g_signal_connect(info_btn, "clicked", G_CALLBACK(on_node_info), this);
        gtk_box_append(GTK_BOX(row), info_btn);

        gtk_box_append(GTK_BOX(node_box_), row);

        if (i < (int)nodes_.size() - 1) {
            gtk_box_append(GTK_BOX(node_box_), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
        }
    }

    if (nodes_.empty()) {
        GtkWidget* empty = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(empty),
            "<span color='#888'>No rooms on your master yet.</span>");
        gtk_widget_set_margin_top(empty, 20);
        gtk_box_append(GTK_BOX(node_box_), empty);
    }
}

void App::on_chat_node_clicked(GtkWidget* w, gpointer d) {
    auto* self = static_cast<App*>(d);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "node_idx"));
    if (idx < 0 || idx >= (int)self->nodes_.size()) return;

    auto& node = self->nodes_[idx];
    self->current_node_id_ = node.id;

    char title[256];
    snprintf(title, sizeof(title), "<span size='large' weight='bold'>%s</span>",
        node.name.c_str());
    gtk_label_set_markup(GTK_LABEL(self->chat_title_), title);

    gtk_widget_set_sensitive(self->chat_input_, TRUE);

    self->messages_.clear();
    auto stored = self->persistence_.load_messages(self->current_node_id_);
    for (auto& m : stored) {
        ChatMessage cm;
        cm.sender = m.sender_name;
        cm.content = m.content;
        cm.timestamp = m.timestamp;
        cm.is_encrypted = m.is_encrypted;
        self->messages_.push_back(cm);
    }
    self->refresh_chat();

    self->switch_to_node_chat();
    // The room lives on this subnode's master (already connected).
}

void App::on_node_info(GtkWidget* w, gpointer d) {
    auto* self = static_cast<App*>(d);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "node_idx"));
    if (idx < 0 || idx >= (int)self->nodes_.size()) return;

    auto& n = self->nodes_[idx];

    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Room Info");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 350);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self->window_));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 16);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    auto add_info = [&](const char* label, const char* value) {
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* lbl = gtk_label_new(NULL);
        char markup[256];
        snprintf(markup, sizeof(markup), "<b>%s</b>", label);
        gtk_label_set_markup(GTK_LABEL(lbl), markup);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_widget_set_size_request(lbl, 120, -1);
        gtk_box_append(GTK_BOX(hbox), lbl);
        GtkWidget* val_lbl = gtk_label_new(value);
        gtk_label_set_xalign(GTK_LABEL(val_lbl), 0);
        gtk_label_set_wrap(GTK_LABEL(val_lbl), TRUE);
        gtk_widget_set_hexpand(val_lbl, TRUE);
        gtk_box_append(GTK_BOX(hbox), val_lbl);
        gtk_box_append(GTK_BOX(vbox), hbox);
    };

    add_info("Name:", n.name.c_str());
    if (!n.creator_pubkey.empty()) add_info("Creator:", n.creator_pubkey.c_str());
    if (!n.id.empty()) add_info("Room ID:", n.id.c_str());

    char time_buf[64];
    time_t t = static_cast<time_t>(n.created_at);
    struct tm* tm_info = localtime(&t);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", tm_info);
    add_info("Created:", time_buf);

    GtkWidget* close_btn = gtk_button_new_with_label("Close");
    g_signal_connect_swapped(close_btn, "clicked", G_CALLBACK(gtk_window_close), dialog);
    gtk_box_append(GTK_BOX(vbox), close_btn);

    gtk_window_present(GTK_WINDOW(dialog));
}

void App::refresh_chat() {
    gtk_list_store_clear(chat_store_);
    for (auto& msg : messages_) {
        GtkTreeIter it;
        gtk_list_store_append(chat_store_, &it);
        gtk_list_store_set(chat_store_, &it, 0, msg.sender.c_str(), 1, msg.content.c_str(), -1);
    }
}

void App::update_profile_summary() {
    auto id = identity_.load();
    if (id) {
        current_pubkey_hex_ = IdentityManager::pubkey_hex(*id);
        char m[128];
        snprintf(m, sizeof(m), "<span font_family='monospace' size='small'>%s</span>",
            current_pubkey_hex_.c_str());
        gtk_label_set_markup(GTK_LABEL(profile_pubkey_label_), m);
    }
}

void App::update_master_status(const std::string& text) {
    if (subnode_register_status_) {
        char markup[1024];
        snprintf(markup, sizeof(markup), "<span size='small'>%s</span>", text.c_str());
        gtk_label_set_markup(GTK_LABEL(subnode_register_status_), markup);
    }
}

void App::on_discover_masters(GtkWidget*, gpointer d) {
    (void)d;  // signal user-data not used; App is read from app_data.self
    auto* self = app_data.self;
    if (!self) return;
    self->update_master_status("Scanning the LAN for masters...");
    // Run the UDP discovery off the UI thread; it blocks up to the timeout.
    std::thread([self]() {
        auto found = std::make_shared<std::vector<DiscoveredMaster>>(
            lili::Discoverer().discover());
        g_idle_add([](gpointer data) -> gboolean {
            auto* ctx = static_cast<std::pair<App*,
                std::shared_ptr<std::vector<DiscoveredMaster>>>*>(data);
            App* s = ctx->first;
            s->discovered_masters_ = *(ctx->second);
            s->populate_master_list();
            if (s->discovered_masters_.empty()) {
                s->update_master_status(
                    "No masters found on the LAN. Start the master on another "
                    "machine and re-scan.");
            } else {
                char m[128];
                snprintf(m, sizeof(m),
                    "Found %zu master(s). Select one to register.",
                    s->discovered_masters_.size());
                s->update_master_status(m);
            }
            delete ctx;
            return G_SOURCE_REMOVE;
        }, new std::pair<App*, std::shared_ptr<std::vector<DiscoveredMaster>>>(
            self, found));
    }).detach();
}

void App::populate_master_list() {
    if (!subnode_scan_list_) return;
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(subnode_scan_list_)) != nullptr) {
        gtk_box_remove(GTK_BOX(subnode_scan_list_), child);
    }
    selected_master_index_ = -1;
    if (discovered_masters_.empty()) {
        gtk_box_append(GTK_BOX(subnode_scan_list_),
            gtk_label_new("No masters found. Click 'Scan for Masters'."));
        return;
    }
    for (size_t i = 0; i < discovered_masters_.size(); ++i) {
        const auto& m = discovered_masters_[i];
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        char name[256];
        snprintf(name, sizeof(name), "%s  (%s:%d)%s",
            m.name.c_str(), m.host.c_str(), (int)m.port,
            m.passphrase_required ? "  [passphrase]" : "");
        GtkWidget* lbl = gtk_label_new(name);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_box_append(GTK_BOX(row), lbl);

        GtkWidget* reg = gtk_button_new_with_label("Register");
        gtk_widget_add_css_class(reg, "suggested-action");
        g_signal_connect(reg, "clicked", G_CALLBACK(on_register_subnode),
            GINT_TO_POINTER((int)i));
        gtk_box_append(GTK_BOX(row), reg);
        gtk_box_append(GTK_BOX(subnode_scan_list_), row);
    }
}

void App::on_register_subnode(GtkWidget*, gpointer d) {
    auto* self = app_data.self;
    if (!self) return;
    int idx = GPOINTER_TO_INT(d);
    if (idx < 0 || idx >= (int)self->discovered_masters_.size()) return;
    self->selected_master_index_ = idx;
    const auto& m = self->discovered_masters_[idx];
    std::string url = "ws://" + m.host + ":" + std::to_string(m.port);

    auto id = self->identity_.load();
    if (!id) {
        self->update_master_status("No identity found; generate one first.");
        return;
    }

    std::string pass;
    if (self->subnode_register_passphrase_) {
        GtkEntryBuffer* buf = gtk_entry_get_buffer(
            GTK_ENTRY(self->subnode_register_passphrase_));
        const char* p = gtk_entry_buffer_get_text(buf);
        if (p) pass = p;
    }

    // Ensure we are connected to this master (connect is synchronous).
    if (self->current_relay_url_ != url || !self->relay_.is_connected()) {
        self->connect_to_relay(url);
    }
    // This master is now this subnode's single hub.
    self->persistence_.save_master_url(url);
    self->update_master_conn_status();

    self->relay_.set_register_ack_callback(
        [self](bool accepted, const std::string& msg) {
            self->ack_accepted_ = accepted;
            self->ack_message_ = msg;
            g_idle_add([](gpointer data) -> gboolean {
                auto* s = static_cast<App*>(data);
                if (s->ack_accepted_) {
                    s->registered_ = true;
                    s->update_master_status(
                        "Registered with master. Pulling achievements...");
                    s->sync_achievements_from_relay();
                    g_timeout_add_seconds(60, [](gpointer d2) -> gboolean {
                        return static_cast<App*>(d2)->heartbeat_tick();
                    }, s);
                } else {
                    s->update_master_status(
                        "Registration denied: " + s->ack_message_);
                }
                return G_SOURCE_REMOVE;
            }, self);
        });

    self->update_master_status("Sending registration to " + url + "...");
    self->relay_.register_subnode(id->display_name, m.host, "", "", pass,
                                  IdentityManager::privkey_hex(*id),
                                  IdentityManager::pubkey_hex(*id));
}

gboolean App::heartbeat_tick() {
    if (!registered_) return G_SOURCE_REMOVE;
    auto id = identity_.load();
    if (!id) return G_SOURCE_REMOVE;
    relay_.send_heartbeat(0, (uint32_t)scanner_.get_unlocked_count(),
                          IdentityManager::privkey_hex(*id),
                          IdentityManager::pubkey_hex(*id));
    return G_SOURCE_CONTINUE;
}

// ---------------------------------------------------------------------------
// Role-based UI: master dashboard vs subnode client.
// ---------------------------------------------------------------------------

void App::apply_role() {
    // Default + recommended mode is hybrid: one process is both the master hub
    // and a subnode. An explicitly-saved master/subnode choice is respected.
    std::string saved = persistence_.load_role();
    if (saved == "master") role_ = "master";
    else if (saved == "subnode") role_ = "subnode";
    else role_ = "hybrid";

    // (Re)start the embedded relay so its bind matches the role: a hybrid hub
    // is loopback-only and private, a master hub is open to the LAN. Stopping
    // first guarantees the switch rebinds correctly (e.g. hybrid -> master).
    stop_master_server();
    if (role_ == "master" || role_ == "hybrid") {
        start_master_server();
    }
    rebuild_notebook();
}

void App::rebuild_notebook() {
    if (!notebook_) return;
    while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook_)) > 0) {
        gtk_notebook_remove_page(GTK_NOTEBOOK(notebook_), 0);
    }
    // Subnode-only widget pointers are (re)set by the page builders; clear them
    // so master mode doesn't hold stale references.
    master_url_entry_ = nullptr;
    master_conn_status_ = nullptr;
    subnode_scan_list_ = nullptr;
    subnode_register_passphrase_ = nullptr;
    subnode_register_status_ = nullptr;
    master_subnode_list_ = nullptr;
    master_detail_label_ = nullptr;
    master_name_entry_ = nullptr;
    master_port_entry_ = nullptr;
    master_passphrase_entry_ = nullptr;
    master_start_stop_btn_ = nullptr;
    master_status_label_ = nullptr;
    master_settings_status_ = nullptr;
    master_room_entry_ = nullptr;
    leaderboard_list_ = nullptr;

    if (role_ == "master") {
        build_master_dashboard_page(notebook_);
        build_master_settings_page(notebook_);
        build_leaderboard_page(notebook_);
    } else if (role_ == "hybrid") {
        build_master_dashboard_page(notebook_);
        build_leaderboard_page(notebook_);
        build_profile_page(notebook_);
        build_achievements_page(notebook_);
        build_nodes_page(notebook_);
        build_hybrid_settings_page(notebook_);
    } else {
        build_profile_page(notebook_);
        build_achievements_page(notebook_);
        build_nodes_page(notebook_);
        build_settings_page(notebook_);
    }
}

void App::start_master_server() {
    if (master_server_) return;
    RelayConfig cfg;
    cfg.master_mode = true;
    cfg.master_name = persistence_.load_master_name();
    cfg.registration_passphrase = persistence_.load_master_passphrase();
    uint16_t port = persistence_.load_master_port();
    // Hybrid hub is private: reachable only from this machine (loopback, no
    // discovery), so other subnodes cannot connect to a hybrid node.
    cfg.loopback_only = (role_ == "hybrid");
    master_server_ = std::make_unique<RelayServer>(port, cfg);
    master_server_->start();
    if (master_refresh_source_ == 0) {
        master_refresh_source_ = g_timeout_add_seconds(3, master_refresh_cb, this);
    }
}

void App::stop_master_server() {
    if (master_refresh_source_ != 0) {
        g_source_remove(master_refresh_source_);
        master_refresh_source_ = 0;
    }
    if (master_server_) {
        master_server_->stop();
        master_server_.reset();
    }
}

gboolean App::master_refresh_cb(gpointer d) {
    auto* self = static_cast<App*>(d);
    if (!self->master_server_) return G_SOURCE_REMOVE;
    self->refresh_master_dashboard();
    self->refresh_leaderboard();
    return G_SOURCE_CONTINUE;
}

void App::build_master_dashboard_page(GtkWidget* nb) {
    GtkWidget* pg = make_page(nb, "Dashboard");

    master_status_label_ = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(master_status_label_), 0);
    gtk_label_set_wrap(GTK_LABEL(master_status_label_), TRUE);
    gtk_box_append(GTK_BOX(pg), master_status_label_);

    gtk_box_append(GTK_BOX(pg), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget* h = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(h), 0);
    gtk_label_set_markup(GTK_LABEL(h),
        "<span weight='bold' size='large'>Registered Subnodes</span>");
    gtk_box_append(GTK_BOX(pg), h);

    GtkWidget* sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(sc, TRUE);
    master_subnode_list_ = gtk_list_box_new();
    g_signal_connect(master_subnode_list_, "row-selected",
        G_CALLBACK(on_master_subnode_selected), &app_data);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), master_subnode_list_);
    gtk_box_append(GTK_BOX(pg), sc);

    master_detail_label_ = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(master_detail_label_), 0);
    gtk_label_set_wrap(GTK_LABEL(master_detail_label_), TRUE);
    gtk_box_append(GTK_BOX(pg), master_detail_label_);

    refresh_master_dashboard();
}

void App::build_master_settings_page(GtkWidget* nb) {
    GtkWidget* pg = make_page(nb, "Master Settings");

    GtkWidget* h = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(h), 0);
    gtk_label_set_markup(GTK_LABEL(h), "<span weight='bold' size='large'>Master Node</span>");
    gtk_box_append(GTK_BOX(pg), h);

    gtk_box_append(GTK_BOX(pg), gtk_label_new("Name (shown to subnodes):"));
    master_name_entry_ = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(master_name_entry_),
        persistence_.load_master_name().c_str());
    gtk_box_append(GTK_BOX(pg), master_name_entry_);

    gtk_box_append(GTK_BOX(pg), gtk_label_new("Port:"));
    master_port_entry_ = gtk_entry_new();
    char p[16];
    snprintf(p, sizeof(p), "%u", (unsigned)persistence_.load_master_port());
    gtk_editable_set_text(GTK_EDITABLE(master_port_entry_), p);
    gtk_box_append(GTK_BOX(pg), master_port_entry_);

    gtk_box_append(GTK_BOX(pg), gtk_label_new(
        "Registration passphrase (empty = open registration):"));
    master_passphrase_entry_ = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(master_passphrase_entry_), FALSE);
    gtk_editable_set_text(GTK_EDITABLE(master_passphrase_entry_),
        persistence_.load_master_passphrase().c_str());
    gtk_box_append(GTK_BOX(pg), master_passphrase_entry_);

    GtkWidget* save = gtk_button_new_with_label("Apply Settings");
    gtk_widget_add_css_class(save, "suggested-action");
    g_signal_connect(save, "clicked", G_CALLBACK(on_master_save_settings), &app_data);
    gtk_box_append(GTK_BOX(pg), save);

    master_start_stop_btn_ = gtk_button_new_with_label(
        master_server_ ? "Stop Master" : "Start Master");
    g_signal_connect(master_start_stop_btn_, "clicked",
        G_CALLBACK(on_master_start_stop), &app_data);
    gtk_box_append(GTK_BOX(pg), master_start_stop_btn_);

    master_settings_status_ = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(master_settings_status_), 0);
    gtk_label_set_wrap(GTK_LABEL(master_settings_status_), TRUE);
    gtk_box_append(GTK_BOX(pg), master_settings_status_);
    gtk_label_set_text(GTK_LABEL(master_settings_status_),
        master_server_ ? "Master running." : "Master stopped.");

    gtk_box_append(GTK_BOX(pg), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget* rooms_hdr = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(rooms_hdr), 0);
    gtk_label_set_markup(GTK_LABEL(rooms_hdr),
        "<span weight='bold' size='large'>Chat Rooms</span>");
    gtk_box_append(GTK_BOX(pg), rooms_hdr);

    GtkWidget* rooms_sub = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(rooms_sub), 0);
    gtk_label_set_wrap(GTK_LABEL(rooms_sub), TRUE);
    gtk_label_set_markup(GTK_LABEL(rooms_sub),
        "<span size='small'>Rooms are hosted on this master. Subnodes list "
        "and join them from here.</span>");
    gtk_box_append(GTK_BOX(pg), rooms_sub);

    GtkWidget* room_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    master_room_entry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(master_room_entry_), "New room name...");
    gtk_widget_set_hexpand(master_room_entry_, TRUE);
    gtk_box_append(GTK_BOX(room_box), master_room_entry_);

    GtkWidget* create = gtk_button_new_with_label("Create Room");
    gtk_widget_add_css_class(create, "suggested-action");
    g_signal_connect(create, "clicked", G_CALLBACK(on_master_create_room), &app_data);
    gtk_box_append(GTK_BOX(room_box), create);
    gtk_box_append(GTK_BOX(pg), room_box);

    GtkWidget* role = gtk_button_new_with_label("Switch to Subnode mode");
    g_signal_connect(role, "clicked", G_CALLBACK(on_role_switch_to_subnode), &app_data);
    gtk_widget_set_tooltip_text(role,
        "Make this machine a pure SUB node: stop the embedded hub and register "
        "with a master you choose (set it in the subnode Settings).");
    gtk_box_append(GTK_BOX(pg), role);

    GtkWidget* role_hybrid = gtk_button_new_with_label("Switch to Hybrid mode (recommended)");
    gtk_widget_add_css_class(role_hybrid, "suggested-action");
    g_signal_connect(role_hybrid, "clicked", G_CALLBACK(on_role_switch_to_hybrid), &app_data);
    gtk_widget_set_tooltip_text(role_hybrid,
        "Run as BOTH master and subnode in one process: this machine hosts the "
        "hub (rooms, leaderboard, other subnodes) AND acts as its own node "
        "(chat, achievements, stats). Best for a single-instance setup.");
    gtk_box_append(GTK_BOX(pg), role_hybrid);
}

void App::build_hybrid_settings_page(GtkWidget* nb) {
    GtkWidget* pg = make_page(nb, "Settings");

    GtkWidget* h = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(h), 0);
    gtk_label_set_markup(GTK_LABEL(h), "<span weight='bold' size='large'>Hybrid Node</span>");
    gtk_box_append(GTK_BOX(pg), h);

    GtkWidget* sub = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(sub), 0);
    gtk_label_set_wrap(GTK_LABEL(sub), TRUE);
    gtk_label_set_markup(GTK_LABEL(sub),
        "<span size='small'>This machine is BOTH the hub (master) and a node. "
        "It hosts the relay and rooms, and it is also its own first subnode "
        "(chat, achievements, stats). The hub is private — it runs on "
        "loopback only, so other subnodes cannot connect to a hybrid node.</span>");
    gtk_box_append(GTK_BOX(pg), sub);

    gtk_box_append(GTK_BOX(pg), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    gtk_box_append(GTK_BOX(pg), gtk_label_new("Hub name:"));
    master_name_entry_ = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(master_name_entry_),
        persistence_.load_master_name().c_str());
    gtk_box_append(GTK_BOX(pg), master_name_entry_);

    GtkWidget* save = gtk_button_new_with_label("Apply Hub Name");
    g_signal_connect(save, "clicked", G_CALLBACK(on_hybrid_save_settings), &app_data);
    gtk_box_append(GTK_BOX(pg), save);

    master_start_stop_btn_ = gtk_button_new_with_label(
        master_server_ ? "Stop Hub" : "Start Hub");
    g_signal_connect(master_start_stop_btn_, "clicked",
        G_CALLBACK(on_master_start_stop), &app_data);
    gtk_box_append(GTK_BOX(pg), master_start_stop_btn_);

    master_settings_status_ = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(master_settings_status_), 0);
    gtk_label_set_wrap(GTK_LABEL(master_settings_status_), TRUE);
    gtk_box_append(GTK_BOX(pg), master_settings_status_);
    gtk_label_set_text(GTK_LABEL(master_settings_status_),
        master_server_ ? "Hub running." : "Hub stopped.");

    gtk_box_append(GTK_BOX(pg), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget* rooms_hdr = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(rooms_hdr), 0);
    gtk_label_set_markup(GTK_LABEL(rooms_hdr),
        "<span weight='bold' size='large'>Chat Rooms</span>");
    gtk_box_append(GTK_BOX(pg), rooms_hdr);

    GtkWidget* rooms_sub = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(rooms_sub), 0);
    gtk_label_set_wrap(GTK_LABEL(rooms_sub), TRUE);
    gtk_label_set_markup(GTK_LABEL(rooms_sub),
        "<span size='small'>Rooms live on this hub. This machine and any "
        "joining node can chat in them.</span>");
    gtk_box_append(GTK_BOX(pg), rooms_sub);

    GtkWidget* room_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    master_room_entry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(master_room_entry_), "New room name...");
    gtk_widget_set_hexpand(master_room_entry_, TRUE);
    gtk_box_append(GTK_BOX(room_box), master_room_entry_);

    GtkWidget* create = gtk_button_new_with_label("Create Room");
    gtk_widget_add_css_class(create, "suggested-action");
    g_signal_connect(create, "clicked", G_CALLBACK(on_master_create_room), &app_data);
    gtk_box_append(GTK_BOX(room_box), create);
    gtk_box_append(GTK_BOX(pg), room_box);

    gtk_box_append(GTK_BOX(pg), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget* role_hdr = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(role_hdr), 0);
    gtk_label_set_markup(GTK_LABEL(role_hdr),
        "<span weight='bold' size='large'>Role</span>");
    gtk_box_append(GTK_BOX(pg), role_hdr);

    GtkWidget* role_sub = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(role_sub), 0);
    gtk_label_set_wrap(GTK_LABEL(role_sub), TRUE);
    gtk_label_set_markup(GTK_LABEL(role_sub),
        "<span size='small'>You are currently in hybrid mode. You can switch to "
        "a single role for a more focused interface.</span>");
    gtk_box_append(GTK_BOX(pg), role_sub);

    GtkWidget* to_master = gtk_button_new_with_label("Switch to Master mode");
    g_signal_connect(to_master, "clicked", G_CALLBACK(on_role_switch_to_master), &app_data);
    gtk_widget_set_tooltip_text(to_master,
        "Run only as the hub (master): host the relay, rooms, leaderboard and "
        "other subnodes, but no longer be a node yourself.");
    gtk_box_append(GTK_BOX(pg), to_master);

    GtkWidget* to_subnode = gtk_button_new_with_label("Switch to Subnode mode");
    g_signal_connect(to_subnode, "clicked", G_CALLBACK(on_role_switch_to_subnode), &app_data);
    gtk_widget_set_tooltip_text(to_subnode,
        "Run only as a node: stop hosting the hub and register with a master "
        "you choose (set it in the subnode Settings).");
    gtk_box_append(GTK_BOX(pg), to_subnode);
}

void App::build_leaderboard_page(GtkWidget* nb) {
    GtkWidget* pg = make_page(nb, "Leaderboard");

    GtkWidget* h = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(h), 0);
    gtk_label_set_markup(GTK_LABEL(h),
        "<span weight='bold' size='large'>Global Leaderboard</span>");
    gtk_box_append(GTK_BOX(pg), h);

    GtkWidget* sub = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(sub), 0);
    gtk_label_set_wrap(GTK_LABEL(sub), TRUE);
    gtk_label_set_markup(GTK_LABEL(sub),
        "<span size='small'>Ranked by terminal commands + uptime + achievements "
        "(nodes report system stats to this master).</span>");
    gtk_box_append(GTK_BOX(pg), sub);

    GtkWidget* sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(sc, TRUE);
    leaderboard_list_ = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), leaderboard_list_);
    gtk_box_append(GTK_BOX(pg), sc);
}

void App::refresh_master_dashboard() {
    if (!master_server_) return;
    auto subs = master_server_->get_subnodes();
    size_t active = 0;
    for (const auto& s : subs) if (s.active) active++;

    if (master_status_label_) {
        char h[512];
        snprintf(h, sizeof(h),
            "<span size='large' weight='bold'>%s</span>  ·  port %u  ·  "
            "<span color='%s'>%zu/%zu subnodes online</span>",
            master_server_->config().master_name.c_str(),
            (unsigned)master_server_->port(),
            active > 0 ? "#27ae60" : "#e74c3c", active, subs.size());
        gtk_label_set_markup(GTK_LABEL(master_status_label_), h);
    }

    if (master_subnode_list_) {
        GtkWidget* row;
        while ((row = gtk_widget_get_first_child(master_subnode_list_)) != nullptr) {
            gtk_list_box_remove(GTK_LIST_BOX(master_subnode_list_), row);
        }
        if (subs.empty()) {
            GtkWidget* lbl = gtk_label_new("No subnodes registered yet.");
            gtk_widget_set_margin_start(lbl, 12);
            gtk_widget_set_margin_top(lbl, 8);
            gtk_list_box_append(GTK_LIST_BOX(master_subnode_list_), lbl);
        } else {
            for (const auto& s : subs) {
                char txt[512];
                const char* dot = s.active ? "\xe2\x97\x8f" : "\xe2\x97\x8b";
                const char* col = s.active ? "#27ae60" : "#888";
                snprintf(txt, sizeof(txt),
                    "<span color='%s'>%s</span>  <b>%s</b>  ·  %s  ·  %s  ·  %s",
                    col, dot,
                    (s.display_name.empty() ? s.pubkey.substr(0, 8).c_str()
                                            : s.display_name.c_str()),
                    s.pubkey.substr(0, 16).c_str(),
                    (s.ip.empty() ? "?" : s.ip.c_str()),
                    s.active ? "online" : "offline");
                GtkWidget* lbl = gtk_label_new(NULL);
                gtk_label_set_xalign(GTK_LABEL(lbl), 0);
                gtk_label_set_markup(GTK_LABEL(lbl), txt);
                GtkWidget* r = gtk_list_box_row_new();
                gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(r), lbl);
                g_object_set_data_full(G_OBJECT(r), "pubkey",
                    g_strdup(s.pubkey.c_str()), g_free);
                gtk_list_box_append(GTK_LIST_BOX(master_subnode_list_), r);
            }
        }
    }
    update_master_detail();
}

void App::refresh_leaderboard() {
    if (!master_server_ || !leaderboard_list_) return;
    auto subs = master_server_->get_subnodes();
    std::vector<lili::StatsEvent> sevs;
    for (auto& e : master_server_->get_events(
            {static_cast<uint16_t>(lili::Event::Kind::STATS)}, "")) {
        lili::StatsEvent se;
        se.pubkey = e.pubkey;
        se.created_at = e.created_at;
        se.content = e.content;
        sevs.push_back(se);
    }
    auto board = lili::aggregate_leaderboard(sevs);
    for (auto& le : board) {
        for (auto& s : subs) {
            if (s.pubkey == le.pubkey) { le.display_name = s.display_name; le.active = s.active; break; }
        }
        if (le.display_name.empty()) le.display_name = le.stats.hostname;
        le.achievements = master_server_->get_events(
            {static_cast<uint16_t>(lili::Event::Kind::ACHIEVEMENT)}, le.pubkey).size();
        le.score = lili::leaderboard_score(le.stats, le.achievements, le.active);
    }
    std::sort(board.begin(), board.end(),
        [](const lili::LeaderboardEntry& a, const lili::LeaderboardEntry& b) {
            return a.score > b.score; });

    GtkWidget* row;
    while ((row = gtk_widget_get_first_child(leaderboard_list_)) != nullptr)
        gtk_list_box_remove(GTK_LIST_BOX(leaderboard_list_), row);

    if (board.empty()) {
        GtkWidget* lbl = gtk_label_new(
            "No node stats reported yet.\nSubnodes publish stats automatically "
            "while the app is open, or via: lili-cli stats report <master-url>");
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_widget_set_margin_start(lbl, 12);
        gtk_widget_set_margin_top(lbl, 8);
        gtk_list_box_append(GTK_LIST_BOX(leaderboard_list_), lbl);
        return;
    }

    int rank = 1;
    for (auto& le : board) {
        const char* medal = rank == 1 ? "🥇" : rank == 2 ? "🥈" : rank == 3 ? "🥉" : "  ";
        char mk[600];
        snprintf(mk, sizeof(mk),
            "<span size='x-large'>%s</span> <span weight='bold' size='large'>%s</span>"
            "  <span color='%s'>%s</span>\n"
            "<span size='small'>score %llu · %llu commands · uptime %lluh · "
            "%llu achievements · mem %llu/%lluMiB · %s</span>",
            medal, le.display_name.c_str(),
            le.active ? "#27ae60" : "#e74c3c", le.active ? "online" : "offline",
            (unsigned long long)le.score,
            (unsigned long long)le.stats.commands,
            (unsigned long long)(le.stats.uptime_seconds / 3600),
            (unsigned long long)le.achievements,
            (unsigned long long)le.stats.mem_used_mb,
            (unsigned long long)le.stats.mem_total_mb,
            le.stats.distro.empty() ? "unknown distro" : le.stats.distro.c_str());
        GtkWidget* r = gtk_list_box_row_new();
        GtkWidget* lbl = gtk_label_new(NULL);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_label_set_markup(GTK_LABEL(lbl), mk);
        gtk_widget_set_margin_start(lbl, 12);
        gtk_widget_set_margin_top(lbl, 8);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(r), lbl);
        gtk_list_box_append(GTK_LIST_BOX(leaderboard_list_), r);
        rank++;
    }
}

void App::on_master_subnode_selected(GtkListBox*, GtkListBoxRow* row, gpointer) {
    auto* self = app_data.self;
    if (!self) return;
    if (!row) {
        self->selected_subnode_pubkey_.clear();
    } else {
        const char* pk = (const char*)g_object_get_data(G_OBJECT(row), "pubkey");
        self->selected_subnode_pubkey_ = pk ? pk : "";
    }
    self->update_master_detail();
}

void App::update_master_detail() {
    if (!master_detail_label_) return;
    if (!master_server_ || selected_subnode_pubkey_.empty()) {
        gtk_label_set_markup(GTK_LABEL(master_detail_label_),
            "<span size='small'>Select a subnode to see its stored achievements.</span>");
        return;
    }
    auto evs = master_server_->get_events(
        {static_cast<uint16_t>(lili::Event::Kind::ACHIEVEMENT)},
        selected_subnode_pubkey_);
    if (evs.empty()) {
        gtk_label_set_markup(GTK_LABEL(master_detail_label_),
            "<span size='small'>No achievements stored for this subnode yet.</span>");
        return;
    }
    std::string out = "<span weight='bold'>Stored achievements</span>";
    for (const auto& e : evs) {
        std::string nm = e.content;
        try {
            auto c = nlohmann::json::parse(e.content);
            if (c.contains("name")) nm = c["name"].get<std::string>();
            else if (c.contains("achievement_id")) nm = c["achievement_id"].get<std::string>();
        } catch (...) {}
        out += "\n\xe2\x80\xa2 " + nm;
    }
    gtk_label_set_markup(GTK_LABEL(master_detail_label_), out.c_str());
}

void App::on_master_save_settings(GtkWidget*, gpointer d) {
    (void)d;
    auto* self = app_data.self;
    if (!self) return;
    const char* name = self->master_name_entry_
        ? gtk_editable_get_text(GTK_EDITABLE(self->master_name_entry_)) : "";
    const char* port = self->master_port_entry_
        ? gtk_editable_get_text(GTK_EDITABLE(self->master_port_entry_)) : "";
    const char* pass = self->master_passphrase_entry_
        ? gtk_editable_get_text(GTK_EDITABLE(self->master_passphrase_entry_)) : "";
    self->persistence_.save_master_name(name ? name : "");
    uint16_t pnum = (uint16_t)atoi((port && *port) ? port : "7777");
    self->persistence_.save_master_port(pnum);
    self->persistence_.save_master_passphrase(pass ? pass : "");
    // Restart the embedded master with the new settings.
    self->stop_master_server();
    self->start_master_server();
    if (self->master_settings_status_) {
        gtk_label_set_text(GTK_LABEL(self->master_settings_status_),
            "Settings applied. Master restarted.");
    }
    if (self->master_start_stop_btn_) {
        gtk_button_set_label(GTK_BUTTON(self->master_start_stop_btn_), "Stop Master");
    }
}

void App::on_hybrid_save_settings(GtkWidget*, gpointer d) {
    (void)d;
    auto* self = app_data.self;
    if (!self) return;
    const char* name = self->master_name_entry_
        ? gtk_editable_get_text(GTK_EDITABLE(self->master_name_entry_)) : "";
    self->persistence_.save_master_name(name ? name : "");
    if (self->master_server_) self->master_server_->set_master_name(name ? name : "");
    if (self->master_settings_status_)
        gtk_label_set_text(GTK_LABEL(self->master_settings_status_),
            "Hub name saved.");
}

void App::on_master_create_room(GtkWidget*, gpointer d) {
    (void)d;
    auto* self = app_data.self;
    if (!self) return;
    if (!self->master_server_) {
        if (self->master_settings_status_)
            gtk_label_set_text(GTK_LABEL(self->master_settings_status_),
                "Start the master first, then create rooms.");
        return;
    }
    GtkEntryBuffer* buf = gtk_entry_get_buffer(GTK_ENTRY(self->master_room_entry_));
    const char* name = gtk_entry_buffer_get_text(buf);
    if (!name || strlen(name) == 0) return;

    auto id = self->identity_.load();
    if (!id) {
        if (self->master_settings_status_)
            gtk_label_set_text(GTK_LABEL(self->master_settings_status_),
                "No identity to sign the room. Log in first.");
        return;
    }

    // Publish the room to this master's own embedded relay (same as the CLI
    // `chat create <name> <relay-url>`). Subnodes see it via NODE events.
    std::string url = "ws://127.0.0.1:" + std::to_string(self->master_server_->port());
    RelayClient rc;
    if (!rc.connect(url) || !rc.is_connected()) {
        if (self->master_settings_status_)
            gtk_label_set_text(GTK_LABEL(self->master_settings_status_),
                ("Could not reach the master relay at " + url).c_str());
        return;
    }
    std::string room_id = rc.publish_room(name,
        IdentityManager::privkey_hex(*id), IdentityManager::pubkey_hex(*id));
    rc.disconnect();

    if (self->master_settings_status_) {
        if (room_id.empty()) {
            gtk_label_set_text(GTK_LABEL(self->master_settings_status_),
                "Failed to create room.");
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "Room '%s' created. id: %.16s...",
                name, room_id.c_str());
            gtk_label_set_text(GTK_LABEL(self->master_settings_status_), msg);
        }
    }
    if (!room_id.empty()) gtk_entry_buffer_set_text(buf, "", -1);
}

void App::on_master_start_stop(GtkWidget*, gpointer d) {
    (void)d;
    auto* self = app_data.self;
    if (!self) return;
    if (self->master_server_) {
        self->stop_master_server();
        if (self->master_settings_status_)
            gtk_label_set_text(GTK_LABEL(self->master_settings_status_), "Master stopped.");
        if (self->master_start_stop_btn_)
            gtk_button_set_label(GTK_BUTTON(self->master_start_stop_btn_), "Start Master");
    } else {
        self->start_master_server();
        if (self->master_settings_status_)
            gtk_label_set_text(GTK_LABEL(self->master_settings_status_), "Master running.");
        if (self->master_start_stop_btn_)
            gtk_button_set_label(GTK_BUTTON(self->master_start_stop_btn_), "Stop Master");
    }
}

void App::on_role_switch_to_master(GtkWidget*, gpointer d) {
    (void)d;
    auto* self = app_data.self;
    if (!self) return;
    self->relay_.disconnect();          // subnodes route through the master, not a client relay
    self->scanner_.stop();              // a master is a pure hub; no local scanning
    self->persistence_.save_role("master");
    self->role_ = "master";
    self->apply_role();
}

void App::on_role_switch_to_subnode(GtkWidget*, gpointer d) {
    (void)d;
    auto* self = app_data.self;
    if (!self) return;
    self->persistence_.save_role("subnode");
    self->role_ = "subnode";
    self->apply_role();                 // stops the embedded master if any
    self->begin_subnode_work(false);
}

void App::on_role_switch_to_hybrid(GtkWidget*, gpointer d) {
    (void)d;
    auto* self = app_data.self;
    if (!self) return;
    self->persistence_.save_role("hybrid");
    self->role_ = "hybrid";
    self->apply_role();                 // starts the embedded master + combined UI
    self->begin_subnode_work(true);     // this process is also a subnode of itself
}

} // namespace lili
