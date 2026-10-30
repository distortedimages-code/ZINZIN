/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* ZINZIN for the M-VAVE FM-1: four tracks (BOUM the drums, MATOU the synth, MAMMOUTH the bass, BAZAR the bêtises on
 * the whole mix), knobs that are bundles of controls that drift on their own, and a screen that looks like a personal
 * web page from 1999 and reacts to all of it.
 *
 * The panel:
 *   K1..K4            BIDOCHE, CHATOUILLE, MOISI, FLEMME of the selected track
 *   ARP held + K1..K4 BOUCAN (the level) of BOUM, MATOU, MAMMOUTH, BAZAR
 *   ALGO              HUMEUR: seven moods from euphoria to the low one (the scale, the beat, the screen)
 *   PRESET            APÉRO: how much everything drifts on its own
 *   SELECT            CAFÉINE: the tempo, and rolls when it gets high
 *   MASTER            the volume
 *   FX SEL ENV LFO    pick BOUM, MATOU, MAMMOUTH, BAZAR
 *   PLAY              ZOU: the sequencer on and off (MIDI start and stop too)
 *   EDIT              PAS: the 16 white keys are the 16 steps, the black keys pick the lane / the bêtise / the note
 *   GLO               AU PIF: a new pattern for the selected track (SEQ: for all four)
 *   HOME              MUET: the selected track quiet
 *   REC               ENREGISTRE: while it plays, what you play is written into the pattern
 *   SAVE              keep the song (knobs, patterns, mutes)
 *   OCT- / OCT+       CAVE / GRENIER: the keys an octave down / up
 *   the keys          play the selected track (BOUM: the four drums; BAZAR: the six bêtises); MIDI in too
 *
 * Part of fm1-base's unity build (base/device/unity.c), after the platform: gfx.c's canvas (cv_px), lcd_blit and
 * lcd_sync are in scope. The engine (dsp/zz_engine.c) and the screen (zz_draw.c) are units of their own, at -O2. */
#include "zz_engine.h"
#include "zz_draw.h"

static uint32_t ur_state = 0x9E3779B9u;
static float ur(void)                            /* the panel's own dice */
{
    ur_state ^= ur_state << 13;
    ur_state ^= ur_state >> 17;
    ur_state ^= ur_state << 5;
    return (float)(ur_state >> 8) * (1.0f / 16777216.0f);
}
static float cl01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

#include "zz_song.c"

static const uint8_t WHITE_K[16] = PLAT_WHITE_KEYS, BLACK_K[11] = PLAT_BLACK_KEYS;
static const uint8_t TRK_BTN[4] = {B_FX, B_SEL, B_ENV, B_LFO};

static struct {
    uint32_t btn, keys, step_ms, over_t, leds[4];
    int bet_sel, pick_deg, shed, shown;
} ui = {0, 0, 0, 0, {0xFFFFFFFFu, 0, 0, 0}, 1, 0, 0, 0};

static int index_of(const uint8_t *a, int n, int v)
{
    int i;
    for (i = 0; i < n; i++)
        if (a[i] == v)
            return i;
    return -1;
}

void app_say(const char *a, const char *b) { zz_show_say(a, b); }

/* ------------------------------------------------------------------ the screen: a frame every 80 ms, in bands */
#define BAND 40
static struct { int band, active; uint32_t t0, fps_t0, fps_n; } fr;

static void draw_band(int b)
{
    uint16_t *dst = cv_px + (uint32_t)(b & 1) * (240u * BAND);   /* the band not being sent */
    zz_rows(b * BAND, BAND, dst);
    lcd_blit(0, (uint32_t)(b * BAND), 240, BAND, dst);           /* sent while the next one is drawn */
}
static void draw_service(uint32_t budget_us)
{
    uint32_t now = plat_ms(), t0 = plat_ticks24();
    if (!fr.active) {
        if (now - fr.t0 < 80u)
            return;
        fr.t0 = now;
        if (!zz_frame_start())
            return;
        fr.band = 0;
        fr.active = 1;
    }
    while (fr.active) {                          /* a few bands a pass: the keys stay quick */
        draw_band(fr.band++);
        if (fr.band >= 240 / BAND) {
            fr.active = 0;
            fr.fps_n++;                          /* frames a second, and the sound's load (ARP + SAVE shows them) */
            if (now - fr.fps_t0 >= 2000u) {
                zz_view.fps10 = (int)(fr.fps_n * 10000u / (now - fr.fps_t0));
                zz_view.cpu = (int)plat_cpu_pct();
                fr.fps_n = 0;
                fr.fps_t0 = now;
            }
        }
        if ((plat_ticks24() - t0) / PLAT_TICKS_PER_US > budget_us)
            break;
    }
}
static void draw_now(void)                       /* a whole frame at once (the splash) */
{
    int b;
    if (!zz_frame_start())
        return;
    for (b = 0; b < 240 / BAND; b++)
        draw_band(b);
    fr.active = 0;
    lcd_sync();
}

