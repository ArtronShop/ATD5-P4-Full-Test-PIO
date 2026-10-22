#include "gui_common.h"
#include <esp_heap_caps.h>

// Module 1: touch test - draw with the finger, pick a color, eraser, clear.

namespace {

constexpr int CANVAS_W = 672;
constexpr int CANVAS_H = 372;
constexpr uint32_t CANVAS_BG = 0x080B12;
constexpr int BRUSH_W = 7;
constexpr int ERASER_W = 28;

const uint32_t PALETTE[6] = { 0xFFFFFF, 0xFF4D4D, 0xFFB020, 0x37D67A, 0x3BA0FF, 0xC084FC };

struct State {
    lv_obj_t * canvas;
    lv_obj_t * hint;
    lv_obj_t * coord;
    lv_obj_t * dots[6];
    lv_obj_t * eraser_btn;
    void *     buf;
    int        color_idx = 4;
    bool       erase = false;
    bool       down = false;
    int        last_x = 0, last_y = 0;
};

State * S = nullptr;

void update_toolbar() {
    for (int i = 0; i < 6; i++) {
        bool sel = (!S->erase && i == S->color_idx);
        lv_obj_set_style_outline_width(S->dots[i], sel ? 3 : 0, 0);
    }
    lv_obj_set_style_border_width(S->eraser_btn, S->erase ? 3 : 0, 0);
}

void draw_segment(int x0, int y0, int x1, int y1) {
    lv_layer_t layer;
    lv_canvas_init_layer(S->canvas, &layer);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(S->erase ? CANVAS_BG : PALETTE[S->color_idx]);
    dsc.width = S->erase ? ERASER_W : BRUSH_W;
    dsc.round_start = 1;
    dsc.round_end = 1;
    dsc.p1.x = x0;
    dsc.p1.y = y0;
    dsc.p2.x = x1;
    dsc.p2.y = y1;
    lv_draw_line(&layer, &dsc);

    lv_canvas_finish_layer(S->canvas, &layer);
}

void canvas_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        S->down = false;
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) {
        return;
    }

    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_label_set_text_fmt(S->coord, "X %d · Y %d", (int) p.x, (int) p.y);

    lv_area_t a;
    lv_obj_get_coords(S->canvas, &a);
    int x = p.x - a.x1;
    int y = p.y - a.y1;

    if (!S->down) {
        S->down = true;
        S->last_x = x;
        S->last_y = y;
        lv_obj_add_flag(S->hint, LV_OBJ_FLAG_HIDDEN);
        draw_segment(x, y, x + 1, y); // a dot for a simple tap
        return;
    }
    if (x == S->last_x && y == S->last_y) {
        return;
    }
    draw_segment(S->last_x, S->last_y, x, y);
    S->last_x = x;
    S->last_y = y;
}

void canvas_delete_cb(lv_event_t *) {
    if (S) {
        heap_caps_free(S->buf);
        delete S;
        S = nullptr;
    }
}

void clear_canvas() {
    lv_canvas_fill_bg(S->canvas, lv_color_hex(CANVAS_BG), LV_OPA_COVER);
    lv_obj_remove_flag(S->hint, LV_OBJ_FLAG_HIDDEN);
}

} // namespace

void gui_touch_build(gui_page_t & pg) {
    S = new State();
    lv_obj_t * c = pg.content;

    // readout in the title bar
    S->coord = gui_label(pg.header_right, "X - · Y -", GUI_F_NORMAL, GUI_C_MUTED);

    // drawing area (frame + canvas)
    lv_obj_t * frame = gui_obj(c, 8, 4, CANVAS_W + 4, CANVAS_H + 4);
    lv_obj_set_style_radius(frame, 16, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(GUI_C_CARD_BORDER), 0);
    lv_obj_set_style_clip_corner(frame, true, 0);

    size_t size = (size_t) CANVAS_W * CANVAS_H * 2; // RGB565, stride = 1344 (multiple of 64)
    S->buf = heap_caps_aligned_alloc(64, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!S->buf) {
        S->buf = heap_caps_aligned_alloc(64, size, MALLOC_CAP_8BIT);
    }
    S->canvas = lv_canvas_create(frame);
    lv_canvas_set_buffer(S->canvas, S->buf, CANVAS_W, CANVAS_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(S->canvas, 0, 0);
    lv_obj_add_flag(S->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(S->canvas, canvas_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(S->canvas, canvas_delete_cb, LV_EVENT_DELETE, NULL);

    S->hint = gui_label(frame, "วาดบนพื้นที่นี้ได้เลย", GUI_F_SMALL, 0x566180);
    lv_obj_set_pos(S->hint, 14, 10);
    lv_obj_remove_flag(S->hint, LV_OBJ_FLAG_CLICKABLE);

    // toolbar: 6 colors, eraser, clear
    for (int i = 0; i < 6; i++) {
        lv_obj_t * d = gui_obj(c, 688 + (i % 2) * 54, 12 + (i / 2) * 60, 50, 50);
        lv_obj_add_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(d, 4);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(PALETTE[i]), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_outline_color(d, lv_color_hex(PALETTE[i]), 0);
        lv_obj_set_style_outline_pad(d, 3, 0);
        lv_obj_add_event_cb(d, [](lv_event_t * e) {
            S->color_idx = (int) (intptr_t) lv_event_get_user_data(e);
            S->erase = false;
            update_toolbar();
        }, LV_EVENT_CLICKED, (void *) (intptr_t) i);
        S->dots[i] = d;
    }

    S->eraser_btn = gui_button(c, 688, 196, 100, 70, GUI_C_KEY, 16);
    lv_obj_set_style_border_color(S->eraser_btn, lv_color_hex(pg.accent), 0);
    gui_icon(S->eraser_btn, GUI_ICON_ERASER, GUI_F_ICON_S, GUI_C_TEXT);
    gui_label(S->eraser_btn, "ยางลบ", GUI_F_SMALL);
    lv_obj_add_event_cb(S->eraser_btn, [](lv_event_t *) {
        S->erase = !S->erase;
        update_toolbar();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t * clear = gui_button(c, 688, 274, 100, 70, GUI_C_KEY, 16);
    gui_icon(clear, GUI_ICON_TRASH, GUI_F_ICON_S, GUI_C_TEXT);
    gui_label(clear, "ล้างจอ", GUI_F_SMALL);
    lv_obj_add_event_cb(clear, [](lv_event_t *) { clear_canvas(); }, LV_EVENT_CLICKED, NULL);

    clear_canvas();
    update_toolbar();
}
