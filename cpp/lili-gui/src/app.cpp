#include "lili-gui/app.hpp"
#include "lili-protocol/signing.hpp"
#include <nlohmann/json.hpp>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <iostream>
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

    GtkWidget* nb = gtk_notebook_new();
    gtk_widget_set_vexpand(nb, TRUE);
    gtk_stack_add_named(GTK_STACK(self->main_stack_), nb, "tabs");

    self->build_profile_page(nb);
    self->build_achievements_page(nb);
    self->build_nodes_page(nb);
    self->build_settings_page(nb);

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

    scanner_.set_persistence(&persistence_);
    scanner_.load_definitions();
    scanner_.start(60);
    scanner_.scan_now();

    refresh_achievements();
    refresh_nodes();
    update_profile_summary();

    auto relays = persistence_.load_relay_list();
    if (!relays.empty()) connect_to_relay(relays.front());
}

void App::load_persisted_data() {
    auto stored_nodes = persistence_.load_nodes();
    nodes_.clear();
    for (auto& n : stored_nodes) {
        NodeInfo ni;
        ni.id = n.id;
        ni.name = n.name;
        ni.description = n.description;
        ni.creator_pubkey = n.creator_pubkey;
        ni.admin_privkey = n.admin_privkey;
        ni.relay_url = n.relay_url;
        ni.created_at = n.created_at;
        ni.member_count = n.member_count;
        ni.is_local = n.is_local;
        ni.running = n.running;
        nodes_.push_back(ni);
    }
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

                if (!self->current_pubkey_hex_.empty()) {
                    self->relay_.subscribe(
                        {static_cast<int>(lili::Event::Kind::ACHIEVEMENT)},
                        self->current_pubkey_hex_);
                }

                self->relay_.subscribe(
                    {static_cast<int>(lili::Event::Kind::METADATA)});

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
            snprintf(label, sizeof(label), "Relay: %s", short_url.c_str());
            gtk_label_set_text(GTK_LABEL(relay_status_label_), label);
        } else {
            gtk_label_set_text(GTK_LABEL(relay_status_label_),
                connected ? "Relay: connected" : "Relay: not connected");
        }
    }
}

void App::on_connect_relay(GtkWidget*, gpointer d) {
    auto* self = static_cast<App*>(d);
    GtkEntryBuffer* buf = gtk_entry_get_buffer(GTK_ENTRY(self->relay_url_entry_));
    const char* url = gtk_entry_buffer_get_text(buf);
    if (!url || strlen(url) == 0) return;
    self->persistence_.save_relay_url(url);
    if (!self->current_node_id_.empty()) {
        auto it = std::find_if(self->nodes_.begin(), self->nodes_.end(),
            [&](const NodeInfo& n) { return n.id == self->current_node_id_; });
        if (it != self->nodes_.end() && !it->relay_url.empty()) {
            self->connect_to_relay(it->relay_url);
        }
    }
}

void App::on_add_relay(GtkWidget*, gpointer d) {
    (void)d;
    auto* self = app_data.self;
    GtkEntryBuffer* buf = gtk_entry_get_buffer(GTK_ENTRY(self->relay_url_entry_));
    const char* url = gtk_entry_buffer_get_text(buf);
    if (!url || strlen(url) == 0) return;

    std::string url_str(url);

    auto list = self->persistence_.load_relay_list();
    for (const auto& existing : list) {
        if (existing == url_str) return;
    }

    list.push_back(url_str);
    self->persistence_.save_relay_list(list);

    gtk_entry_buffer_set_text(buf, "", -1);
    self->refresh_relay_list();
}

void App::on_remove_relay(GtkWidget* w, gpointer d) {
    auto* self = app_data.self;
    int idx = GPOINTER_TO_INT(d);

    auto list = self->persistence_.load_relay_list();
    if (idx < 0 || idx >= (int)list.size()) return;

    list.erase(list.begin() + idx);
    self->persistence_.save_relay_list(list);
    self->refresh_relay_list();
}

