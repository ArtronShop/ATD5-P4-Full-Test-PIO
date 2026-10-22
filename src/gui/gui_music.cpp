#include <math.h>
#include "gui_common.h"

// Module 3: speaker test - plays built-in melodies (synthesized, 16 kHz mono)
// through the on-board I2S amplifier. No SD card needed.

namespace {

// ---- Melodies --------------------------------------------------------------
enum { R = 0, C4 = 262, D4 = 294, E4 = 330, F4 = 349, G4 = 392, A4 = 440, B4 = 494,
       C5 = 523, D5 = 587, E5 = 659, F5 = 698, G5 = 784 };

struct Note { uint16_t freq; uint16_t ms; };
struct Song { const char * name; const Note * notes; uint16_t count; };

const Note TWINKLE[] = {
    {C4,400},{C4,400},{G4,400},{G4,400},{A4,400},{A4,400},{G4,800},
    {F4,400},{F4,400},{E4,400},{E4,400},{D4,400},{D4,400},{C4,800},
    {G4,400},{G4,400},{F4,400},{F4,400},{E4,400},{E4,400},{D4,800},
    {G4,400},{G4,400},{F4,400},{F4,400},{E4,400},{E4,400},{D4,800},
    {C4,400},{C4,400},{G4,400},{G4,400},{A4,400},{A4,400},{G4,800},
    {F4,400},{F4,400},{E4,400},{E4,400},{D4,400},{D4,400},{C4,800},
};
const Note BIRTHDAY[] = {
    {G4,300},{G4,150},{A4,450},{G4,450},{C5,450},{B4,900},
    {G4,300},{G4,150},{A4,450},{G4,450},{D5,450},{C5,900},
    {G4,300},{G4,150},{G5,450},{E5,450},{C5,450},{B4,450},{A4,900},
    {F5,300},{F5,150},{E5,450},{C5,450},{D5,450},{C5,900},
};
const Note SCALE[] = {
    {C4,350},{D4,350},{E4,350},{F4,350},{G4,350},{A4,350},{B4,350},{C5,700},
    {B4,350},{A4,350},{G4,350},{F4,350},{E4,350},{D4,350},{C4,700},
};
const Song SONGS[] = {
    { "Twinkle Twinkle Little Star", TWINKLE,  sizeof(TWINKLE)  / sizeof(Note) },
    { "Happy Birthday",              BIRTHDAY, sizeof(BIRTHDAY) / sizeof(Note) },
    { "ทดสอบเสียง โด เร มี",           SCALE,    sizeof(SCALE)    / sizeof(Note) },
};
constexpr int SONG_COUNT = sizeof(SONGS) / sizeof(SONGS[0]);

constexpr int SAMPLE_RATE = 16000;           // must match Sound.begin()
constexpr int SAMPLES_PER_MS = SAMPLE_RATE / 1000;
constexpr int CHUNK = 256;                   // samples per I2S write (16 ms)

// ---- Player state shared with the audio task --------------------------------
volatile bool     g_playing = false;
volatile int      g_song = 0;
volatile int      g_volume = 60;             // 0..100
volatile uint32_t g_pos = 0;                 // position in samples
volatile uint32_t g_epoch = 0;               // bumped when the UI seeks / changes song
TaskHandle_t      g_task = nullptr;

uint32_t song_samples(int s) {
    uint32_t total = 0;
    for (int i = 0; i < SONGS[s].count; i++) {
        total += SONGS[s].notes[i].ms * SAMPLES_PER_MS;
    }
    return total;
}

void audio_task(void *) {
    int16_t buf[CHUNK];
    float phase = 0;

    while (true) {
        if (!g_playing) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        const uint32_t epoch = g_epoch;
        const int si = g_song;
        const Song & sg = SONGS[si];
        const uint32_t pos = g_pos;
        const float peak = 16000.0f * (g_volume / 100.0f) * (g_volume / 100.0f);

        // locate the note at `pos`
        int idx = 0;
        uint32_t start = 0;
        while (idx < sg.count && pos >= start + sg.notes[idx].ms * SAMPLES_PER_MS) {
            start += sg.notes[idx].ms * SAMPLES_PER_MS;
            idx++;
        }
        if (idx >= sg.count) { // song finished -> next song
            g_song = (si + 1) % SONG_COUNT;
            g_pos = 0;
            g_epoch++;
            continue;
        }

        for (int i = 0; i < CHUNK; i++) {
            uint32_t t = pos + i;
            while (idx < sg.count && t >= start + sg.notes[idx].ms * SAMPLES_PER_MS) {
                start += sg.notes[idx].ms * SAMPLES_PER_MS;
                idx++;
            }
            if (idx >= sg.count) {
                buf[i] = 0;
                continue;
            }
            const Note & n = sg.notes[idx];
            uint32_t len = n.ms * SAMPLES_PER_MS;
            uint32_t rel = t - start;
            float s = 0;
            if (n.freq) {
                float env = 1.0f;
                if (rel < 160) env = rel / 160.0f;                       // 10 ms attack
                if (len - rel < 640) env = fminf(env, (len - rel) / 640.0f); // 40 ms release (gap between notes)
                phase += 2.0f * (float) M_PI * n.freq / SAMPLE_RATE;
                if (phase > 2.0f * (float) M_PI) phase -= 2.0f * (float) M_PI;
                s = (sinf(phase) + 0.25f * sinf(2 * phase)) * env;
            }
            buf[i] = (int16_t) (s * peak);
        }

        Sound.play(buf, sizeof(buf)); // blocks until the DMA accepted the data
        if (epoch == g_epoch) {
            g_pos = pos + CHUNK;
        }
        // Let other tasks take the I2S driver mutex. Without this, the touch beep
        // (Sound.play called from the LVGL task) starves forever and the whole UI freezes.
        vTaskDelay(1);
    }
}

// ---- UI ---------------------------------------------------------------------
struct State {
    lv_obj_t * title;
    lv_obj_t * sub;
    lv_obj_t * progress;
    lv_obj_t * t_now;
    lv_obj_t * t_total;
    lv_obj_t * play_icon;
    lv_obj_t * vol_label;
    lv_obj_t * bars[12];
    lv_timer_t * timer;
    int shown_song = -1;
};
State * S = nullptr;

void set_song(int s) {
    g_song = (s + SONG_COUNT) % SONG_COUNT;
    g_pos = 0;
    g_epoch++;
}

void fmt_time(char * out, size_t n, uint32_t samples) {
    uint32_t sec = samples / SAMPLE_RATE;
    snprintf(out, n, "%02u:%02u", (unsigned) (sec / 60), (unsigned) (sec % 60));
}

void tick_cb(lv_timer_t *) {
    int song = g_song;
    if (song != S->shown_song) {
        S->shown_song = song;
        lv_label_set_text(S->title, SONGS[song].name);
        lv_label_set_text_fmt(S->sub, "เพลงที่ %d / %d · ทดสอบลำโพง", song + 1, SONG_COUNT);
        char b[8];
        fmt_time(b, sizeof(b), song_samples(song));
        lv_label_set_text(S->t_total, b);
    }

    uint32_t total = song_samples(song);
    uint32_t pos = g_pos;
    lv_slider_set_value(S->progress, total ? (int) ((uint64_t) pos * 1000 / total) : 0, LV_ANIM_OFF);
    char b[8];
    fmt_time(b, sizeof(b), pos);
    lv_label_set_text(S->t_now, b);

    lv_label_set_text(S->play_icon, g_playing ? GUI_ICON_PAUSE : GUI_ICON_PLAY);

    for (int i = 0; i < 12; i++) { // fake equalizer
        int h = g_playing ? (int) lv_rand(14, 120) : 12;
        lv_obj_set_height(S->bars[i], h);
        lv_obj_align(S->bars[i], LV_ALIGN_BOTTOM_LEFT, 4 + i * 14, 0);
    }
}

void screen_delete_cb(lv_event_t *) {
    g_playing = false; // stop sound when leaving the page
    if (S) {
        lv_timer_delete(S->timer);
        delete S;
        S = nullptr;
    }
}

lv_obj_t * circle_button(lv_obj_t * parent, int cx, int cy, int d, uint32_t bg, const char * icon, uint32_t fg) {
    lv_obj_t * b = gui_button(parent, cx - d / 2, cy - d / 2, d, d, bg, LV_RADIUS_CIRCLE);
    lv_obj_set_ext_click_area(b, 6);
    gui_icon(b, icon, GUI_F_ICON_S, fg);
    return b;
}

void style_slider(lv_obj_t * s, uint32_t track, uint32_t fill, int knob_pad) {
    lv_obj_set_style_bg_color(s, lv_color_hex(track), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, lv_color_hex(fill), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, knob_pad, LV_PART_KNOB);
}

} // namespace