/* ------------------------------------------------------------------ playing */
static void set_playing(int on)
{
    zz_view.playing = on;
    zz_seq(on);
    if (on) {
        zz_view.beat_n = 0;
        zz_view.beat_ms = plat_ms();
    } else
        zz_view.cur_step = -1;
}

/* ENREGISTRE: the step nearest to now (the next one if the current is half gone) */
static int rec_step(void)
{
    uint32_t half = (uint32_t)(60000.0f / zz_bpm() / 8.0f);
    int s = zz_view.cur_step < 0 ? 0 : zz_view.cur_step;
    if (plat_ms() - ui.step_ms > half)
        s = (s + 1) & 15;
    return s;
}
static void rec_write(int tr, int what, int acc)
{
    int s;
    if (!zz_view.rec || !zz_view.playing)
        return;
    s = rec_step();
    if (tr == 0) {
        song.drum[what][s] = (int8_t)(acc ? 2 : 1);
        song.drum_edited = 1;
    } else if (tr == 3)
        song.bz[s] = (int8_t)what;
    else {
        int8_t *d = tr == 1 ? song.syn_d : song.bas_d, *a = tr == 1 ? song.syn_a : song.bas_a;
        d[s] = (int8_t)midi_to_deg(tr, what);
        a[s] = (int8_t)(acc != 0);
    }
    render_notes();
    drums_out();
    song_dirty = 1;
}

/* a note on the selected track, from a key or MIDI: n a MIDI note, idx the key (the drums and BAZAR take it as a
 * choice) */
static void play_note(int n, int idx, int acc)
{
    int sel = zz_view.sel;
    float vel = acc ? 1.0f : 0.8f;
    if (sel == 0) {
        int lane = idx % 4;
        zz_play(0, lane, vel);
        zz_vis_hit(0, lane, acc);
        rec_write(0, lane, acc);
    } else if (sel == 3) {
        int b = 1 + idx % 6;
        zz_play(3, b, 1.0f);
        rec_write(3, b, 0);
    } else {
        n = snap(sel, n);
        last_note[sel] = n;
        zz_play(sel, n, vel);
        zz_vis_hit(sel, n, acc);
        rec_write(sel, n, acc);
    }
    zz_view.note_ms = plat_ms();
}

/* PAS: a step's state on the selected track (0 off, 1 on, 2 accent) */
static int step_val(int s)
{
    int sel = zz_view.sel;
    if (sel == 0)
        return song.drum[zz_view.lane][s];
    if (sel == 3)
        return song.bz[s] ? 1 : 0;
    if (sel == 1)
        return song.syn_d[s] == NODEG ? 0 : (song.syn_a[s] ? 2 : 1);
    return song.bas_d[s] == NODEG ? 0 : (song.bas_a[s] ? 2 : 1);
}
static void toggle_step(int s)
{
    int sel = zz_view.sel;
    if (sel == 0) {
        song.drum[zz_view.lane][s] = (int8_t)((song.drum[zz_view.lane][s] + 1) % 3);
        song.drum_edited = 1;
    } else if (sel == 3)
        song.bz[s] = (int8_t)(song.bz[s] == ui.bet_sel ? 0 : ui.bet_sel);
    else {
        int8_t *d = sel == 1 ? song.syn_d : song.bas_d, *a = sel == 1 ? song.syn_a : song.bas_a;
        if (d[s] == NODEG) {
            d[s] = (int8_t)midi_to_deg(sel, last_note[sel]);
            a[s] = 0;
        } else if (!a[s])
            a[s] = 1;
        else {
            d[s] = NODEG;
            a[s] = 0;
        }
    }
    render_notes();
    drums_out();
    song_dirty = 1;
}
/* PAS: a black key picks what the white keys write (and plays it) */
static void pick(int bi)
{
    int sel = zz_view.sel;
    if (sel == 0) {
        if (bi < 4) {
            zz_view.lane = bi;
            zz_play(0, bi, 0.9f);
        }
    } else if (sel == 3) {
        if (bi < 6) {
            ui.bet_sel = bi + 1;
            zz_play(3, ui.bet_sel, 1.0f);
        }
    } else {
        int n = deg_to_midi(sel, bi + song.oct * 7);
        ui.pick_deg = bi;
        last_note[sel] = n;
        zz_play(sel, n, 0.8f);
    }
}

