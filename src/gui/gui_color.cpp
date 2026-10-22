#include "gui_common.h"

// Module 2: display color test - full screen colors, tap to change.

namespace {

struct ColorDef {
    uint32_t    rgb;
    const char * th;
    const char * en;
};

const ColorDef COLORS[] = {
    { 0xFF0000, "แดง",      "RED" },
    { 0x00FF00, "เขียว",    "GREEN" },
    { 0x0000FF, "น้ำเงิน",  "BLUE" },
    { 0xFFFFFF, "ขาว",      "WHITE" },
    { 0x000000, "ดำ",       "BLACK" },
    { 0xFFFF00, "เหลือง",   "YELLOW" },
    { 0x00FFFF, "ฟ้า",      "CYAN" },
    { 0xFF00FF, "ม่วงแดง",  "MAGENTA" },
};
constexpr int COLOR_COUNT = sizeof(COLORS) / sizeof(COLORS[0]);

struct State {
    lv_obj_t * screen;
    lv_obj_t * name;
    lv_obj_t * code;
    lv_obj_t * hint;
    lv_obj_t * dots[COLOR_COUNT];
    int index = 0;
};

State * S = nullptr;

bool is_light(uint32_t rgb) {
    int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return (r * 299 + g * 587 + b * 114) / 1000 > 140;
}

void apply() {
    const ColorDef & c = COLORS[S->index];
    uint32_t fg = is_light(c.rgb) ? 0x000000 : 0xFFFFFF;

    lv_obj_set_style_bg_color(S->screen, lv_color_hex(c.rgb), 0);
    lv_label_set_text(S->name, c.th);
    lv_obj_set_style_text_color(S->name, lv_color_hex(fg), 0);
    lv_label_set_text_fmt(S->code, "%s · #%06X", c.en, (unsigned) c.rgb);
    lv_obj_set_style_text_color(S->code, lv_color_hex(fg), 0);

    for (int i = 0; i < COLOR_COUNT; i++) {
        bool cur = (i == S->index);
        int d = cur ? 30 : 20;
        lv_obj_set_size(S->dots[i], d, d);
        lv_obj_set_style_border_color(S->dots[i], lv_color_hex(fg), 0);
        lv_obj_set_style_border_opa(S->dots[i], cur ? LV_OPA_COVER : LV_OPA_50, 0);
    }
}

void screen_delete_cb(lv_event_t *) {
    gui_statusbar_overlay(false);
    delete S;
    S = nullptr;
}

} // namespace

void gui_color_build(gui_page_t & pg) {
    S = new State();
    S->screen = pg.screen;
    lv_obj_t * c = pg.content;
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, [](lv_event_t *) {
        S->index = (S->index + 1) % COLOR_COUNT;
        apply();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(pg.screen, screen_delete_cb, LV_EVENT_DELETE, NULL);

    gui_statusbar_overlay(true);

    // translucent back button
    lv_obj_t * back = gui_back_button(c, 12, 48, 52, 0x000000, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(back, LV_OPA_40, 0);

    S->name = gui_label(c, "", GUI_F_HUGE);
    lv_obj_align(S->name, LV_ALIGN_CENTER, 0, -20);
    S->code = gui_label(c, "", GUI_F_LARGE);
    lv_obj_align(S->code, LV_ALIGN_CENTER, 0, 36);

    // hint + position dots
    S->hint = gui_label(c, "แตะหน้าจอเพื่อเปลี่ยนสี", GUI_F_NORMAL, 0xFFFFFF);
    lv_obj_set_style_bg_color(S->hint, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(S->hint, LV_OPA_40, 0);
    lv_obj_set_style_radius(S->hint, 20, 0);
    lv_obj_set_style_pad_hor(S->hint, 22, 0);
    lv_obj_set_style_pad_ver(S->hint, 5, 0);
    lv_obj_align(S->hint, LV_ALIGN_BOTTOM_MID, 0, -54);

    lv_obj_t * row = gui_obj(c, 0, 0, 8 * 34, 34);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    for (int i = 0; i < COLOR_COUNT; i++) {
        lv_obj_t * d = gui_obj(row, 0, 0, 20, 20);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(COLORS[i].rgb), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(d, 2, 0);
        S->dots[i] = d;
    }

    apply();
}