void App::on_connect_relay_row(GtkWidget*, gpointer d) {
    auto* self = app_data.self;
    int idx = GPOINTER_TO_INT(d);

    auto list = self->persistence_.load_relay_list();
    if (idx < 0 || idx >= (int)list.size()) return;

    std::string url = list[idx];
    self->persistence_.save_relay_url(url);
    self->connect_to_relay(url);
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
    char tm[128];
    snprintf(tm, sizeof(tm), "<span color='%s' weight='bold'>%s</span>  %s",
        tc[ti], tn[ti], ach.unlocked ? "✅ UNLOCKED" : "🔒 LOCKED");
    gtk_label_set_markup(GTK_LABEL(self->ach_detail_tier_), tm);
    gtk_label_set_text(GTK_LABEL(self->ach_detail_desc_), ach.description.c_str());
}

void App::build_nodes_page(GtkWidget* nb) {
    GtkWidget* pg = make_page(nb, "Nodes");

    node_stack_ = gtk_stack_new();
    gtk_widget_set_vexpand(node_stack_, TRUE);
    gtk_box_append(GTK_BOX(pg), node_stack_);

    GtkWidget* list_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(list_page, 8);
    gtk_widget_set_margin_start(list_page, 8);
    gtk_widget_set_margin_end(list_page, 8);
    gtk_stack_add_named(GTK_STACK(node_stack_), list_page, "list");

    GtkWidget* t = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(t), "<span size='xx-large' weight='bold'>Chat Nodes</span>");
    gtk_box_append(GTK_BOX(list_page), t);

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "New node name...");
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_box_append(GTK_BOX(hbox), entry);
    GtkWidget* btn = gtk_button_new_with_label("Create Node");
    gtk_widget_add_css_class(btn, "suggested-action");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_create_node), &app_data);
    g_object_set_data(G_OBJECT(btn), "entry", entry);
    gtk_box_append(GTK_BOX(hbox), btn);
    gtk_box_append(GTK_BOX(list_page), hbox);

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