static void press_key(int k)
{
    int wi = index_of(WHITE_K, 16, k), bi = index_of(BLACK_K, 11, k);
    if (zz_view.step_mode) {
        if (wi >= 0)
            toggle_step(wi);
        else if (bi >= 0)
            pick(bi);
        return;
    }
    play_note(53 + k + song.oct * 12 - (zz_view.sel == 2 ? 24 : 0), wi >= 0 ? wi : bi, ur() < 0.25f);
}

/* ------------------------------------------------------------------ the knobs */
static float nudge(volatile float *p, int32_t e)
{
    int32_t a = e < 0 ? -e : e;
    float v = cl01(*p + (float)e * (a >= 3 ? 3.0f : (a == 2 ? 2.0f : 1.0f)) / 48.0f);
    *p = v;
    song_dirty = 1;
    return v;
}
static void turn(int k, int32_t e)
{
    if (k >= EN_K1 && k <= EN_K4) {
        int q = k - EN_K1;
        if (ui.btn >> B_ARP & 1u)
            zz_show_knob(4, q, nudge(&zz_T[q][ZZ_BOUCAN], e));
        else
            zz_show_knob(q, zz_view.sel, nudge(&zz_T[zz_view.sel][q], e));
    } else if (k == EN_ALGO) {
        int before = mood();
        zz_show_knob(5, -1, nudge(&zz_G[ZZ_HUMEUR], e));
        if (mood() != before)
            mood_changed();
    } else if (k == EN_PRESET)
        zz_show_knob(6, -1, nudge(&zz_G[ZZ_APERO], e));
    else if (k == EN_SELECT)
        zz_show_knob(7, -1, nudge(&zz_G[ZZ_CAFEINE], e));
}

/* ------------------------------------------------------------------ the panel */
static void int_text(int v, char *t)
{
    char b[12];
    int n = 0, u = v < 0 ? -v : v;
    do
        b[n++] = (char)('0' + u % 10);
    while ((u /= 10) && n < 11);
    if (v < 0)
        *t++ = '-';
    while (n)
        *t++ = b[--n];
    *t = 0;
}

static void button(int b)
{
    int t = index_of(TRK_BTN, 4, b);
    if (t >= 0) {
        zz_view.sel = t;
        return;
    }
    switch (b) {
    case B_PLAY:
        set_playing(!zz_view.playing);
        break;
    case B_EDIT:
        zz_view.step_mode = !zz_view.step_mode;
        break;
    case B_GLO:
        gen_track(zz_view.sel);
        zz_vis_pop();
        break;
    case B_SEQ:
        for (t = 0; t < 4; t++)
            gen_track(t);
        zz_vis_pop();
        app_say("au_pif.exe", "TOUT AU PIF");
        break;
    case B_HOME:
        zz_mute[zz_view.sel] = (uint8_t)!zz_mute[zz_view.sel];
        song_dirty = 1;
        break;
    case B_REC:
        zz_view.rec = !zz_view.rec;
        app_say("enregistre.exe", zz_view.rec ? "J'ÉCOUTE" : "J'ARRÊTE");
        break;
    case B_SAVE:
        if (ui.btn >> B_ARP & 1u)                /* ARP + SAVE: frames a second and the sound's load, top right */
            zz_view.debug = !zz_view.debug;
        else
            app_say("zinzin.sav", song_save() == 0 ? "SAUVÉ !" : "RATÉ...");
        break;
    case B_OCTDN:
    case B_OCTUP: {
        char txt[16] = "octave ", n[8];
        int o = song.oct + (b == B_OCTUP ? 1 : -1), i = 7, j = 0;
        if (o < -2 || o > 2)
            break;
        song.oct = (int8_t)o;
        song_dirty = 1;
        int_text(o, n);
        if (o > 0)
            txt[i++] = '+';
        while (n[j])
            txt[i++] = n[j++];
        txt[i] = 0;
        app_say(o < 0 ? "cave.exe" : (o > 0 ? "grenier.exe" : "rdc.exe"), txt);
        break;
    }
    default:
        break;
    }
}

static void midi(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if ((st & 0xF0u) == 0x90u && d2)
            play_note((int)d1, (int)d1, d2 > 100u);
        else if (st == 0xFAu)
            set_playing(1);
        else if (st == 0xFCu)
            set_playing(0);
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), press = btn & ~ui.btn, kp = keys & ~ui.keys, k;
    int32_t e;
    ui.btn = btn;
    for (k = 0; k < NE; k++)
        if ((e = plat_enc((int)k)) != 0)
            turn((int)k, e);
    for (k = 0; k < NB; k++)
        if (press >> k & 1u)
            button((int)k);
    for (k = 0; k < NKEYS; k++)
        if (kp >> k & 1u)
            press_key((int)k);
    ui.keys = keys;
    midi();
}

