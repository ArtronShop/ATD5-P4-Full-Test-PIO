#include "gui_common.h"

// Module 6: USB OTG test - is a device plugged in, and what is it called?
// Uses the ESP-IDF USB host stack. The host is installed on the first visit of this page and
// stays installed (the device information is kept up to date in the background).

#if __has_include("usb/usb_host.h")
#define GUI_HAS_USB_HOST 1
#include "usb/usb_host.h"
#endif

namespace {

struct UsbInfo {
    bool     connected = false;
    char     product[64] = "";
    char     vendor[48] = "";
    uint16_t vid = 0, pid = 0;
    uint8_t  cls = 0;
    int      speed = -1;   // 0 low, 1 full, 2 high
};

UsbInfo g_info;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
bool g_started = false;
bool g_failed = false;

const char * class_name(uint8_t c) {
    switch (c) {
        case 0x01: return "Audio";
        case 0x02: return "CDC (Serial)";
        case 0x03: return "HID (Keyboard / Mouse)";
        case 0x06: return "Still Image";
        case 0x07: return "Printer";
        case 0x08: return "Mass Storage";
        case 0x09: return "Hub";
        case 0x0A: return "CDC Data";
        case 0x0E: return "Video (Webcam)";
        case 0xE0: return "Wireless";
        case 0xFF: return "Vendor Specific";
        default:   return "Unknown";
    }
}

const char * speed_name(int s) {
    switch (s) {
        case 0:  return "Low-Speed 1.5 Mbps";
        case 1:  return "Full-Speed 12 Mbps";
        case 2:  return "High-Speed 480 Mbps";
        default: return "-";
    }
}

#ifdef GUI_HAS_USB_HOST

usb_host_client_handle_t g_client = nullptr;
usb_device_handle_t      g_dev = nullptr;

// UTF-16 string descriptor -> UTF-8 (BMP only)
void str_desc_to_utf8(const usb_str_desc_t * d, char * out, size_t n) {
    out[0] = 0;
    if (!d || d->bLength < 2) {
        return;
    }
    size_t o = 0;
    int chars = (d->bLength - 2) / 2;
    for (int i = 0; i < chars; i++) {
        uint16_t u = d->wData[i];
        if (u < 0x80) {
            if (o + 1 >= n) break;
            out[o++] = (char) u;
        } else if (u < 0x800) {
            if (o + 2 >= n) break;
            out[o++] = 0xC0 | (u >> 6);
            out[o++] = 0x80 | (u & 0x3F);
        } else {
            if (o + 3 >= n) break;
            out[o++] = 0xE0 | (u >> 12);
            out[o++] = 0x80 | ((u >> 6) & 0x3F);
            out[o++] = 0x80 | (u & 0x3F);
        }
    }
    out[o] = 0;
}

void on_new_device(uint8_t address) {
    if (g_dev) { // single device is enough for this test
        usb_host_device_close(g_client, g_dev);
        g_dev = nullptr;
    }
    usb_device_handle_t dev;
    if (usb_host_device_open(g_client, address, &dev) != ESP_OK) {
        return;
    }
    g_dev = dev;

    UsbInfo info;
    info.connected = true;

    usb_device_info_t di;
    if (usb_host_device_info(dev, &di) == ESP_OK) {
        info.speed = (int) di.speed;
        str_desc_to_utf8(di.str_desc_product, info.product, sizeof(info.product));
        str_desc_to_utf8(di.str_desc_manufacturer, info.vendor, sizeof(info.vendor));
    }

    const usb_device_desc_t * dd;
    if (usb_host_get_device_descriptor(dev, &dd) == ESP_OK) {
        info.vid = dd->idVendor;
        info.pid = dd->idProduct;
        info.cls = dd->bDeviceClass;
    }
    if (info.cls == 0) { // class is defined per interface
        const usb_config_desc_t * cfg;
        if (usb_host_get_active_config_descriptor(dev, &cfg) == ESP_OK) {
            const uint8_t * p = (const uint8_t *) cfg;
            uint16_t total = cfg->wTotalLength;
            for (uint16_t i = 0; i + 9 <= total; i += p[i] ? p[i] : 1) { // walk descriptors, first interface wins
                if (p[i + 1] == 0x04) { // interface descriptor
                    info.cls = p[i + 5];
                    break;
                }
            }
        }
    }

    portENTER_CRITICAL(&g_mux);
    g_info = info;
    portEXIT_CRITICAL(&g_mux);
}

void client_event_cb(const usb_host_client_event_msg_t * msg, void *) {
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        on_new_device(msg->new_dev.address);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        if (g_dev && msg->dev_gone.dev_hdl == g_dev) {
            usb_host_device_close(g_client, g_dev);
            g_dev = nullptr;
        }
        portENTER_CRITICAL(&g_mux);
        g_info = UsbInfo();
        portEXIT_CRITICAL(&g_mux);
    }
}

void lib_task(void *) {
    while (true) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
    }
}

void client_task(void *) {
    while (true) {
        usb_host_client_handle_events(g_client, portMAX_DELAY);
    }
}

void start_host() {
    if (g_started) {
        return;
    }
    g_started = true;

    usb_host_config_t cfg = {};
    cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
    if (usb_host_install(&cfg) != ESP_OK) {
        g_failed = true;
        return;
    }

    usb_host_client_config_t cc = {};
    cc.is_synchronous = false;
    cc.max_num_event_msg = 5;
    cc.async.client_event_callback = client_event_cb;
    cc.async.callback_arg = nullptr;
    if (usb_host_client_register(&cc, &g_client) != ESP_OK) {
        g_failed = true;
        return;
    }

    xTaskCreate(lib_task, "usb_lib", 4096, NULL, 4, NULL);
    xTaskCreate(client_task, "usb_client", 6144, NULL, 4, NULL);
}