void App::on_create_node(GtkWidget* w, gpointer) {
    auto* self = app_data.self;
    auto* entry = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(w), "entry"));
    GtkEntryBuffer* buf = gtk_entry_get_buffer(GTK_ENTRY(entry));
    const char* name = gtk_entry_buffer_get_text(buf);
    if (!name || strlen(name) == 0) return;

    auto node_kp = lili::generate_keypair();
    std::string node_pub = IdentityManager::key_to_hex(node_kp.public_key.data(), 32);
    std::string node_priv = IdentityManager::key_to_hex(node_kp.secret_key.data(), 32);

    auto creator_id = self->identity_.load();
    std::string creator_pub = creator_id ? IdentityManager::pubkey_hex(*creator_id) : "";

    lili::Event node_event;
    node_event.kind = static_cast<uint16_t>(lili::Event::Kind::NODE);
    node_event.created_at = static_cast<uint64_t>(time(nullptr));
    node_event.content = name;
    node_event.pubkey = node_pub;
    node_event.tags = {
        {"d", node_pub},            // replaceable event tag
        {"name", name},
        {"relay", self->persistence_.load_relay_url()},
        {"creator", creator_pub}
    };

    auto signed_event = lili::sign_event(node_event, node_kp);

    std::string relay_url = self->persistence_.load_relay_url();

    NodeInfo nd;
    nd.id = std::to_string(signed_event.id);
    nd.name = name;
    nd.description = "";
    nd.creator_pubkey = creator_pub;
    nd.admin_privkey = node_priv;
    nd.relay_url = relay_url;
    nd.created_at = signed_event.created_at;
    nd.member_count = 1;
    nd.is_local = true;
    nd.running = true;
    self->nodes_.push_back(nd);

    self->save_nodes();

    if (self->relay_.is_connected()) {
        nlohmann::json j;
        j["id"] = std::to_string(signed_event.id);
        j["pubkey"] = signed_event.pubkey;
        j["kind"] = signed_event.kind;
        j["content"] = signed_event.content;
        j["created_at"] = signed_event.created_at;
        j["tags"] = signed_event.tags;
        j["sig"] = signed_event.sig;
        self->relay_.publish_event(j.dump());
    }

    self->refresh_nodes();
    gtk_entry_buffer_set_text(buf, "", -1);
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

    GtkWidget* relay_header = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(relay_header), 0);
    gtk_label_set_markup(GTK_LABEL(relay_header),
        "<span weight='bold' size='large'>Relay List</span>");
    gtk_box_append(GTK_BOX(pg), relay_header);

    GtkWidget* relay_sub = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(relay_sub), 0);
    gtk_label_set_wrap(GTK_LABEL(relay_sub), TRUE);
    gtk_label_set_markup(GTK_LABEL(relay_sub),
        "<span size='small'>Add relays to publish achievements and connect to nodes. "
        "Your node's relay is used when you open chat.</span>");
    gtk_box_append(GTK_BOX(pg), relay_sub);

    relay_list_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_vexpand(relay_list_box_, TRUE);
    gtk_box_append(GTK_BOX(pg), relay_list_box_);
    refresh_relay_list();

    GtkWidget* add_relay_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    relay_url_entry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(relay_url_entry_), "wss://relay.example.com");
    gtk_widget_set_hexpand(relay_url_entry_, TRUE);
    gtk_box_append(GTK_BOX(add_relay_box), relay_url_entry_);

    GtkWidget* add_btn = gtk_button_new_with_label("Add Relay");
    gtk_widget_add_css_class(add_btn, "suggested-action");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_relay), &app_data);
    gtk_box_append(GTK_BOX(add_relay_box), add_btn);
    gtk_box_append(GTK_BOX(pg), add_relay_box);

    gtk_box_append(GTK_BOX(pg), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    gtk_box_append(GTK_BOX(pg), gtk_label_new("Default Relay URL (used for new nodes):"));
    GtkWidget* default_entry = gtk_entry_new();
    GtkEntryBuffer* def_buf = gtk_entry_get_buffer(GTK_ENTRY(default_entry));
    gtk_entry_buffer_set_text(def_buf, persistence_.load_relay_url().c_str(), -1);
    g_signal_connect(default_entry, "changed", G_CALLBACK(on_relay_url_changed), &app_data);
    gtk_box_append(GTK_BOX(pg), default_entry);

    GtkWidget* info = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(info), 0);
    gtk_label_set_wrap(GTK_LABEL(info), TRUE);
    gtk_label_set_markup(GTK_LABEL(info),
        "<span size='small'>Your key is saved locally in ~/.lili/ - never sent to any server.\n"
        "Copy your private key to move your identity to another machine.\n"
        "All relay connections are routed through Tor for privacy.</span>");
    gtk_box_append(GTK_BOX(pg), info);
}

void App::refresh_relay_list() {
    if (!relay_list_box_) return;

    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(relay_list_box_)) != nullptr) {
        gtk_box_remove(GTK_BOX(relay_list_box_), child);
    }

    relay_list_ = persistence_.load_relay_list();

    if (relay_list_.empty()) {
        GtkWidget* empty = gtk_label_new("No relays configured. Add one above.");
        gtk_widget_set_margin_top(empty, 8);
        gtk_box_append(GTK_BOX(relay_list_box_), empty);
        return;
    }

    for (size_t i = 0; i < relay_list_.size(); i++) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_top(row, 4);
        gtk_widget_set_margin_bottom(row, 4);

        GtkWidget* url_label = gtk_label_new(relay_list_[i].c_str());
        gtk_label_set_xalign(GTK_LABEL(url_label), 0);
        gtk_widget_set_hexpand(url_label, TRUE);
        gtk_box_append(GTK_BOX(row), url_label);

        GtkWidget* connect_btn = gtk_button_new_with_label("Connect");
        gtk_widget_add_css_class(connect_btn, "suggested-action");
        g_signal_connect(connect_btn, "clicked", G_CALLBACK(on_connect_relay_row),
            GINT_TO_POINTER((int)i));
        gtk_box_append(GTK_BOX(row), connect_btn);

        GtkWidget* remove_btn = gtk_button_new_with_label("Remove");
        gtk_widget_add_css_class(remove_btn, "destructive-action");
        g_signal_connect(remove_btn, "clicked", G_CALLBACK(on_remove_relay),
            GINT_TO_POINTER((int)i));
        gtk_box_append(GTK_BOX(row), remove_btn);

        gtk_box_append(GTK_BOX(relay_list_box_), row);
    }
}

