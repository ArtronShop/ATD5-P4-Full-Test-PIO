#include <WiFi.h>
#include <algorithm>
#include <vector>
#include "gui_common.h"

// Module 8: WiFi test - scan networks, connect with a password.
// The last network is remembered by the WiFi library and re-joined on boot (see gui_init()).

namespace {

constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;

struct Network {
    String ssid;
    int    rssi;
    bool   secure;
};

struct State {
    lv_obj_t * title;
    lv_obj_t * msg;            // header right: transient status text
    lv_obj_t * sw;             // WiFi on/off
    lv_obj_t * refresh_btn;

    lv_obj_t * list_view;
    lv_obj_t * cur_name;
    lv_obj_t * cur_info;
    lv_obj_t * cur_bars;
    lv_obj_t * cur_card;
    lv_obj_t * cur_icon;
    lv_obj_t * list;
    lv_obj_t * caption;

    lv_obj_t * pw_view;
    lv_obj_t * ta;
    lv_obj_t * kb;
    lv_obj_t * eye_icon;

    // modal "connecting" dialog
    lv_obj_t * dialog;         // full-screen overlay that blocks the page below
    lv_obj_t * dlg_spinner;
    lv_obj_t * dlg_icon;
    lv_obj_t * dlg_title;
    lv_obj_t * dlg_sub;
    lv_obj_t * dlg_btn_label;
    bool   dialog_open = false;

    // shown while WiFi is still being brought up in the background (see gui_wifi_ready())
    lv_obj_t * wait_view;
    bool   waiting = false;

