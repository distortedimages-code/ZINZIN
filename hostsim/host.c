/* ZINZIN's own test bench: the whole app (app.c and the engine) on a computer, driven by a script of presses and
 * turns on a simulated clock. It writes the screen as pictures (build/host_out/ *.ppm, and the frames of a few clips) and
 * the sound as a WAV, and checks a few things on the way (no NaN, the song saved and loaded back, levels).
 *
 *   cc -O2 -DZZ_LIBM -Ihostsim -Izinzin -Izinzin/gen -Izinzin/dsp hostsim/host.c zinzin/zz_draw.c zinzin/dsp/zz_engine.c \
 *      -lm -o build/host
 *   build/host build/host_out [film]  then tools/host_pics.py build/host_out (film: the first 95 s of the screen as
 *                                      raw frames, film.rgb, 12.5 a second, for tools/host_film.sh)
 *
 * This is not fm1-base's simulator (base/host/plat_host.c), which runs the app with the real platform code; it only
 * stands in for the few calls ZINZIN makes, so the app can be looked at and listened to here. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "plat.h"
#include "app.h"

/* ------------------------------------------------------------------ the platform, simulated */
static uint32_t sim_ms, sim_btn, sim_keys, sim_lights[4];
static int32_t sim_enc[NE];
static uint16_t fb[240 * 240];
static uint16_t cv_px[240 * 80];
static uint32_t frames_done;
static const char *outdir = "build/host_out";

uint32_t plat_ms(void) { return sim_ms; }
uint32_t plat_buttons(void) { return sim_btn; }
uint32_t plat_keys(void) { return sim_keys; }
int32_t plat_enc(int role)
{
    int32_t e = sim_enc[role];
    sim_enc[role] = 0;
    return e;
}
uint32_t plat_master(void) { return 4096u; }
void plat_lights(uint32_t buttons, uint32_t keys, uint32_t dim_buttons, uint32_t dim_keys, int glow)
{
    (void)glow;
    sim_lights[0] = buttons;
    sim_lights[1] = keys;
    sim_lights[2] = dim_buttons;
    sim_lights[3] = dim_keys;
}
static uint32_t midi_q[16];
static int midi_n;
int plat_midi_in(uint32_t *pkt, int *port)
{
    int i;
    if (!midi_n)
        return 0;
    *pkt = midi_q[0];
    if (port)
        *port = 0;
    for (i = 1; i < midi_n; i++)
        midi_q[i - 1] = midi_q[i];
    midi_n--;
    return 1;
}
static void midi_note(int n, int v)
{
    if (midi_n < 16)
        midi_q[midi_n++] = 0x09u | 0x90u << 8 | (uint32_t)n << 16 | (uint32_t)v << 24;
}
uint32_t plat_ticks24(void) { return sim_ms * 24000u; }   /* frozen during a call: a frame is drawn whole */
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    char p[256];
    FILE *f;
    int n;
    snprintf(p, sizeof p, "%s/store_%u.bin", outdir, obj);
    if (!(f = fopen(p, "rb")))
        return -1;
    n = (int)fread(dst, 1, max, f);
    fclose(f);
    return n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    char p[256];
    FILE *f;
    snprintf(p, sizeof p, "%s/store_%u.bin", outdir, obj);
    if (!(f = fopen(p, "wb")))
        return -1;
    fwrite(src, 1, len, f);
    fclose(f);
    return 0;
}
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t r, c;
    for (r = 0; r < h; r++)
        for (c = 0; c < w; c++) {
            uint16_t v = px[r * w + c];
            if (y + r < 240 && x + c < 240)
                fb[(y + r) * 240 + x + c] = (uint16_t)((v >> 8) | (v << 8));
        }
    if (y + h >= 240)
        frames_done++;
}

#include "app.c"