#else
void start_host() { g_failed = true; }
#endif

// ---- UI ---------------------------------------------------------------------
struct State {
    lv_obj_t * status;
    lv_obj_t * on_view;
    lv_obj_t * off_view;
    lv_obj_t * off_title;
    lv_obj_t * off_sub;
    lv_obj_t * name;
    lv_obj_t * rows[4];
    lv_timer_t * timer;
    bool last_connected = false;
    bool first = true;
};
State * S = nullptr;

void update_cb(lv_timer_t *) {
    UsbInfo info;
    portENTER_CRITICAL(&g_mux);
    info = g_info;
    portEXIT_CRITICAL(&g_mux);

    if (!S->first && info.connected == S->last_connected && !info.connected) {
        return;
    }
    S->first = false;
    S->last_connected = info.connected;

    if (info.connected) {
        lv_obj_add_flag(S->off_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(S->on_view, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(S->status, "เชื่อมต่อแล้ว");
        lv_obj_set_style_text_color(S->status, lv_color_hex(GUI_C_OK), 0);

        char vp[24];
        lv_label_set_text(S->name, info.product[0] ? info.product : "USB Device");
        lv_label_set_text(S->rows[0], info.vendor[0] ? info.vendor : "-");
        snprintf(vp, sizeof(vp), "%04X : %04X", info.vid, info.pid);
        lv_label_set_text(S->rows[1], vp);
        lv_label_set_text(S->rows[2], class_name(info.cls));
        lv_label_set_text(S->rows[3], speed_name(info.speed));
    } else {
        lv_obj_add_flag(S->on_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(S->off_view, LV_OBJ_FLAG_HIDDEN);
        if (g_failed) {
            lv_label_set_text(S->status, "USB Host ใช้งานไม่ได้");
            lv_obj_set_style_text_color(S->status, lv_color_hex(GUI_C_BAD), 0);
            lv_label_set_text(S->off_title, "เริ่ม USB Host ไม่สำเร็จ");
            lv_label_set_text(S->off_sub, "ตรวจสอบการตั้งค่า USB ของบอร์ด");
        } else {
            lv_label_set_text(S->status, "ไม่มีอุปกรณ์");
            lv_obj_set_style_text_color(S->status, lv_color_hex(GUI_C_MUTED), 0);
            lv_label_set_text(S->off_title, "ไม่พบอุปกรณ์");
            lv_label_set_text(S->off_sub, "กรุณาเสียบอุปกรณ์ที่พอร์ต USB OTG");
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

} // namespace

void gui_usb_build(gui_page_t & pg) {
    S = new State();
    lv_obj_t * c = pg.content;
    const uint32_t ac = pg.accent;
    lv_obj_add_event_cb(pg.screen, screen_delete_cb, LV_EVENT_DELETE, NULL);

    S->status = gui_label(pg.header_right, "", GUI_F_NORMAL, GUI_C_MUTED);

    lv_obj_t * card = gui_card(c, 20, 22, 760, 340, 22);

    // ---- connected view
    S->on_view = gui_obj(card, 0, 0, 758, 338);
    lv_obj_t * box = gui_obj(S->on_view, 34, 94, 150, 150);
    lv_obj_set_style_radius(box, 36, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(ac), 0);
    lv_obj_set_style_bg_opa(box, 34, 0);
    lv_obj_t * ic = gui_icon(box, GUI_ICON_USB, GUI_F_ICON_L, ac);
    lv_obj_center(ic);
    lv_obj_t * badge = gui_obj(S->on_view, 150, 214, 46, 46);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(GUI_C_OK), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_t * ck = gui_icon(badge, GUI_ICON_CHECK, GUI_F_ICON_S, GUI_C_DARK_TEXT);
    lv_obj_center(ck);

    lv_obj_t * found = gui_label(S->on_view, "พบอุปกรณ์ USB", GUI_F_NORMAL, GUI_C_OK);
    lv_obj_set_pos(found, 220, 56);
    S->name = gui_label(S->on_view, "", GUI_F_TITLE);
    lv_label_set_long_mode(S->name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(S->name, 510, 42);
    lv_obj_set_pos(S->name, 220, 86);

    static const char * const KEYS[4] = { "ผู้ผลิต", "VID : PID", "ชนิด", "ความเร็ว" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t * k = gui_label(S->on_view, KEYS[i], GUI_F_NORMAL, GUI_C_MUTED);
        lv_obj_set_pos(k, 220, 148 + i * 40);
        S->rows[i] = gui_label(S->on_view, "", GUI_F_NORMAL);
        gui_label_ellipsis(S->rows[i], 380);
        lv_obj_set_pos(S->rows[i], 350, 148 + i * 40);
    }

    // ---- not connected view
    S->off_view = gui_obj(card, 0, 0, 758, 338);
    lv_obj_set_flex_flow(S->off_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(S->off_view, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(S->off_view, 12, 0);
    gui_icon(S->off_view, GUI_ICON_USB, GUI_F_ICON_L, 0x566180);
    S->off_title = gui_label(S->off_view, "", GUI_F_TITLE);
    S->off_sub = gui_label(S->off_view, "", GUI_F_NORMAL, GUI_C_MUTED);
    lv_obj_add_flag(S->on_view, LV_OBJ_FLAG_HIDDEN);

    start_host();
    S->timer = lv_timer_create(update_cb, 300, NULL);
    update_cb(NULL);
}