void gui_music_build(gui_page_t & pg) {
    if (!g_task) {
        xTaskCreate(audio_task, "gui_audio", 4096, NULL, 1, &g_task);
    }
    g_playing = false;
    g_pos = 0;
    g_epoch++;

    S = new State();
    lv_obj_t * c = pg.content;
    const uint32_t ac = pg.accent;

    // album art with equalizer
    lv_obj_t * art = gui_card(c, 24, 30, 250, 250, 22);
    lv_obj_set_style_bg_color(art, lv_color_hex(0x231530), 0);
    lv_obj_t * note = gui_icon(art, GUI_ICON_MUSIC, GUI_F_ICON_L, ac);
    lv_obj_align(note, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_t * eq = gui_obj(art, 0, 0, 172, 120);
    lv_obj_align(eq, LV_ALIGN_BOTTOM_MID, 0, -20);
    for (int i = 0; i < 12; i++) {
        lv_obj_t * bar = gui_obj(eq, 4 + i * 14, 0, 9, 12);
        lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(ac), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 4 + i * 14, 0);
        S->bars[i] = bar;
    }

    // title
    S->title = gui_label(c, "", GUI_F_TITLE);
    gui_label_ellipsis(S->title, 468);
    lv_obj_set_pos(S->title, 304, 22);
    S->sub = gui_label(c, "", GUI_F_NORMAL, GUI_C_MUTED);
    lv_obj_set_pos(S->sub, 304, 64);

    // progress
    S->progress = lv_slider_create(c);
    lv_slider_set_range(S->progress, 0, 1000);
    lv_obj_remove_flag(S->progress, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(S->progress, 468, 10);
    lv_obj_set_pos(S->progress, 304, 112);
    style_slider(S->progress, 0x26304D, ac, 7);
    S->t_now = gui_label(c, "00:00", GUI_F_SMALL, GUI_C_MUTED);
    lv_obj_set_pos(S->t_now, 304, 134);
    S->t_total = gui_label(c, "00:00", GUI_F_SMALL, GUI_C_MUTED);
    lv_obj_align(S->t_total, LV_ALIGN_TOP_RIGHT, -28, 134);

    // controls
    lv_obj_t * prev = circle_button(c, 424, 231, 72, GUI_C_KEY, GUI_ICON_PREV, GUI_C_TEXT);
    lv_obj_t * play = circle_button(c, 538, 231, 96, ac, GUI_ICON_PLAY, GUI_C_DARK_TEXT);
    lv_obj_t * next = circle_button(c, 652, 231, 72, GUI_C_KEY, GUI_ICON_NEXT, GUI_C_TEXT);
    S->play_icon = lv_obj_get_child(play, 0);
    lv_obj_set_style_text_font(S->play_icon, GUI_F_ICON_M, 0);

    lv_obj_add_event_cb(prev, [](lv_event_t *) { set_song(g_song - 1); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(next, [](lv_event_t *) { set_song(g_song + 1); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(play, [](lv_event_t *) { g_playing = !g_playing; }, LV_EVENT_CLICKED, NULL);

    // volume
    lv_obj_t * vol_icon = gui_icon(c, GUI_ICON_VOLUME, GUI_F_ICON_S, 0xAAB3C8);
    lv_obj_set_pos(vol_icon, 24, 326);
    lv_obj_t * vol = lv_slider_create(c);
    lv_slider_set_range(vol, 0, 100);
    lv_slider_set_value(vol, g_volume, LV_ANIM_OFF);
    lv_obj_set_size(vol, 600, 12);
    lv_obj_set_pos(vol, 84, 336);
    lv_obj_set_ext_click_area(vol, 18);
    style_slider(vol, 0x26304D, 0xAAB3C8, 8);
    S->vol_label = gui_label(c, "", GUI_F_LARGE);
    lv_label_set_text_fmt(S->vol_label, "%d%%", (int) g_volume);
    lv_obj_align(S->vol_label, LV_ALIGN_TOP_RIGHT, -28, 324);
    lv_obj_add_event_cb(vol, [](lv_event_t * e) {
        g_volume = lv_slider_get_value((lv_obj_t *) lv_event_get_target(e));
        lv_label_set_text_fmt(S->vol_label, "%d%%", (int) g_volume);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    S->timer = lv_timer_create(tick_cb, 100, NULL);
    lv_obj_add_event_cb(pg.screen, screen_delete_cb, LV_EVENT_DELETE, NULL);
    tick_cb(NULL);
}
