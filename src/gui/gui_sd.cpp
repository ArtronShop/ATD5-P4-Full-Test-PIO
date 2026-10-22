#include <algorithm>
#include <strings.h>
#include <string>
#include <vector>
#include "gui_common.h"

// Module 4: MicroSD card test - mount the card and browse folders / files.
//
// Mounting the card and reading a folder can take seconds, so it is done in a
// short-lived FreeRTOS task pinned to core 0 (the Arduino loop task, which runs
// LVGL, lives on core 1). The UI shows a spinner meanwhile.

namespace {

constexpr int MAX_ENTRIES = 100;
constexpr int ROW_H = 62;

struct Entry {
    std::string name;
    bool        dir;
    uint64_t    size;
};

// Result of one load job (allocated by the worker, freed by the UI callback)
struct LoadResult {
    bool               card_ok = false;
    std::string        path;
    std::string        info;      // "SDHC · 29.72 GB"
    std::vector<Entry> entries;
    uint32_t           ticket = 0;
};

struct State {
    lv_obj_t * status;      // header right icon
    lv_obj_t * path_label;
    lv_obj_t * info_label;
    lv_obj_t * list;
    lv_obj_t * loading_view;
    lv_obj_t * loading_text;
    lv_obj_t * empty_view;  // "no card" view
    std::string path = "/";
    std::vector<Entry> entries;
    int  pending = -2;      // row pressed (-1 = parent folder), handled outside the event
    bool busy = false;
};
State * S = nullptr;
uint32_t g_ticket = 0;      // identifies the latest load job (global: survives page re-entry)

// ---- helpers ------------------------------------------------------------------
std::string format_size(uint64_t b) {
    char s[24];
    if (b < 1024)                       snprintf(s, sizeof(s), "%u B", (unsigned) b);
    else if (b < 1024ULL * 1024)        snprintf(s, sizeof(s), "%.1f KB", b / 1024.0);
    else if (b < 1024ULL * 1024 * 1024) snprintf(s, sizeof(s), "%.1f MB", b / 1048576.0);
    else                                snprintf(s, sizeof(s), "%.2f GB", b / 1073741824.0);
    return s;
}

// ---- worker (NOT on the LVGL core; must not touch any lv_* object) --------------
struct Job {
    std::string path;
    uint32_t    ticket;
};

bool mount_card() {
    pinMode(SD_CD_PIN, INPUT_PULLUP);
    if (digitalRead(SD_CD_PIN) != LOW) { // CD = HIGH => no card
        if (Card.cardType() != CARD_NONE) {
            Card.end();
        }
        return false;
    }
    if (Card.cardType() != CARD_NONE) {
        return true;
    }
    return Card.begin();
}

void deliver(void * arg); // runs in the LVGL context

void load_task(void * arg) {
    Job * job = (Job *) arg;
    LoadResult * r = new LoadResult();
    r->path = job->path;
    r->ticket = job->ticket;
    delete job;

    r->card_ok = mount_card();
    if (r->card_ok) {
        static const char * const TYPES[] = { "?", "MMC", "SD", "SDHC", "?" };
        int t = (int) Card.cardType();
        r->info = std::string(TYPES[t >= 0 && t < 5 ? t : 0]) + " · " + format_size(Card.cardSize());

        File dir = Card.open(r->path.c_str());
        if (dir && dir.isDirectory()) {
            while (r->entries.size() < MAX_ENTRIES) {
                File f = dir.openNextFile();
                if (!f) {
                    break;
                }
                std::string n = f.name();
                size_t slash = n.find_last_of('/');
                if (slash != std::string::npos) {
                    n = n.substr(slash + 1);
                }
                bool is_dir = f.isDirectory();
                r->entries.push_back({ n, is_dir, is_dir ? 0 : (uint64_t) f.size() });
                f.close();
            }
            dir.close();
        }
        std::sort(r->entries.begin(), r->entries.end(), [](const Entry & a, const Entry & b) {
            if (a.dir != b.dir) return a.dir;
            return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
        });
    }

    // NOTE: lv_safe_update() copies the std::function bit-by-bit; use a capture-less lambda + user_data.
    lv_safe_update([](void * a) { deliver(a); }, r);
    vTaskDelete(NULL);
}

void start_load(const std::string & path) {
    S->busy = true;
    g_ticket++;
    Job * job = new Job{ path, g_ticket };

    // spinner instead of the list while waiting
    lv_obj_add_flag(S->list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(S->empty_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(S->loading_view, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(S->loading_text, path == "/" ? "กำลังอ่าน MicroSD Card..." : "กำลังเปิดโฟลเดอร์...");

    xTaskCreatePinnedToCore(load_task, "sd_load", 6144, job, 1, NULL, 0);
}

// ---- UI -----------------------------------------------------------------------
void row_cb(lv_event_t * e);

void add_row(int index, const char * icon, uint32_t icon_color, const char * name, const char * right, bool chevron) {
    lv_obj_t * row = gui_obj(S->list, 0, 0, GUI_W, ROW_H);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE); // let the list scroll when dragged
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(GUI_C_LINE), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1A2238), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void *) (intptr_t) index);