void App::on_relay_url_changed(GtkEditable* ed, gpointer d) {
    (void)d;
    auto* self = app_data.self;
    char* t = gtk_editable_get_chars(ed, 0, -1);
    self->persistence_.save_relay_url(t);
    g_free(t);
}

void App::refresh_achievements() {
    gtk_list_store_clear(ach_store_);
    auto achs = scanner_.get_achievements();
    int uc = 0;
    for (auto& a : achs) {
        GtkTreeIter it;
        gtk_list_store_append(ach_store_, &it);
        const char* status = a.unlocked ? "✅ Unlocked" : "🔒 Locked";
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

void App::save_nodes() {
    std::vector<StoredNode> stored;
    for (auto& n : nodes_) {
        stored.push_back({n.id, n.name, n.description, n.creator_pubkey,
            n.admin_privkey, n.relay_url, n.created_at, n.member_count,
            n.is_local, n.running});
    }
    persistence_.save_nodes(stored);
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

        GtkWidget* dot = gtk_label_new(NULL);
        const char* color = n.running ? "#27ae60" : "#e74c3c";
        char dot_markup[64];
        snprintf(dot_markup, sizeof(dot_markup), "<span color='%s'>\xe2\x97\x8f</span>", color);
        gtk_label_set_markup(GTK_LABEL(dot), dot_markup);
        gtk_box_append(GTK_BOX(row), dot);

        char name_buf[256];
        snprintf(name_buf, sizeof(name_buf), "<b>%s</b>  <span size='small' color='#888'>%s</span>",
            n.name.c_str(), n.is_local ? "(yours)" : "");
        GtkWidget* name_lbl = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(name_lbl), name_buf);
        gtk_label_set_xalign(GTK_LABEL(name_lbl), 0);
        gtk_widget_set_hexpand(name_lbl, TRUE);
        gtk_box_append(GTK_BOX(row), name_lbl);

        GtkWidget* chat_btn = gtk_button_new_with_label("Chat");
        gtk_widget_add_css_class(chat_btn, "suggested-action");
        g_object_set_data(G_OBJECT(chat_btn), "node_idx", GINT_TO_POINTER(i));
        g_signal_connect(chat_btn, "clicked", G_CALLBACK(on_chat_node_clicked), this);
        gtk_box_append(GTK_BOX(row), chat_btn);

        if (n.is_local) {
            GtkWidget* toggle = gtk_button_new_with_label(n.running ? "Stop" : "Start");
            gtk_widget_add_css_class(toggle, n.running ? "destructive-action" : "suggested-action");
            g_object_set_data(G_OBJECT(toggle), "node_idx", GINT_TO_POINTER(i));
            g_signal_connect(toggle, "clicked", G_CALLBACK(on_node_toggle), this);
            gtk_box_append(GTK_BOX(row), toggle);
        }

        GtkWidget* info_btn = gtk_button_new_with_label("Info");
        g_object_set_data(G_OBJECT(info_btn), "node_idx", GINT_TO_POINTER(i));
        g_signal_connect(info_btn, "clicked", G_CALLBACK(on_node_info), this);
        gtk_box_append(GTK_BOX(row), info_btn);

        GtkWidget* del_btn = gtk_button_new_with_label("Delete");
        gtk_widget_add_css_class(del_btn, "destructive-action");
        g_object_set_data(G_OBJECT(del_btn), "node_idx", GINT_TO_POINTER(i));
        g_signal_connect(del_btn, "clicked", G_CALLBACK(on_node_delete), this);
        gtk_box_append(GTK_BOX(row), del_btn);

        gtk_box_append(GTK_BOX(node_box_), row);

        if (i < (int)nodes_.size() - 1) {
            gtk_box_append(GTK_BOX(node_box_), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
        }
    }

    if (nodes_.empty()) {
        GtkWidget* empty = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(empty),
            "<span color='#888'>No nodes yet. Create one above or join via relay.</span>");
        gtk_widget_set_margin_top(empty, 20);
        gtk_box_append(GTK_BOX(node_box_), empty);
    }
}

