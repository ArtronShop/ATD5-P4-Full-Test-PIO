#include <Wire.h>
#include <vector>
#include "gui_common.h"

// Module 5: I2C test on the Grove port - scan addresses 0x08..0x77.
// (The touch controller uses Wire1, so the Grove port is on Wire.)

namespace {

struct State {
    lv_obj_t * list;
    lv_obj_t * count;
    lv_obj_t * btn;
    lv_obj_t * btn_icon;
    lv_obj_t * btn_spinner;
    lv_obj_t * btn_text;
    bool scanning = false;
    uint32_t accent;
};
State * S = nullptr;
bool g_wire_started = false;

const char * guess_name(uint8_t a) {
    switch (a) {
        case 0x3C: case 0x3D: return "จอ OLED (SSD1306)";
        case 0x27: case 0x3F: return "จอ LCD (PCF8574)";
        case 0x68:            return "RTC DS3231 / MPU6050";
        case 0x69:            return "MPU6050 / ICM";
        case 0x76: case 0x77: return "BME280 / BMP280";
        case 0x48: case 0x49: return "ADS1115 / PCF8591";
        case 0x23: case 0x5C: return "BH1750 เซนเซอร์แสง";
        case 0x40:            return "INA219 / HTU21 / PCA9685";
        case 0x44: case 0x45: return "SHT3x เซนเซอร์อุณหภูมิ";
        case 0x38:            return "AHT10 / AHT20";
        case 0x29:            return "VL53L0X / TCS34725";
        case 0x57:            return "EEPROM / MAX30102";
        case 0x5A:            return "MLX90614 / CCS811";
        case 0x62:            return "SCD4x เซนเซอร์ CO2";
        default:              return "ไม่ทราบชนิด";
    }
}

void show_results(const std::vector<uint8_t> & found) {
    lv_obj_clean(S->list);
    if (found.empty()) {
        lv_obj_t * l = gui_label(S->list, "ไม่พบอุปกรณ์ I2C\nตรวจสอบสายและแหล่งจ่ายไฟของโมดูล", GUI_F_NORMAL, GUI_C_MUTED);
        lv_obj_set_style_pad_all(l, 8, 0);
    }
    for (uint8_t a : found) {
        lv_obj_t * card = gui_card(S->list, 0, 0, 446, 76);
        char addr[8];
        snprintf(addr, sizeof(addr), "0x%02X", a);
        lv_obj_t * al = gui_label(card, addr, GUI_F_TITLE, S->accent);
        lv_obj_align(al, LV_ALIGN_LEFT_MID, 18, 0);
        lv_obj_t * nl = gui_label(card, guess_name(a), GUI_F_NORMAL);
        gui_label_ellipsis(nl, 270);
        lv_obj_align(nl, LV_ALIGN_LEFT_MID, 140, 0);
    }
    lv_label_set_text_fmt(S->count, found.empty() ? "ไม่พบ" : "พบ %d อุปกรณ์", (int) found.size());
}

void set_scanning(bool on) {
    S->scanning = on;
    if (on) {
        lv_obj_add_flag(S->btn_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(S->btn_spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(S->btn_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(S->btn_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(S->btn_text, on ? "กำลังสแกน..." : "สแกนหา Address");
    lv_obj_set_style_opa(S->btn, on ? LV_OPA_60 : LV_OPA_COVER, 0);
    if (on) {
        lv_label_set_text(S->count, "");
    }
}

void scan_task(void *) {
    if (!g_wire_started) {
        Wire.begin(GUI_GROVE_SDA, GUI_GROVE_SCL, GUI_GROVE_FREQ_HZ);
        Wire.setTimeOut(50);
        g_wire_started = true;
    }

    // NOTE: lv_safe_update() copies the std::function bit-by-bit into a FreeRTOS queue, so the
    // callback must not capture anything that owns memory. Pass the result through user_data instead.
    auto * found = new std::vector<uint8_t>();
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            found->push_back(a);
        }
    }

    lv_safe_update([](void * arg) {
        auto * result = (std::vector<uint8_t> *) arg;
        if (S) { // page may have been closed meanwhile
            show_results(*result);
            set_scanning(false);
        }
        delete result;
    }, found);
    vTaskDelete(NULL);
}

void screen_delete_cb(lv_event_t *) {
    delete S;
    S = nullptr;
}

} // namespace

void gui_i2c_build(gui_page_t & pg) {
    S = new State();
    S->accent = pg.accent;
    lv_obj_t * c = pg.content;
    lv_obj_add_event_cb(pg.screen, screen_delete_cb, LV_EVENT_DELETE, NULL);

    // left column: pins + scan button
    lv_obj_t * pins = gui_card(c, 20, 22, 290, 54, 14);
    lv_obj_t * pl = gui_label(pins, "", GUI_F_NORMAL, GUI_C_MUTED);
    lv_label_set_text_fmt(pl, "SDA GPIO %d  ·  SCL GPIO %d", GUI_GROVE_SDA, GUI_GROVE_SCL);
    lv_obj_center(pl);

    S->btn = gui_button(c, 20, 94, 290, 110, pg.accent, 20);
    lv_obj_set_style_pad_row(S->btn, 4, 0);
    S->btn_icon = gui_icon(S->btn, GUI_ICON_SEARCH, GUI_F_ICON_M, GUI_C_DARK_TEXT);
    S->btn_spinner = gui_spinner(S->btn, 40, GUI_C_DARK_TEXT, 0x2BB887, 5);
    lv_obj_add_flag(S->btn_spinner, LV_OBJ_FLAG_HIDDEN);
    S->btn_text = gui_label(S->btn, "สแกนหา Address", GUI_F_TITLE, GUI_C_DARK_TEXT);
    lv_obj_add_event_cb(S->btn, [](lv_event_t *) {
        if (S->scanning) {
            return;
        }
        set_scanning(true);
        xTaskCreate(scan_task, "i2c_scan", 4096, NULL, 1, NULL);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t * note = gui_label(c, "สแกนช่วง 0x08 – 0x77", GUI_F_SMALL, GUI_C_MUTED);
    lv_obj_set_width(note, 290);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note, 20, 216);

    // right column: results
    lv_obj_t * head = gui_label(c, "ผลการสแกน", GUI_F_LARGE_B);
    lv_obj_set_pos(head, 334, 18);
    S->count = gui_label(c, "", GUI_F_LARGE_B, pg.accent);
    lv_obj_align(S->count, LV_ALIGN_TOP_RIGHT, -20, 18);

    S->list = gui_obj(c, 334, 58, 446, 316);
    lv_obj_add_flag(S->list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(S->list, LV_DIR_VER);
    lv_obj_set_flex_flow(S->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(S->list, 10, 0);
    lv_obj_t * hint = gui_label(S->list, "กดปุ่ม \"สแกนหา Address\"\nเพื่อค้นหาอุปกรณ์ที่ต่อกับช่อง Grove", GUI_F_NORMAL, GUI_C_MUTED);
    lv_obj_set_style_pad_all(hint, 8, 0);
}