    lv_timer_t * timer;
    std::vector<Network> nets;
    String target;             // network being joined
    bool   scanning = false;
    bool   connecting = false;
    uint32_t connect_start = 0;
    bool   in_pw_view = false;
    uint32_t accent;
};
State * S = nullptr;

int level_of(int rssi) {
    return rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
}

void set_msg(const char * text, uint32_t color) {
    lv_label_set_text(S->msg, text);
    lv_obj_set_style_text_color(S->msg, lv_color_hex(color), 0);
}

// ---- modal dialog ---------------------------------------------------------------
void dialog_connecting(const String & ssid) {
    String t = String("กำลังเชื่อมต่อ ") + ssid;
    lv_label_set_text(S->dlg_title, t.c_str());
    lv_label_set_text(S->dlg_sub, "กรุณารอสักครู่...");
    lv_obj_set_style_text_color(S->dlg_sub, lv_color_hex(GUI_C_MUTED), 0);
    lv_label_set_text(S->dlg_btn_label, "ยกเลิก");
    lv_obj_add_flag(S->dlg_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(S->dlg_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(S->dialog, LV_OBJ_FLAG_HIDDEN);
    S->dialog_open = true;
}

void dialog_failed() {
    lv_label_set_text(S->dlg_title, "เชื่อมต่อไม่สำเร็จ");
    lv_label_set_text(S->dlg_sub, "ตรวจสอบรหัสผ่านแล้วลองอีกครั้ง");
    lv_obj_set_style_text_color(S->dlg_sub, lv_color_hex(GUI_C_BAD), 0);
    lv_label_set_text(S->dlg_btn_label, "ตกลง");
    lv_obj_add_flag(S->dlg_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(S->dlg_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(S->dialog, LV_OBJ_FLAG_HIDDEN);
    S->dialog_open = true;
}

void dialog_hide() {
    lv_obj_add_flag(S->dialog, LV_OBJ_FLAG_HIDDEN);
    S->dialog_open = false;
}

void dialog_button_cb(lv_event_t *) { // "Cancel" while connecting, "OK" after a failure
    if (S->connecting) {
        S->connecting = false;
        WiFi.disconnect();
    }
    dialog_hide();
}

// ---- views -------------------------------------------------------------------
void show_list_view() {
    S->in_pw_view = false;
    lv_obj_add_flag(S->pw_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(S->list_view, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(S->title, "WiFi");
    lv_obj_remove_flag(S->sw, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(S->refresh_btn, LV_OBJ_FLAG_HIDDEN);
}

void show_pw_view(const String & ssid) {
    S->in_pw_view = true;
    S->target = ssid;
    lv_obj_add_flag(S->list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(S->pw_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(S->sw, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(S->refresh_btn, LV_OBJ_FLAG_HIDDEN);
    String t = String("เชื่อมต่อ ") + ssid;
    lv_label_set_text(S->title, t.c_str());
    lv_textarea_set_text(S->ta, "");
    set_msg("", GUI_C_MUTED);
}

bool back_hook() {
    if (S && S->dialog_open) {
        dialog_button_cb(NULL);
        return true;
    }
    if (S && S->in_pw_view) {
        S->connecting = false;
        show_list_view();
        return true;
    }
    return false;
}

// ---- connect -----------------------------------------------------------------
void start_connect(const String & ssid, const char * pass) {
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), pass && pass[0] ? pass : nullptr);
    S->target = ssid;
    S->connecting = true;
    S->connect_start = millis();
    dialog_connecting(ssid);
}

// ---- scan --------------------------------------------------------------------
void start_scan() {
    if (S->scanning || !gui_wifi_ready() || WiFi.getMode() == WIFI_OFF) {
        return;
    }
    WiFi.scanDelete();
    WiFi.scanNetworks(true /*async*/, false /*hidden*/);
    S->scanning = true;
    lv_label_set_text(S->caption, "กำลังสแกน...");

    lv_obj_clean(S->list);
    lv_obj_t * box = gui_obj(S->list, 0, 0, GUI_W, 230);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 14, 0);
    gui_spinner(box, 56, S->accent, 0x323B57, 6);
    gui_label(box, "กำลังสแกนหาเครือข่าย WiFi...", GUI_F_NORMAL, GUI_C_MUTED);
}

void net_row_cb(lv_event_t * e) {
    if (S->connecting) {
        return;
    }
    int i = (int) (intptr_t) lv_event_get_user_data(e);
    if (i < 0 || i >= (int) S->nets.size()) {
        return;
    }
    const Network & n = S->nets[i];
    if (n.secure) {
        show_pw_view(n.ssid);
    } else {
        start_connect(n.ssid, nullptr);
    }
}

void fill_list() {
    lv_obj_clean(S->list);
    for (size_t i = 0; i < S->nets.size(); i++) {
        const Network & n = S->nets[i];
        lv_obj_t * row = gui_obj(S->list, 0, 0, GUI_W, 56);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(GUI_C_LINE), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1A2238), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, net_row_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);

        lv_obj_t * name = gui_label(row, n.ssid.c_str(), GUI_F_LARGE);
        gui_label_ellipsis(name, 560);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 20, 0);

        if (n.secure) {
            lv_obj_t * lk = gui_icon(row, GUI_ICON_LOCK, GUI_F_ICON_S, GUI_C_MUTED);
            lv_obj_align(lk, LV_ALIGN_RIGHT_MID, -76, 0);
        }
        lv_obj_t * bars = gui_signal_bars(row, level_of(n.rssi), S->accent);
        lv_obj_align(bars, LV_ALIGN_RIGHT_MID, -22, 0);
    }
    if (S->nets.empty()) {
        lv_obj_t * l = gui_label(S->list, "ไม่พบเครือข่าย WiFi", GUI_F_NORMAL, GUI_C_MUTED);
        lv_obj_set_style_pad_all(l, 20, 0);
    }
}

void collect_results(int n) {
    S->nets.clear();
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) {
            continue;
        }
        bool dup = false;
        for (auto & e : S->nets) {
            if (e.ssid == ssid) { // same SSID on several APs: keep the strongest
                dup = true;
                e.rssi = std::max(e.rssi, (int) WiFi.RSSI(i));
            }
        }
        if (!dup) {
            S->nets.push_back({ ssid, (int) WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN });
        }
    }
    std::sort(S->nets.begin(), S->nets.end(), [](const Network & a, const Network & b) { return a.rssi > b.rssi; });
    WiFi.scanDelete();
}

// ---- periodic update ---------------------------------------------------------
void finish_waiting() {
    S->waiting = false;
    lv_obj_add_flag(S->wait_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_state(S->sw, LV_STATE_DISABLED);
    lv_obj_remove_state(S->refresh_btn, LV_STATE_DISABLED);
    if (WiFi.getMode() != WIFI_OFF) {
        lv_obj_add_state(S->sw, LV_STATE_CHECKED);
        start_scan();
    } else {
        lv_label_set_text(S->caption, "WiFi ปิดอยู่");
    }
}

void tick_cb(lv_timer_t *) {
    if (S->waiting) {
        if (!gui_wifi_ready()) {
            return;
        }
        finish_waiting();
    }
    bool connected = WiFi.status() == WL_CONNECTED;

    // current connection card
    if (connected) {
        lv_label_set_text(S->cur_name, WiFi.SSID().c_str());
        String info = String("เชื่อมต่อแล้ว · ") + WiFi.localIP().toString();
        lv_label_set_text(S->cur_info, info.c_str());
        lv_obj_set_style_text_color(S->cur_info, lv_color_hex(GUI_C_OK), 0);
        lv_obj_set_style_text_color(S->cur_icon, lv_color_hex(GUI_C_OK), 0);
        lv_obj_remove_flag(S->cur_bars, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(S->cur_name, WiFi.getMode() == WIFI_OFF ? "WiFi ปิดอยู่" : "ยังไม่ได้เชื่อมต่อ");
        lv_label_set_text(S->cur_info, "เลือกเครือข่ายจากรายการด้านล่าง");
        lv_obj_set_style_text_color(S->cur_info, lv_color_hex(GUI_C_MUTED), 0);
        lv_obj_set_style_text_color(S->cur_icon, lv_color_hex(0x566180), 0);
        lv_obj_add_flag(S->cur_bars, LV_OBJ_FLAG_HIDDEN);
    }

    // scan result
    if (S->scanning) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
            S->scanning = false;
            collect_results(n);
            fill_list();
            lv_label_set_text(S->caption, "เครือข่ายที่พบ");
        } else if (n == WIFI_SCAN_FAILED) {
            S->scanning = false;
            lv_label_set_text(S->caption, "สแกนไม่สำเร็จ - กดปุ่มรีเฟรช");
            lv_obj_clean(S->list);
        }
    }

    // connecting
    if (S->connecting) {
        if (connected && WiFi.SSID() == S->target) {
            S->connecting = false;
            dialog_hide();
            if (S->in_pw_view) {
                show_list_view();
            }
        } else if (millis() - S->connect_start > CONNECT_TIMEOUT_MS) {
            S->connecting = false;
            WiFi.disconnect();
            dialog_failed();
        }
    }
}

void screen_delete_cb(lv_event_t *) {
    if (S) {
        lv_timer_delete(S->timer);
        delete S;
        S = nullptr;
    }
}

void style_keyboard(lv_obj_t * kb) {
    lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 0, 0);
    lv_obj_set_style_pad_row(kb, 6, 0);
    lv_obj_set_style_pad_column(kb, 6, 0);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_24, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_hex(GUI_C_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x222B45), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 12, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x2D3858), LV_PART_ITEMS | LV_STATE_CHECKED);
}

} // namespace

