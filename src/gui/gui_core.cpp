#include <WiFi.h>
#include "gui.h"
#include "gui_common.h"

// ===========================================================================
// Widget helpers
// ===========================================================================

lv_obj_t * gui_obj(lv_obj_t * parent, int x, int y, int w, int h) {
    lv_obj_t * o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    return o;
}

lv_obj_t * gui_card(lv_obj_t * parent, int x, int y, int w, int h, int radius) {
    lv_obj_t * o = gui_obj(parent, x, y, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(GUI_C_CARD), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(GUI_C_CARD_BORDER), 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

lv_obj_t * gui_label(lv_obj_t * parent, const char * text, const lv_font_t * font, uint32_t color) {
    lv_obj_t * l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

lv_obj_t * gui_icon(lv_obj_t * parent, const char * icon, const lv_font_t * font, uint32_t color) {
    return gui_label(parent, icon, font, color);
}

lv_obj_t * gui_button(lv_obj_t * parent, int x, int y, int w, int h, uint32_t bg, int radius) {
    lv_obj_t * b = gui_obj(parent, x, y, w, h);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, radius, 0);
    lv_obj_set_style_opa(b, LV_OPA_70, LV_STATE_PRESSED);   // press feedback
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return b;
}

lv_obj_t * gui_spinner(lv_obj_t * parent, int size, uint32_t color, uint32_t track, int width) {
    lv_obj_t * sp = lv_spinner_create(parent);
    lv_spinner_set_anim_params(sp, 1000, 240);
    lv_obj_set_size(sp, size, size);
    lv_obj_remove_flag(sp, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_arc_width(sp, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, lv_color_hex(track), LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sp, lv_color_hex(color), LV_PART_INDICATOR);
    return sp;
}

lv_obj_t * gui_signal_bars(lv_obj_t * parent, int level, uint32_t color) {
    lv_obj_t * c = gui_obj(parent, 0, 0, 30, 24);
    for (int i = 0; i < 4; i++) {
        int h = 6 + i * 5;
        lv_obj_t * bar = gui_obj(c, i * 8, 24 - h, 6, h);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(i < level ? color : 0x323B57), 0);
    }
    return c;
}

void gui_label_ellipsis(lv_obj_t * label, int width) {
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label, width);
}

// ===========================================================================
// Pages & navigation
// ===========================================================================

static bool (*s_back_hook)() = nullptr;

void gui_set_back_hook(bool (*hook)()) {
    s_back_hook = hook;
}

static void go_home_async(void *) {
    gui_go_home();
}

static void back_cb(lv_event_t *) {
    if (s_back_hook && s_back_hook()) {
        return;
    }
    lv_async_call(go_home_async, NULL); // never delete the screen from inside its own event
}

lv_obj_t * gui_back_button(lv_obj_t * parent, int x, int y, int size, uint32_t bg, int radius) {
    lv_obj_t * b = gui_button(parent, x, y, size, size, bg, radius);
    lv_obj_set_ext_click_area(b, 6);
    lv_obj_t * i = gui_icon(b, GUI_ICON_LEFT, GUI_F_ICON_S, GUI_C_TEXT);
    (void) i;
    lv_obj_add_event_cb(b, back_cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static lv_obj_t * screen_create(uint32_t bg) {
    lv_obj_t * s = lv_obj_create(NULL);
    lv_obj_remove_style_all(s);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    return s;
}

gui_page_t gui_page_create(const char * title, uint32_t accent) {
    s_back_hook = nullptr;
    gui_page_t pg;
    pg.accent = accent;
    pg.screen = screen_create(GUI_C_BG);

    lv_obj_t * bar = gui_obj(pg.screen, 0, GUI_STATUS_H, GUI_W, GUI_TITLE_H);
    lv_obj_set_style_bg_color(bar, lv_color_hex(GUI_C_PANEL), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 2, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(accent), 0);

    gui_back_button(bar, 10, 3, 52, GUI_C_KEY, 14);

    pg.title = gui_label(bar, title, GUI_F_TITLE);
    lv_obj_align(pg.title, LV_ALIGN_LEFT_MID, 74, -1);

    pg.header_right = gui_obj(bar, 380, 0, 406, GUI_TITLE_H - 2);
    lv_obj_set_flex_flow(pg.header_right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pg.header_right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pg.header_right, 10, 0);

    pg.content = gui_obj(pg.screen, 0, GUI_STATUS_H + GUI_TITLE_H, GUI_W, GUI_H - GUI_STATUS_H - GUI_TITLE_H);
    return pg;
}

gui_page_t gui_page_create_fullscreen(uint32_t bg) {
    s_back_hook = nullptr;
    gui_page_t pg;
    pg.accent = 0;
    pg.screen = screen_create(bg);
    pg.title = nullptr;
    pg.header_right = nullptr;
    pg.content = gui_obj(pg.screen, 0, 0, GUI_W, GUI_H);
    return pg;
}

void gui_page_show(gui_page_t & page) {
    static bool first = true; // keep LVGL's initial empty screen, delete every screen after it
    lv_screen_load_anim(page.screen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, !first);
    first = false;
}

// ===========================================================================
// Home screen (icon grid) and module table
// ===========================================================================

struct gui_module_t {
    const char * title;       // text on the home tile
    const char * page_title;  // title bar text (NULL = title)
    const char * sub;         // small caption on the home tile
    const char * icon;
    uint32_t     color;
    void (*build)(gui_page_t &);
    bool         fullscreen;
};

static const gui_module_t MODULES[] = {
    { "ทดสอบสัมผัส",     NULL,                 "Touch",         GUI_ICON_PEN,     0xFB923C, gui_touch_build, false },
    { "ทดสอบสีหน้าจอ",   NULL,                 "Display",       GUI_ICON_DESKTOP, 0xA78BFA, gui_color_build, true  },
    { "เล่นเพลง",         NULL,                 "Audio",         GUI_ICON_MUSIC,   0xF472B6, gui_music_build, false },
    { "MicroSD Card",    NULL,                 "SD Card",       GUI_ICON_SDCARD,  0xFACC15, gui_sd_build,    false },
    { "อุปกรณ์ I2C",     "อุปกรณ์ I2C (Grove)", "Grove",         GUI_ICON_CHIP,    0x34D399, gui_i2c_build,   false },
    { "USB OTG",         NULL,                 "USB Host",      GUI_ICON_USB,     0x60A5FA, gui_usb_build,   false },
    { "ทดสอบ IO",         NULL,                 "GPIO",          GUI_ICON_TOGGLE,  0x22D3EE, gui_io_build,    false },
    { "WiFi",            NULL,                 "Scan / Connect", GUI_ICON_WIFI,    0xFB7185, gui_wifi_build,  false },
};
static const int MODULE_COUNT = sizeof(MODULES) / sizeof(MODULES[0]);

static void open_async(void * arg) {
    gui_open_module((int) (intptr_t) arg);
}

static void tile_cb(lv_event_t * e) {
    lv_async_call(open_async, lv_event_get_user_data(e));
}

static gui_page_t home_build() {
    gui_page_t pg;
    pg.accent = 0;
    pg.title = nullptr;
    pg.header_right = nullptr;
    pg.screen = screen_create(GUI_C_BG);
    pg.content = pg.screen;

    lv_obj_t * t = gui_label(pg.screen, "ชุดทดสอบฮาร์ดแวร์", GUI_F_TITLE);
    lv_obj_set_pos(t, 22, GUI_STATUS_H + 4);
    lv_obj_t * s = gui_label(pg.screen, "ATD5-P4 · 800×480", GUI_F_SMALL, GUI_C_MUTED);
    lv_obj_align(s, LV_ALIGN_TOP_RIGHT, -22, GUI_STATUS_H + 14);

    const int x0 = 16, y0 = 90, w = 181, h = 180, gap = 14;
    for (int i = 0; i < MODULE_COUNT; i++) {
        const gui_module_t & m = MODULES[i];
        lv_obj_t * tile = gui_card(pg.screen, x0 + (i % 4) * (w + gap), y0 + (i / 4) * (h + gap), w, h, 22);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x232C4A), LV_STATE_PRESSED);
        lv_obj_add_event_cb(tile, tile_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);

        lv_obj_t * box = gui_obj(tile, 0, 0, 74, 74);
        lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_set_style_radius(box, 22, 0);
        lv_obj_set_style_bg_color(box, lv_color_hex(m.color), 0);
        lv_obj_set_style_bg_opa(box, 34, 0);
        lv_obj_t * ic = gui_icon(box, m.icon, GUI_F_ICON_M, m.color);
        lv_obj_center(ic);

        lv_obj_t * name = gui_label(tile, m.title, GUI_F_LARGE_B);
        gui_label_ellipsis(name, w - 8);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 102);

        lv_obj_t * sub = gui_label(tile, m.sub, GUI_F_SMALL, GUI_C_MUTED);
        gui_label_ellipsis(sub, w - 8);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 142);
    }
    return pg;
}

void gui_go_home() {
    s_back_hook = nullptr;
    gui_page_t pg = home_build();
    gui_statusbar_overlay(false);
    gui_page_show(pg);
}

void gui_open_module(int index) {
    if (index < 0 || index >= MODULE_COUNT) {
        return;
    }
    const gui_module_t & m = MODULES[index];
    gui_page_t pg = m.fullscreen ? gui_page_create_fullscreen(GUI_C_BG)
                                 : gui_page_create(m.page_title ? m.page_title : m.title, m.color);
    m.build(pg);
    gui_page_show(pg);
}

// ===========================================================================
// Entry point
// ===========================================================================

static volatile bool s_wifi_ready = false;

bool gui_wifi_ready() {
    return s_wifi_ready;
}

static void wifi_init_task(void *) {
    // reconnect to the last network (credentials are kept in flash by the WiFi library)
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin();
    s_wifi_ready = true;
    vTaskDelete(NULL);
}

void gui_init() {
    // Keep the GUI responsive: bring WiFi up on core 0 (LVGL runs on core 1)
    xTaskCreatePinnedToCore(wifi_init_task, "wifi_init", 8192, NULL, 1, NULL, 0);

    gui_statusbar_init();
    gui_go_home();
}