static void events(void)
{
    zz_ev_t e;
    while (zz_ev_get(&e)) {
        if (e.t == ZZ_EV_HIT)
            zz_vis_hit(e.tr, e.a, e.b);
        else if (e.t == ZZ_EV_BET)
            zz_vis_bet(e.a, e.b * 10);
        else if (e.t == ZZ_EV_STEP) {
            zz_view.cur_step = e.a;
            ui.step_ms = plat_ms();
        } else if (e.t == ZZ_EV_BEAT) {
            zz_view.beat_ms = plat_ms();
            zz_view.beat_n++;
        }
    }
}

/* the lights: the track picked, PLAY, the steps in PAS (an accent bright, a note dim, the playhead blinking through) */
static void leds(void)
{
    uint32_t bb = 0, db = 0, kk = 0, dk = 0, t;
    int sel = zz_view.sel, step = zz_view.cur_step;
    for (t = 0; t < 4; t++) {
        if ((int)t == sel)
            bb |= 1u << TRK_BTN[t];
        else if (!zz_mute[t])
            db |= 1u << TRK_BTN[t];
    }
    if (zz_view.playing)
        bb |= LED_PLAY_GREEN | (zz_view.rec ? 1u << B_PLAY : 0u);
    if (zz_view.rec)
        bb |= 1u << B_REC;
    if (zz_view.step_mode)
        bb |= 1u << B_EDIT;
    if (song_dirty)
        bb |= 1u << B_SAVE;
    if (zz_mute[sel])
        bb |= 1u << B_HOME;
    if (ui.btn >> B_ARP & 1u)
        bb |= 1u << B_ARP;
    if (song.oct < 0)
        bb |= 1u << B_OCTDN;
    if (song.oct > 0)
        bb |= 1u << B_OCTUP;
    if (zz_view.step_mode) {
        int s, b = sel == 0 ? zz_view.lane : (sel == 3 ? ui.bet_sel - 1 : ui.pick_deg);
        for (s = 0; s < 16; s++) {
            int v = step_val(s);
            if (v == 2)
                kk |= 1u << WHITE_K[s];
            else if (v)
                dk |= 1u << WHITE_K[s];
        }
        if (zz_view.playing && step >= 0) {
            uint32_t bit = 1u << WHITE_K[step & 15];
            dk &= ~bit;
            kk ^= bit;
        }
        if (b >= 0 && b < 11)
            kk |= 1u << BLACK_K[b];
    } else {
        kk = ui.keys;
        if (zz_view.playing && step >= 0)
            dk |= 1u << WHITE_K[step & 15];
    }
    if (bb != ui.leds[0] || kk != ui.leds[1] || db != ui.leds[2] || dk != ui.leds[3]) {
        ui.leds[0] = bb;
        ui.leds[1] = kk;
        ui.leds[2] = db;
        ui.leds[3] = dk;
        plat_lights(bb, kk, db, dk, 1);
    }
}

/* ------------------------------------------------------------------ the contract */
void app_init(void)
{
    zz_draw_init();
    zz_init();
    song_load();
    zz_view.drum = (const int8_t(*)[16])song.drum;
    zz_view.bz = song.bz;
    zz_view.syn_a = song.syn_a;
    zz_view.bas_a = song.bas_a;
    zz_view.cur_step = -1;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float L[APP_BLOCK], R[APP_BLOCK], g_now;
    float g = (float)master_q12 * (8300000.0f / 4096.0f);
    uint32_t i;
    if (n > APP_BLOCK)
        n = APP_BLOCK;
    zz_render(L, R, n);
    for (i = 0; i < n; i++) {
        g_now += (g - g_now) * 0.004f;            /* ~5 ms: turning MASTER does not click */
        lr[2 * i] = (int32_t)(L[i] * g_now);
        lr[2 * i + 1] = (int32_t)(R[i] * g_now);
    }
}

void app_panic(void)
{
    zz_seq(0);
    zz_panic();
    zz_view.playing = 0;
}

void app_overload(void)                          /* the render ran late: lighter until it has been fine a while */
{
    if (ui.shed < 2)
        zz_shed(++ui.shed);
    ui.over_t = plat_ms();
}

void app_splash(void)                            /* (before app_init) */
{
    zz_view.splash = 1;
    draw_now();
    zz_view.splash = 0;
}

void app_ui_init(void)                           /* (again after an update over USB that did not happen) */
{
    if (ui.shown)
        return;
    ui.shown = 1;
    zz_view.note_ms = plat_ms() - 9000u;         /* the big ZINZIN until something plays */
    zz_draw_start();
}

void app_input_only(void)                        /* the main loop's time between frames: the screen's too */
{
    input();
    events();
    draw_service(3000u);
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    events();
    if (ui.shed && now - ui.over_t > 15000u) {
        zz_shed(--ui.shed);
        ui.over_t = now;
    }
    draw_service(9000u);
    leds();
}