void gui_wifi_build(gui_page_t & pg) {
    S = new State();
    S->accent = pg.accent;
    S->title = pg.title;
    lv_obj_t * c = pg.content;
    lv_obj_add_event_cb(pg.screen, screen_delete_cb, LV_EVENT_DELETE, NULL);
    gui_set_back_hook(back_hook);

    // ---- header: message, refresh, on/off switch
    S->msg = gui_label(pg.header_right, "", GUI_F_NORMAL, GUI_C_MUTED);
    S->refresh_btn = gui_button(pg.header_right, 0, 0, 58, 46, GUI_C_KEY, 12);
    lv_obj_set_ext_click_area(S->refresh_btn, 4);
    gui_icon(S->refresh_btn, GUI_ICON_REFRESH, GUI_F_ICON_S, GUI_C_TEXT);
    lv_obj_add_event_cb(S->refresh_btn, [](lv_event_t *) { start_scan(); }, LV_EVENT_CLICKED, NULL);

    S->sw = lv_switch_create(pg.header_right);
    lv_obj_set_size(S->sw, 62, 34);
    lv_obj_set_style_bg_color(S->sw, lv_color_hex(0x323B57), LV_PART_MAIN);
    lv_obj_set_style_bg_color(S->sw, lv_color_hex(GUI_C_OK), (lv_style_selector_t) LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(S->sw, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_ext_click_area(S->sw, 6);
    lv_obj_add_event_cb(S->sw, [](lv_event_t * e) {
        bool on = lv_obj_has_state((lv_obj_t *) lv_event_get_target(e), LV_STATE_CHECKED);
        if (on) {
            WiFi.mode(WIFI_STA);
            WiFi.begin();
            start_scan();
        } else {
            WiFi.disconnect(true, false);
            WiFi.mode(WIFI_OFF);
            S->nets.clear();
            S->scanning = false;
            fill_list();
            lv_label_set_text(S->caption, "WiFi ปิดอยู่");
        }
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // ---- list view
    S->list_view = gui_obj(c, 0, 0, GUI_W, 384);

    S->cur_card = gui_obj(S->list_view, 16, 12, 768, 68);
    lv_obj_set_style_radius(S->cur_card, 16, 0);
    lv_obj_set_style_bg_color(S->cur_card, lv_color_hex(pg.accent), 0);
    lv_obj_set_style_bg_opa(S->cur_card, 34, 0);
    lv_obj_set_style_border_width(S->cur_card, 1, 0);
    lv_obj_set_style_border_color(S->cur_card, lv_color_hex(pg.accent), 0);
    lv_obj_set_style_border_opa(S->cur_card, LV_OPA_40, 0);
    S->cur_icon = gui_icon(S->cur_card, GUI_ICON_WIFI, GUI_F_ICON_M, 0x566180);
    lv_obj_align(S->cur_icon, LV_ALIGN_LEFT_MID, 18, 0);
    S->cur_name = gui_label(S->cur_card, "", GUI_F_LARGE_B);
    gui_label_ellipsis(S->cur_name, 560);
    lv_obj_align(S->cur_name, LV_ALIGN_TOP_LEFT, 82, 6);
    S->cur_info = gui_label(S->cur_card, "", GUI_F_SMALL, GUI_C_MUTED);
    gui_label_ellipsis(S->cur_info, 560);
    lv_obj_align(S->cur_info, LV_ALIGN_TOP_LEFT, 82, 38);
    S->cur_bars = gui_signal_bars(S->cur_card, 4, GUI_C_OK);
    lv_obj_align(S->cur_bars, LV_ALIGN_RIGHT_MID, -20, 0);

    S->caption = gui_label(S->list_view, "", GUI_F_SMALL, GUI_C_MUTED);
    lv_obj_set_pos(S->caption, 20, 86);

    S->list = gui_obj(S->list_view, 0, 112, GUI_W, 272);
    lv_obj_add_flag(S->list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(S->list, LV_DIR_VER);
    lv_obj_set_flex_flow(S->list, LV_FLEX_FLOW_COLUMN);

    // ---- password view (hidden until a secured network is tapped)
    S->pw_view = gui_obj(c, 0, 0, GUI_W, 384);

    S->ta = lv_textarea_create(S->pw_view);
    lv_textarea_set_one_line(S->ta, true);
    lv_textarea_set_password_mode(S->ta, true);
    lv_textarea_set_max_length(S->ta, 63);
    lv_textarea_set_placeholder_text(S->ta, "รหัสผ่าน WiFi");
    lv_obj_set_size(S->ta, 776, 58);
    lv_obj_set_pos(S->ta, 12, 8);
    lv_obj_set_style_text_font(S->ta, &gui_font_th24, 0);
    lv_obj_set_style_text_color(S->ta, lv_color_hex(GUI_C_TEXT), 0);
    lv_obj_set_style_bg_color(S->ta, lv_color_hex(0x080B12), 0);
    lv_obj_set_style_bg_opa(S->ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(S->ta, 2, 0);
    lv_obj_set_style_border_color(S->ta, lv_color_hex(pg.accent), 0);
    lv_obj_set_style_radius(S->ta, 14, 0);
    lv_obj_set_style_pad_left(S->ta, 16, 0);
    lv_obj_set_style_pad_right(S->ta, 70, 0);
    lv_obj_set_style_pad_ver(S->ta, 12, 0);
    lv_obj_set_style_text_color(S->ta, lv_color_hex(0x566180), LV_PART_TEXTAREA_PLACEHOLDER);

    lv_obj_t * eye = gui_button(S->pw_view, 722, 12, 58, 50, 0x080B12, 12);
    lv_obj_set_ext_click_area(eye, 4);
    S->eye_icon = gui_icon(eye, GUI_ICON_EYE, GUI_F_ICON_S, 0xAAB3C8);
    lv_obj_add_event_cb(eye, [](lv_event_t *) {
        bool hidden = lv_textarea_get_password_mode(S->ta);
        lv_textarea_set_password_mode(S->ta, !hidden);
        lv_label_set_text(S->eye_icon, hidden ? GUI_ICON_EYE_OFF : GUI_ICON_EYE);
    }, LV_EVENT_CLICKED, NULL);

    S->kb = lv_keyboard_create(S->pw_view);
    lv_obj_set_size(S->kb, 784, 298);
    lv_obj_align(S->kb, LV_ALIGN_BOTTOM_MID, 0, -8); // lv_keyboard_create() aligns itself to the bottom; set_pos would just offset that
    lv_keyboard_set_textarea(S->kb, S->ta);
    style_keyboard(S->kb);
    auto submit = [](lv_event_t *) {
        if (!S->connecting) {
            start_connect(S->target, lv_textarea_get_text(S->ta));
        }
    };
    lv_obj_add_event_cb(S->kb, submit, LV_EVENT_READY, NULL); // the "OK" key
    lv_obj_add_event_cb(S->ta, submit, LV_EVENT_READY, NULL); // the "Enter" key (one-line textarea)
    lv_obj_add_flag(S->pw_view, LV_OBJ_FLAG_HIDDEN);

    // ---- modal dialog (on the screen, above everything incl. the title bar controls)
    S->dialog = gui_obj(pg.screen, 0, GUI_STATUS_H, GUI_W, GUI_H - GUI_STATUS_H);
    lv_obj_add_flag(S->dialog, LV_OBJ_FLAG_CLICKABLE); // swallow every touch
    lv_obj_set_style_bg_color(S->dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(S->dialog, LV_OPA_70, 0);
    lv_obj_t * dc = gui_card(S->dialog, 0, 0, 480, 270, 24);
    lv_obj_set_style_bg_color(dc, lv_color_hex(0x1B2340), 0);
    lv_obj_align(dc, LV_ALIGN_CENTER, 0, -10);
    S->dlg_spinner = gui_spinner(dc, 64, pg.accent, 0x323B57, 6);
    lv_obj_align(S->dlg_spinner, LV_ALIGN_TOP_MID, 0, 28);
    S->dlg_icon = gui_icon(dc, GUI_ICON_WARNING, GUI_F_ICON_M, GUI_C_BAD);
    lv_obj_align(S->dlg_icon, LV_ALIGN_TOP_MID, 0, 30);
    S->dlg_title = gui_label(dc, "", GUI_F_LARGE_B);
    gui_label_ellipsis(S->dlg_title, 440);
    lv_obj_set_style_text_align(S->dlg_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(S->dlg_title, LV_ALIGN_TOP_MID, 0, 110);
    S->dlg_sub = gui_label(dc, "", GUI_F_NORMAL, GUI_C_MUTED);
    lv_obj_set_style_text_align(S->dlg_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(S->dlg_sub, LV_ALIGN_TOP_MID, 0, 148);
    lv_obj_t * dbtn = gui_button(dc, 0, 0, 200, 52, GUI_C_KEY, 14);
    lv_obj_align(dbtn, LV_ALIGN_BOTTOM_MID, 0, -22);
    S->dlg_btn_label = gui_label(dbtn, "", GUI_F_LARGE_B);
    lv_obj_add_event_cb(dbtn, dialog_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(S->dialog, LV_OBJ_FLAG_HIDDEN);

    // first scan (WiFi may still be joining the saved network; scanning is fine meanwhile)
    S->timer = lv_timer_create(tick_cb, 500, NULL);

    // WiFi may still be starting (ESP32-C6 link): show a spinner and wait
    S->wait_view = gui_obj(c, 0, 0, GUI_W, 384);
    lv_obj_add_flag(S->wait_view, LV_OBJ_FLAG_CLICKABLE); // swallow touches
    lv_obj_set_style_bg_color(S->wait_view, lv_color_hex(GUI_C_BG), 0);
    lv_obj_set_style_bg_opa(S->wait_view, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(S->wait_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(S->wait_view, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(S->wait_view, 16, 0);
    gui_spinner(S->wait_view, 64, pg.accent, 0x323B57, 6);
    gui_label(S->wait_view, "กำลังเริ่มต้น WiFi...", GUI_F_NORMAL, GUI_C_MUTED);
    lv_obj_add_state(S->sw, LV_STATE_DISABLED);
    lv_obj_add_state(S->refresh_btn, LV_STATE_DISABLED);
    S->waiting = true;

    tick_cb(NULL); // finishes immediately when WiFi is already up
}