    lv_obj_t * ic = gui_icon(row, icon, GUI_F_ICON_S, icon_color);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 18, 0);

    lv_obj_t * n = gui_label(row, name, GUI_F_LARGE, index == -1 ? GUI_C_MUTED : GUI_C_TEXT);
    gui_label_ellipsis(n, 480);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 70, 0);

    if (right && right[0]) {
        lv_obj_t * r = gui_label(row, right, GUI_F_NORMAL, GUI_C_MUTED);
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, chevron ? -62 : -24, 0);
    }
    if (chevron) {
        lv_obj_t * ch = gui_icon(row, GUI_ICON_RIGHT, GUI_F_ICON_S, 0x566180);
        lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -22, 0);
    }
}

void show_no_card() {
    lv_obj_add_flag(S->list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(S->loading_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(S->empty_view, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(S->path_label, "ไม่พบการ์ด");
    lv_label_set_text(S->info_label, "");
    lv_label_set_text(S->status, GUI_ICON_WARNING);
    lv_obj_set_style_text_color(S->status, lv_color_hex(GUI_C_BAD), 0);
}

void apply_result(LoadResult * r) {
    S->busy = false;
    lv_obj_add_flag(S->loading_view, LV_OBJ_FLAG_HIDDEN);

    if (!r->card_ok) {
        show_no_card();
        return;
    }
    S->path = r->path;
    S->entries = r->entries;

    lv_obj_add_flag(S->empty_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(S->list, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(S->status, GUI_ICON_CHECK);
    lv_obj_set_style_text_color(S->status, lv_color_hex(GUI_C_OK), 0);

    std::string shown = "/sdcard" + (S->path == "/" ? std::string() : S->path);
    lv_label_set_text(S->path_label, shown.c_str());
    lv_label_set_text(S->info_label, r->info.c_str());

    lv_obj_clean(S->list);
    lv_obj_scroll_to_y(S->list, 0, LV_ANIM_OFF);

    if (S->path != "/") {
        add_row(-1, GUI_ICON_LEVEL_UP, GUI_C_MUTED, "..", "ย้อนกลับ", false);
    }
    for (size_t i = 0; i < S->entries.size(); i++) {
        const Entry & en = S->entries[i];
        if (en.dir) {
            add_row((int) i, GUI_ICON_FOLDER, 0xFACC15, en.name.c_str(), "", true);
        } else {
            add_row((int) i, GUI_ICON_FILE, 0xAAB3C8, en.name.c_str(), format_size(en.size).c_str(), false);
        }
    }
    if (S->entries.empty()) {
        lv_obj_t * l = gui_label(S->list, "โฟลเดอร์ว่าง", GUI_F_LARGE, GUI_C_MUTED);
        lv_obj_set_style_pad_all(l, 24, 0);
    }
}

void deliver(void * arg) {
    LoadResult * r = (LoadResult *) arg;
    if (S && r->ticket == g_ticket) { // page still open and this is the latest job
        apply_result(r);
    }
    delete r;
}

void navigate_async(void *) {
    if (!S || S->pending == -2 || S->busy) {
        return;
    }
    int idx = S->pending;
    S->pending = -2;
    std::string target;
    if (idx == -1) { // parent folder
        size_t slash = S->path.find_last_of('/');
        target = (slash == 0 || slash == std::string::npos) ? "/" : S->path.substr(0, slash);
    } else if (idx >= 0 && idx < (int) S->entries.size() && S->entries[idx].dir) {
        target = S->path + (S->path == "/" ? "" : "/") + S->entries[idx].name;
    } else {
        return; // a file: nothing to do
    }
    start_load(target);
}

void row_cb(lv_event_t * e) {
    if (S->busy) {
        return;
    }
    S->pending = (int) (intptr_t) lv_event_get_user_data(e);
    lv_async_call(navigate_async, NULL); // the rows get deleted while navigating
}

void screen_delete_cb(lv_event_t *) {
    delete S;      // a running worker only delivers into a freed S -> deliver() checks S first
    S = nullptr;
}

} // namespace

void gui_sd_build(gui_page_t & pg) {
    S = new State();
    lv_obj_t * c = pg.content;
    lv_obj_add_event_cb(pg.screen, screen_delete_cb, LV_EVENT_DELETE, NULL);

    S->status = gui_label(pg.header_right, "", GUI_F_ICON_S, GUI_C_OK);

    // path bar
    lv_obj_t * bar = gui_obj(c, 0, 0, GUI_W, 56);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x10162A), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    S->path_label = gui_label(bar, "", GUI_F_LARGE_B, pg.accent);
    gui_label_ellipsis(S->path_label, 520);
    lv_obj_align(S->path_label, LV_ALIGN_LEFT_MID, 18, -1);
    S->info_label = gui_label(bar, "", GUI_F_SMALL, GUI_C_MUTED);
    lv_obj_align(S->info_label, LV_ALIGN_RIGHT_MID, -18, 0);

    // file list
    S->list = gui_obj(c, 0, 56, GUI_W, 328);
    lv_obj_add_flag(S->list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(S->list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(S->list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(S->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(S->list, lv_color_hex(0x323B57), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(S->list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(S->list, 6, LV_PART_SCROLLBAR);
    lv_obj_add_flag(S->list, LV_OBJ_FLAG_HIDDEN);

    // loading view (spinner)
    S->loading_view = gui_obj(c, 0, 56, GUI_W, 328);
    lv_obj_set_flex_flow(S->loading_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(S->loading_view, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(S->loading_view, 16, 0);
    gui_spinner(S->loading_view, 64, pg.accent, 0x323B57, 6);
    S->loading_text = gui_label(S->loading_view, "", GUI_F_NORMAL, GUI_C_MUTED);

    // "no card" view
    S->empty_view = gui_obj(c, 0, 56, GUI_W, 328);
    lv_obj_set_flex_flow(S->empty_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(S->empty_view, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(S->empty_view, 10, 0);
    gui_icon(S->empty_view, GUI_ICON_SDCARD, GUI_F_ICON_L, 0x566180);
    gui_label(S->empty_view, "ไม่พบ MicroSD Card", GUI_F_TITLE);
    gui_label(S->empty_view, "ใส่การ์ดแล้วกดปุ่มลองใหม่", GUI_F_NORMAL, GUI_C_MUTED);
    lv_obj_t * retry = gui_button(S->empty_view, 0, 0, 220, 60, pg.accent, 16);
    lv_obj_set_flex_flow(retry, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(retry, 10, 0);
    gui_icon(retry, GUI_ICON_REFRESH, GUI_F_ICON_S, GUI_C_DARK_TEXT);
    gui_label(retry, "ลองใหม่", GUI_F_LARGE_B, GUI_C_DARK_TEXT);
    lv_obj_add_event_cb(retry, [](lv_event_t *) {
        if (!S->busy) {
            start_load(S->path);
        }
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(S->empty_view, LV_OBJ_FLAG_HIDDEN);

    start_load("/");
}