/* ------------------------------------------------------------------ outputs */
static FILE *film;                               /* every 80 ms of the session, the screen (a video's frames) */
static void film_frame(void)
{
    int i;
    for (i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        unsigned char rgb[3] = {(unsigned char)(((c >> 11) << 3) | (c >> 13)), (unsigned char)((((c >> 5) & 63) << 2) | ((c >> 9) & 3)),
                                (unsigned char)(((c & 31) << 3) | ((c >> 2) & 7))};
        fwrite(rgb, 1, 3, film);
    }
}
static void ppm(const char *name)
{
    char p[256];
    FILE *f;
    int i;
    snprintf(p, sizeof p, "%s/%s.ppm", outdir, name);
    if (!(f = fopen(p, "wb")))
        return;
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        unsigned char rgb[3] = {(unsigned char)(((c >> 11) << 3) | (c >> 13)), (unsigned char)((((c >> 5) & 63) << 2) | ((c >> 9) & 3)),
                                (unsigned char)(((c & 31) << 3) | ((c >> 2) & 7))};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static FILE *wav;
static uint32_t wav_frames;
static double ss, peak;
static uint32_t nan_count;
static void wav_open(void)
{
    char p[256];
    snprintf(p, sizeof p, "%s/session.wav", outdir);
    wav = fopen(p, "wb");
    if (wav)
        fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x44\xAC\0\0\x10\xB1\x02\0\x04\0\x10\0data\0\0\0\0", 1, 44, wav);
}
static void wav_close(void)
{
    uint32_t b = wav_frames * 4u, r = 36u + b;
    if (!wav)
        return;
    fseek(wav, 4, SEEK_SET);
    fwrite(&r, 4, 1, wav);
    fseek(wav, 40, SEEK_SET);
    fwrite(&b, 4, 1, wav);
    fclose(wav);
}

/* ------------------------------------------------------------------ the simulated main loop */
static uint32_t next_frame, audio_acc, clip_until, clip_n, last_clip_frame;
static char clip_name[32];
static clock_t frame_clock;

static void tick(void)
{
    static int32_t lr[2 * APP_BLOCK];
    sim_ms++;
    audio_acc += 44100u;
    while (audio_acc >= 1000u * APP_BLOCK) {      /* a block every 5.8 ms */
        uint32_t i;
        audio_acc -= 1000u * APP_BLOCK;
        app_render(lr, APP_BLOCK, 4096u);
        for (i = 0; i < 2u * APP_BLOCK; i++) {
            double v = (double)lr[i] / 8300000.0;
            short s;
            if (v != v)
                nan_count++;
            if (fabs(v) > peak)
                peak = fabs(v);
            ss += v * v;
            v = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v);
            s = (short)lrint(v * 32767.0);
            if (wav)
                fwrite(&s, 2, 1, wav);
        }
        wav_frames += APP_BLOCK;
    }
    if (sim_ms >= next_frame) {
        clock_t c0 = clock();
        next_frame = sim_ms + 16u;
        app_frame();
        frame_clock += clock() - c0;
    } else if (sim_ms % 4u == 0u)
        app_input_only();
    if (film && sim_ms % 80u == 0u) {
        if (sim_ms <= 95000u)
            film_frame();
        else {
            fclose(film);
            film = 0;
        }
    }
    if (clip_until && frames_done != last_clip_frame) {   /* a clip: every new frame */
        char n[64];
        last_clip_frame = frames_done;
        snprintf(n, sizeof n, "clip_%s_%03u", clip_name, clip_n++);
        ppm(n);
        if (sim_ms >= clip_until)
            clip_until = 0;
    }
}
static void h_run(uint32_t ms)
{
    while (ms--)
        tick();
}
static void h_press(int b)
{
    sim_btn |= 1u << b;
    h_run(40);
    sim_btn &= ~(1u << b);
    h_run(40);
}
static void h_turn(int e, int n)
{
    sim_enc[e] += n;
    h_run(30);
}
static void h_key(int k)
{
    sim_keys |= 1u << k;
    h_run(60);
    sim_keys &= ~(1u << k);
    h_run(60);
}
static void h_shot(const char *name)
{
    uint32_t f = frames_done;
    while (frames_done == f)                     /* the next whole frame */
        tick();
    ppm(name);
    printf("  %-26s humeur %.2f  bpm %.0f  pops %d  flyers %d\n", name, zz_EG[ZZ_HUMEUR], zz_bpm(), zz_draw_count(0), zz_draw_count(1));
}
static void h_clip(const char *name, uint32_t ms)
{
    snprintf(clip_name, sizeof clip_name, "%s", name);
    clip_n = 0;
    last_clip_frame = frames_done;
    clip_until = sim_ms + ms;
    h_run(ms + 100);
}
/* HUMEUR to the middle of mood m (one detent: 1/48) */
static void to_mood(int m)
{
    float target = ((float)m + 0.5f) / 7.0f;
    int n = (int)((target - zz_G[ZZ_HUMEUR]) * 48.0f + (target > zz_G[ZZ_HUMEUR] ? 0.5f : -0.5f));
    while (n) {
        int d = n > 1 ? 1 : (n < -1 ? -1 : n);
        h_turn(EN_ALGO, d);
        n -= d;
    }
}
static void h_knob(int e, float target, volatile float *v)
{
    int n = (int)((target - *v) * 48.0f + (target > *v ? 0.5f : -0.5f));
    while (n) {
        int d = n > 0 ? 1 : -1;
        h_turn(e, d);
        n -= d;
    }
}