void App::on_node_toggle(GtkWidget* w, gpointer d) {
    auto* self = static_cast<App*>(d);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "node_idx"));
    if (idx < 0 || idx >= (int)self->nodes_.size()) return;

    auto& n = self->nodes_[idx];
    n.running = !n.running;
    self->save_nodes();
    self->refresh_nodes();

    if (!n.running && self->current_node_id_ == n.id) {
        self->current_node_id_.clear();
        gtk_widget_set_sensitive(self->chat_input_, FALSE);
        self->messages_.clear();
        self->refresh_chat();
        self->switch_to_node_list();
    }
}

void App::on_chat_node_clicked(GtkWidget* w, gpointer d) {
    auto* self = static_cast<App*>(d);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "node_idx"));
    if (idx < 0 || idx >= (int)self->nodes_.size()) return;

    auto& node = self->nodes_[idx];
    self->current_node_id_ = node.id;

    char title[256];
    snprintf(title, sizeof(title), "<span size='large' weight='bold'>%s %s</span>",
        node.is_local ? "[Local]" : "[Remote]", node.name.c_str());
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

    if (!node.relay_url.empty()) {
        self->connect_to_relay(node.relay_url);
    }
}

void App::on_node_info(GtkWidget* w, gpointer d) {
    auto* self = static_cast<App*>(d);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "node_idx"));
    if (idx < 0 || idx >= (int)self->nodes_.size()) return;

    auto& n = self->nodes_[idx];

    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Node Info");
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
    add_info("Status:", n.running ? "Running" : "Stopped");
    add_info("Type:", n.is_local ? "Local (yours)" : "Remote");

    if (!n.relay_url.empty()) add_info("Relay:", n.relay_url.c_str());
    if (!n.creator_pubkey.empty()) add_info("Creator:", n.creator_pubkey.c_str());
    if (!n.id.empty()) add_info("Node ID:", n.id.c_str());

    char time_buf[64];
    time_t t = static_cast<time_t>(n.created_at);
    struct tm* tm_info = localtime(&t);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", tm_info);
    add_info("Created:", time_buf);

    char members_buf[32];
    snprintf(members_buf, sizeof(members_buf), "%d", n.member_count);
    add_info("Members:", members_buf);

    GtkWidget* close_btn = gtk_button_new_with_label("Close");
    g_signal_connect_swapped(close_btn, "clicked", G_CALLBACK(gtk_window_close), dialog);
    gtk_box_append(GTK_BOX(vbox), close_btn);

    gtk_window_present(GTK_WINDOW(dialog));
}

void App::on_node_delete(GtkWidget* w, gpointer d) {
    auto* self = static_cast<App*>(d);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "node_idx"));
    if (idx < 0 || idx >= (int)self->nodes_.size()) return;

    if (self->nodes_[idx].id == self->current_node_id_) {
        self->current_node_id_.clear();
        gtk_widget_set_sensitive(self->chat_input_, FALSE);
        self->messages_.clear();
        self->refresh_chat();
        self->switch_to_node_list();
    }

    self->nodes_.erase(self->nodes_.begin() + idx);
    self->save_nodes();
    self->refresh_nodes();
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

} // namespace lili
