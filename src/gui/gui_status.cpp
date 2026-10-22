#include <WiFi.h>
#include <time.h>
#include "gui_common.h"

// Phone-like status bar (clock + WiFi) living on the top layer, so it is
// shown on every page.

static lv_obj_t * s_bar;
static lv_obj_t * s_time;
static lv_obj_t * s_wifi_icon;
static lv_obj_t * s_ssid;
static lv_obj_t * s_spin;
static bool s_ntp_started = false;

static void update_cb(lv_timer_t *) {
    // clock
    struct tm t;
    if (getLocalTime(&t, 0)) {
        lv_label_set_text_fmt(s_time, "%02d:%02d", t.tm_hour, t.tm_min);
    } else {
        lv_label_set_text(s_time, "--:--");
    }

    // WiFi (not touched until the background bring-up has finished)
    if (!gui_wifi_ready()) {
        lv_obj_remove_flag(s_spin, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_wifi_icon, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_ssid, "กำลังเริ่ม WiFi...");
        return;
    }
    lv_obj_add_flag(s_spin, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_wifi_icon, LV_OBJ_FLAG_HIDDEN);

    if (WiFi.status() == WL_CONNECTED) {
        if (!s_ntp_started) {
            configTime(GUI_TZ_OFFSET_SEC, 0, GUI_NTP_SERVER_1, GUI_NTP_SERVER_2);
            s_ntp_started = true;
        }
        lv_obj_set_style_text_color(s_wifi_icon, lv_color_hex(GUI_C_OK), 0);
        String ssid = WiFi.SSID();
        lv_label_set_text(s_ssid, ssid.c_str());
    } else {
        s_ntp_started = false;
        lv_obj_set_style_text_color(s_wifi_icon, lv_color_hex(0x566180), 0);
        lv_label_set_text(s_ssid, "ไม่ได้เชื่อมต่อ");
    }
}

void gui_statusbar_init() {
    s_bar = gui_obj(lv_layer_top(), 0, 0, GUI_W, GUI_STATUS_H);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(GUI_C_BAR), 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);

    s_time = gui_label(s_bar, "--:--", GUI_F_NORMAL, 0xCFD6E6);
    lv_obj_align(s_time, LV_ALIGN_LEFT_MID, 18, 0);

    lv_obj_t * right = gui_obj(s_bar, 400, 0, 382, GUI_STATUS_H);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 8, 0);
    s_spin = gui_spinner(right, 18, 0x60A5FA, 0x323B57, 3);
    s_wifi_icon = gui_icon(right, GUI_ICON_WIFI, GUI_F_ICON_S, 0x566180);
    s_ssid = gui_label(right, "", GUI_F_SMALL, 0xCFD6E6);

    lv_timer_create(update_cb, 1000, NULL);
    update_cb(NULL);
}

void gui_statusbar_overlay(bool on) {
    if (!s_bar) {
        return;
    }
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(on ? 0x000000 : GUI_C_BAR), 0);
    lv_obj_set_style_bg_opa(s_bar, on ? LV_OPA_30 : LV_OPA_COVER, 0);
}