int main(int argc, char **argv)
{
    int m, k, fails = 0;
    char name[64];
    if (argc > 1)
        outdir = argv[1];
    if (argc > 2 && !strcmp(argv[2], "film")) {
        char p[300];
        snprintf(p, sizeof p, "%s/film.rgb", outdir);
        film = fopen(p, "wb");
    }
    {
        char p[300];
        snprintf(p, sizeof p, "%s/store_0.bin", outdir);
        remove(p);
    }
    wav_open();
    printf("ZINZIN on the bench (RAM du son %.1f Ko, canvas %u o)\n", zz_ram_bytes() / 1024.0,
           (unsigned)sizeof cv_px);
    app_init();
    app_splash();
    ppm("00_splash");
    app_ui_init();
    h_run(1500);
    h_shot("01_accueil");
    h_press(B_PLAY);
    h_run(2500);
    h_shot("02_joue_euphorie");
    h_clip("euphorie", 3000);
    for (m = 1; m < 7; m++) {
        to_mood(m);
        h_run(2600);
        snprintf(name, sizeof name, "03_humeur_%d", m);
        h_shot(name);
        if (m == 3)
            h_clip("reverie", 3000);
        if (m == 5)
            h_clip("colere", 3000);
        if (m == 6)
            h_clip("deprime", 3000);
    }
    to_mood(3);
    h_run(1000);
    /* MATOU: the cat swells, then Warhol */
    h_press(B_SEL);
    h_knob(EN_K1, 0.85f, &zz_T[1][ZZ_BIDOCHE]);
    h_run(1500);
    h_shot("04_matou_bidoche");
    h_knob(EN_K1, 1.0f, &zz_T[1][ZZ_BIDOCHE]);
    h_run(1500);
    h_shot("05_matou_warhol");
    h_knob(EN_K1, 0.35f, &zz_T[1][ZZ_BIDOCHE]);
    h_knob(EN_K2, 0.9f, &zz_T[1][ZZ_CHATOUILLE]);
    h_run(1500);
    h_shot("06_matou_chatouille");
    h_knob(EN_K2, 0.3f, &zz_T[1][ZZ_CHATOUILLE]);
    /* BOUM in PAS mode */
    h_press(B_FX);
    h_press(B_EDIT);
    h_key(BLACK_K[0]);
    h_key(WHITE_K[3]);
    h_run(800);
    h_shot("07_boum_pas");
    h_press(B_EDIT);
    h_knob(EN_K2, 0.8f, &zz_T[0][ZZ_CHATOUILLE]);
    h_run(1200);
    h_shot("08_boum_kaleidoscope");
    h_knob(EN_K2, 0.25f, &zz_T[0][ZZ_CHATOUILLE]);
    /* MAMMOUTH: the herd */
    h_press(B_ENV);
    h_knob(EN_K1, 0.95f, &zz_T[2][ZZ_BIDOCHE]);
    h_knob(EN_K2, 0.8f, &zz_T[2][ZZ_CHATOUILLE]);
    h_run(1200);
    h_shot("09_mammouth");
    /* BAZAR: each bêtise from the keys */
    h_press(B_LFO);
    for (k = 0; k < 6; k++) {
        static const char *const BN[6] = {"begaie", "recule", "fige", "coule", "plonge", "ecrase"};
        h_key(WHITE_K[k]);
        h_run(150);
        snprintf(name, sizeof name, "10_betise_%d_%s", k + 1, BN[k]);
        h_shot(name);
        h_run(1200);
    }
    /* BAZAR MOISI: rot */
    h_knob(EN_K3, 0.9f, &zz_T[3][ZZ_MOISI]);
    h_run(1500);
    h_shot("11_bazar_moisi");
    h_clip("moisi", 2500);
    h_knob(EN_K3, 0.0f, &zz_T[3][ZZ_MOISI]);
    /* APÉRO: everything drifts */
    h_knob(EN_PRESET, 0.9f, &zz_G[ZZ_APERO]);
    h_run(2000);
    h_shot("12_apero");
    h_clip("apero", 2500);
    h_knob(EN_PRESET, 0.15f, &zz_G[ZZ_APERO]);
    /* ARP + K2: MATOU's level; a knob's window */
    sim_btn |= 1u << B_ARP;
    h_turn(EN_K2, -3);
    sim_btn &= ~(1u << B_ARP);
    h_run(200);
    h_shot("13_boucan");
    h_turn(EN_SELECT, 4);
    h_run(200);
    h_shot("14_cafeine");
    /* SAVE, then load it back */
    h_press(B_SAVE);
    h_run(200);
    h_shot("15_sauve");
    {
        song_t before = song;
        song_load();
        if (memcmp(&before, &song, sizeof song)) {
            printf("FAIL: the song loaded back differs\n");
            fails++;
        }
    }
    h_press(B_PLAY);
    h_run(9500);
    h_shot("16_pause");
    printf("une image en moyenne : %.2f ms ici (%u images)\n", (double)frame_clock * 1000.0 / CLOCKS_PER_SEC / frames_done,
           frames_done);
    /* the rest of the panel: REC on MATOU, octaves, PAS on the melodies and BAZAR, mutes, MIDI, all at random */
    h_press(B_PLAY);
    h_press(B_SEL);
    h_press(B_REC);
    for (k = 0; k < 8; k++) {
        h_key(WHITE_K[k * 2]);
        h_run(90);
    }
    h_press(B_OCTUP);
    h_press(B_OCTUP);
    h_press(B_OCTUP);
    midi_note(60, 120);
    midi_note(64, 60);
    h_run(300);
    h_press(B_REC);
    h_press(B_EDIT);
    h_key(BLACK_K[4]);
    for (k = 0; k < 16; k += 3)
        h_key(WHITE_K[k]);
    h_press(B_LFO);
    h_key(BLACK_K[2]);
    h_key(WHITE_K[1]);
    h_key(WHITE_K[1]);
    h_press(B_EDIT);
    h_press(B_HOME);
    h_press(B_SEQ);
    h_press(B_OCTDN);
    h_run(1500);
    h_shot("17_reste");
    {
        uint32_t r = 12345u, t;
        for (t = 0; t < 2000; t++) {             /* the monkey: 2000 random gestures */
            uint32_t what;
            r = r * 1103515245u + 12345u;
            what = (r >> 16) % 10u;
            if (what < 3)
                h_turn((int)((r >> 8) % NE), (int)((r >> 20) % 13u) - 6);
            else if (what < 6)
                h_key((int)((r >> 4) % NKEYS));
            else if (what < 8) {
                int b = (int)((r >> 12) % NB);
                if (b != B_SAVE)
                    h_press(b);
            } else if (what < 9) {
                sim_btn |= 1u << B_ARP;
                h_turn(EN_K1 + (int)((r >> 6) % 4u), (int)((r >> 22) % 9u) - 4);
                sim_btn &= ~(1u << B_ARP);
            } else
                midi_note((int)((r >> 9) % 128u), 1 + (int)((r >> 3) % 127u));
        }
        h_run(500);
        h_shot("18_singe");
    }
    /* the cost of a frame, busy: two worlds, everything flying, rot */
    h_press(B_PLAY);
    zz_G[ZZ_HUMEUR] = 0.62f;
    zz_T[3][ZZ_MOISI] = 0.4f;
    zz_G[ZZ_APERO] = 0.5f;
    h_run(3000);
    {
        clock_t c0 = clock();
        int i;
        double ms;
        for (i = 0; i < 200; i++)
            draw_now();
        ms = (double)(clock() - c0) * 1000.0 / CLOCKS_PER_SEC / 200.0;
        printf("une image : %.2f ms ici\n", ms);
    }
    {
        static float L[256], R[256];
        clock_t c0 = clock();
        int i;
        double ms;
        for (i = 0; i < 44100 * 10 / 256; i++)
            zz_render(L, R, 256);
        ms = (double)(clock() - c0) * 1000.0 / CLOCKS_PER_SEC / 10.0;
        printf("le son : %.1f ms par seconde ici (%.1f %%)\n", ms, ms / 10.0);
    }
    wav_close();
    printf("son : peak %.2f rms %.3f nan %u, %.1f s\n", peak, sqrt(ss / (wav_frames * 2.0 + 1.0)), nan_count,
           wav_frames / 44100.0);
    if (nan_count || peak > 1.0 || peak < 0.05) {
        printf("FAIL: the sound\n");
        fails++;
    }
    printf(fails ? "%d FAIL\n" : "OK\n", fails);
    return fails != 0;
}
