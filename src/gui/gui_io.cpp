#include "gui_common.h"

// Module 7: GPIO test.
//   OUTPUT mode: tap a tile to toggle the pin HIGH / LOW.
//   INPUT  mode: pins are read (internal pull-up) and shown live.
// The pins are listed in gui_config.h (GUI_IO_PINS).

namespace {

const uint8_t PINS[GUI_IO_PIN_COUNT] = GUI_IO_PINS;

struct Tile {
    lv_obj_t * box;
    lv_obj_t * name;
    lv_obj_t * state;
    lv_obj_t * hint;
    lv_obj_t * led;
};

struct State {
    Tile tiles[GUI_IO_PIN_COUNT];
    bool level[GUI_IO_PIN_COUNT] = {};
    bool input_mode = false;
    lv_obj_t * seg[2];
    lv_timer_t * timer;
    uint32_t accent;
};
State * S = nullptr;

void update_tile(int i) {
    Tile & t = S->tiles[i];
    bool on = S->level[i];
    bool filled = !S->input_mode && on;
    uint32_t fg = filled ? GUI_C_DARK_TEXT : GUI_C_TEXT;

    lv_obj_set_style_bg_color(t.box, lv_color_hex(filled ? S->accent : GUI_C_CARD), 0);
    lv_obj_set_style_text_color(t.name, lv_color_hex(filled ? GUI_C_DARK_TEXT : 0xAAB3C8), 0);
    lv_label_set_text(t.state, on ? "HIGH" : "LOW");
    lv_obj_set_style_text_color(t.state, lv_color_hex(S->input_mode ? (on ? S->accent : GUI_C_MUTED) : fg), 0);

    if (S->input_mode) {
        lv_obj_add_flag(t.hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(t.led, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(t.led, lv_color_hex(on ? S->accent : 0x2A3350), 0);
        lv_obj_set_style_shadow_width(t.led, on ? 16 : 0, 0);
    } else {
        lv_obj_remove_flag(t.hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(t.led, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(t.hint, lv_color_hex(filled ? 0x2A4A55 : GUI_C_MUTED), 0);
    }
}

void apply_mode() {
    for (int i = 0; i < GUI_IO_PIN_COUNT; i++) {
        if (S->input_mode) {
            pinMode(PINS[i], INPUT_PULLUP);
            S->level[i] = digitalRead(PINS[i]);
        } else {
            S->level[i] = false;
            pinMode(PINS[i], OUTPUT);
            digitalWrite(PINS[i], LOW);
        }
        update_tile(i);
    }
    for (int m = 0; m < 2; m++) {
        bool active = (m == (S->input_mode ? 1 : 0));
        lv_obj_set_style_bg_opa(S->seg[m], active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(S->seg[m], 0), lv_color_hex(active ? GUI_C_DARK_TEXT : GUI_C_MUTED), 0);
    }
}

void poll_cb(lv_timer_t *) {
    if (!S->input_mode) {
        return;
    }
    for (int i = 0; i < GUI_IO_PIN_COUNT; i++) {
        bool v = digitalRead(PINS[i]);
        if (v != S->level[i]) {
            S->level[i] = v;
            update_tile(i);
        }
    }
}

void screen_delete_cb(lv_event_t *) {
    // back to high impedance, LED off
    for (int i = 0; i < GUI_IO_PIN_COUNT; i++) {
        pinMode(PINS[i], INPUT);
    }
    pinMode(GUI_LED_PIN, OUTPUT);
    digitalWrite(GUI_LED_PIN, HIGH);
    if (S) {
        lv_timer_delete(S->timer);
        delete S;
        S = nullptr;
    }
}

} // namespace

void gui_io_build(gui_page_t & pg) {
    S = new State();
    S->accent = pg.accent;
    lv_obj_t * c = pg.content;
    lv_obj_add_event_cb(pg.screen, screen_delete_cb, LV_EVENT_DELETE, NULL);

    // OUTPUT / INPUT selector
    lv_obj_t * segs = gui_obj(c, 20, 10, 760, 56);
    lv_obj_set_style_bg_color(segs, lv_color_hex(GUI_C_PANEL), 0);
    lv_obj_set_style_bg_opa(segs, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(segs, 16, 0);
    static const char * const NAMES[2] = { "OUTPUT (สั่งขา)", "INPUT (อ่านค่า)" };
    for (int m = 0; m < 2; m++) {
        lv_obj_t * b = gui_button(segs, 5 + m * 375, 5, 375, 46, pg.accent, 12);
        gui_label(b, NAMES[m], GUI_F_LARGE_B, GUI_C_DARK_TEXT);
        lv_obj_add_event_cb(b, [](lv_event_t * e) {
            bool in = ((int) (intptr_t) lv_event_get_user_data(e)) == 1;
            if (in != S->input_mode) {
                S->input_mode = in;
                apply_mode();
            }
        }, LV_EVENT_CLICKED, (void *) (intptr_t) m);
        S->seg[m] = b;
    }

    // 4 x 2 pin tiles
    const int tw = 181, th = 140, gap = 12, y0 = 78;
    for (int i = 0; i < GUI_IO_PIN_COUNT; i++) {
        Tile & t = S->tiles[i];
        t.box = gui_button(c, 20 + (i % 4) * (tw + gap), y0 + (i / 4) * (th + gap), tw, th, GUI_C_CARD, 18);
        lv_obj_set_style_border_width(t.box, 1, 0);
        lv_obj_set_style_border_color(t.box, lv_color_hex(GUI_C_CARD_BORDER), 0);
        lv_obj_set_style_pad_row(t.box, 4, 0);
        lv_obj_add_event_cb(t.box, [](lv_event_t * e) {
            int i = (int) (intptr_t) lv_event_get_user_data(e);
            if (S->input_mode) {
                return;
            }
            S->level[i] = !S->level[i];
            digitalWrite(PINS[i], S->level[i] ? HIGH : LOW);
            update_tile(i);
        }, LV_EVENT_CLICKED, (void *) (intptr_t) i);

        char n[16];
        snprintf(n, sizeof(n), "GPIO %d", PINS[i]);
        t.name = gui_label(t.box, n, GUI_F_NORMAL);
        t.led = gui_obj(t.box, 0, 0, 30, 30);
        lv_obj_set_style_radius(t.led, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(t.led, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(t.led, lv_color_hex(pg.accent), 0);
        t.state = gui_label(t.box, "LOW", GUI_F_TITLE);
        t.hint = gui_label(t.box, "แตะเพื่อสลับ", GUI_F_SMALL);
    }

    apply_mode();
    S->timer = lv_timer_create(poll_cb, 100, NULL);
}
