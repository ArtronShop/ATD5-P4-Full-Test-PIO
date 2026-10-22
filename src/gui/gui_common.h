#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <ATD5-P4.h>
#include "gui_config.h"
#include "gui_icons.h"
#include "fonts/gui_fonts.h"

// ---- Palette (0xRRGGBB) ---------------------------------------------------
#define GUI_C_BG           0x0D111C
#define GUI_C_BAR          0x090C14
#define GUI_C_PANEL        0x131929
#define GUI_C_CARD         0x171E32
#define GUI_C_CARD_BORDER  0x232C47
#define GUI_C_KEY          0x1F2740
#define GUI_C_TEXT         0xE8ECF4
#define GUI_C_MUTED        0x7D88A3
#define GUI_C_LINE         0x1C2438
#define GUI_C_OK           0x4ADE80
#define GUI_C_BAD          0xF87171
#define GUI_C_DARK_TEXT    0x0B1020   // text on bright accent buttons

// ---- Fonts ----------------------------------------------------------------
#define GUI_F_SMALL   (&gui_font_th16)
#define GUI_F_NORMAL  (&gui_font_th20)
#define GUI_F_LARGE   (&gui_font_th24)
#define GUI_F_LARGE_B (&gui_font_th24b)
#define GUI_F_TITLE   (&gui_font_th28b)
#define GUI_F_HUGE    (&gui_font_th48b)
#define GUI_F_ICON_S  (&gui_font_icon24)
#define GUI_F_ICON_M  (&gui_font_icon40)
#define GUI_F_ICON_L  (&gui_font_icon72)

// ---- A page = screen + title bar + content area ---------------------------
struct gui_page_t {
    lv_obj_t * screen;        // the lv screen
    lv_obj_t * title;         // title label (NULL on fullscreen pages)
    lv_obj_t * header_right;  // right-aligned flex row inside the title bar (NULL on fullscreen pages)
    lv_obj_t * content;       // area below the title bar (800 x 384), or whole screen for fullscreen pages
    uint32_t   accent;
};

// ---- Widget helpers (gui_core.cpp) ----------------------------------------
lv_obj_t * gui_obj(lv_obj_t * parent, int x, int y, int w, int h);                 // transparent, not clickable/scrollable
lv_obj_t * gui_card(lv_obj_t * parent, int x, int y, int w, int h, int radius = 18);
lv_obj_t * gui_label(lv_obj_t * parent, const char * text, const lv_font_t * font, uint32_t color = GUI_C_TEXT);
lv_obj_t * gui_icon(lv_obj_t * parent, const char * icon, const lv_font_t * font, uint32_t color);
lv_obj_t * gui_button(lv_obj_t * parent, int x, int y, int w, int h, uint32_t bg, int radius = 18); // clickable, centered content
lv_obj_t * gui_spinner(lv_obj_t * parent, int size, uint32_t color, uint32_t track, int width = 5); // rotating loading arc
lv_obj_t * gui_signal_bars(lv_obj_t * parent, int level /*0..4*/, uint32_t color);
lv_obj_t * gui_back_button(lv_obj_t * parent, int x, int y, int size, uint32_t bg, int radius);
void       gui_label_ellipsis(lv_obj_t * label, int width);                         // fixed width, "..." when too long

// ---- Navigation -----------------------------------------------------------
gui_page_t gui_page_create(const char * title, uint32_t accent);
gui_page_t gui_page_create_fullscreen(uint32_t bg);
void       gui_page_show(gui_page_t & page);
void       gui_set_back_hook(bool (*hook)());   // return true when the back press was handled by the page
void       gui_go_home();
void       gui_open_module(int index);

// ---- WiFi bring-up (gui_core.cpp) -----------------------------------------
// WiFi.mode()/begin() talk to the ESP32-C6 and take a long time, so they run in a
// background task. Anything that uses WiFi must wait for this to become true
// (and show a spinner meanwhile).
bool gui_wifi_ready();

// ---- Status bar (gui_status.cpp) ------------------------------------------
void gui_statusbar_init();
void gui_statusbar_overlay(bool on);            // translucent bar (used by the full screen color test)

// ---- Module pages ---------------------------------------------------------
void gui_touch_build(gui_page_t & page);
void gui_color_build(gui_page_t & page);
void gui_music_build(gui_page_t & page);
void gui_sd_build(gui_page_t & page);
void gui_i2c_build(gui_page_t & page);
void gui_usb_build(gui_page_t & page);
void gui_io_build(gui_page_t & page);
void gui_wifi_build(gui_page_t & page);
