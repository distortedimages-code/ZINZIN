/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* ZINZIN's own stand-in for fm1-base's browser emulator (base/web/emu/emu_web.c on base/host/plat_host.c): the
 * same exports, on the bench's few platform calls (hostsim/plat.h), built with clang for WebAssembly, so the page
 * (zinzin/web/emu/) can be tried on this computer without Emscripten:
 *
 *   make web            -> build/web/ (app.wasm, the page, fm1-base's worklet.js), then
 *   python3 -m http.server 8000 --directory build/web   and open http://localhost:8000/
 *
 * The version that is published is fm1-base's (tools/build.py zinzin --web, in the workflow), with the real
 * platform code. */
#include <stdint.h>
#include <string.h>
#include "plat.h"
#include "app.h"

#define EXPORT __attribute__((used, visibility("default")))
#define STORE_MAX 4096u

static uint32_t now_ms, held_btn, held_keys, lit_btn, lit_keys, dim_btn, dim_keys, master = 2800u, blits, store_writes;
static int32_t enc_acc[NE];
static uint16_t fb[240 * 240];
static uint16_t cv_px[APP_CANVAS_PX];
static uint8_t store[OBJ_NOBJ][STORE_MAX];
static int store_len[OBJ_NOBJ] = {-1, -1};

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int role)
{
    int32_t e = enc_acc[role];
    enc_acc[role] = 0;
    return e;
}
uint32_t plat_master(void) { return master; }
void plat_lights(uint32_t buttons, uint32_t keys, uint32_t dim_buttons, uint32_t dim_k, int glow)
{
    (void)glow;
    lit_btn = buttons;
    lit_keys = keys;
    dim_btn = dim_buttons;
    dim_keys = dim_k;
}
#define MQ 64u
static uint32_t min_q[MQ], mi_w, mi_r;
int plat_midi_in(uint32_t *pkt, int *port)
{
    if (mi_w == mi_r)
        return 0;
    *pkt = min_q[mi_r++ % MQ];
    if (port)
        *port = 0;
    return 1;
}
uint32_t plat_ticks24(void) { return now_ms * 24000u; }   /* still during a call, as the worklet's clock */
uint32_t plat_cpu_pct(void) { return 0; }
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    if (obj >= OBJ_NOBJ || store_len[obj] < 0)
        return -1;
    memcpy(dst, store[obj], (uint32_t)store_len[obj] < max ? (uint32_t)store_len[obj] : max);
    return store_len[obj];
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    if (obj >= OBJ_NOBJ || len > STORE_MAX)
        return -1;
    memcpy(store[obj], src, len);
    store_len[obj] = (int)len;
    store_writes++;
    return 0;
}
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    blits++;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = px[j * w + i];
}

#include "app.c"
#include "web.c"

/* the device's clock, a millisecond at a time: a block of sound when one is due, a frame every 16 ms */
#define RING 4096u
static float ring[RING * 2], out_l[1024], out_r[1024];
static uint32_t ring_w, ring_r, ui_due;
static double audio_due;
static void run_ms(uint32_t ms)
{
    static int32_t blk[2 * APP_BLOCK];
    while (ms--) {
        now_ms++;
        while (audio_due <= (double)now_ms) {
            uint32_t k, g = master * master >> 12;   /* fm1-base's APP_GAIN: the square */
            app_render(blk, APP_BLOCK, g);
            for (k = 0; k < APP_BLOCK; k++) {
                uint32_t i = (ring_w++ % RING) * 2u;
                ring[i] = (float)blk[2 * k] / 8388608.0f;
                ring[i + 1] = (float)blk[2 * k + 1] / 8388608.0f;
            }
            audio_due += APP_BLOCK * 1000.0 / 44100.0;
        }
        if (now_ms >= ui_due) {
            ui_due = now_ms + 16u;
            app_frame();
        } else
            app_input_only();
    }
}

EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }
EXPORT void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    while (ring_w - ring_r < n)
        run_ms(1);
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}
EXPORT void web_boot(void)
{
    app_init();
    memset(fb, 0, sizeof fb);
    app_ui_init();
}
EXPORT void web_buttons(uint32_t m) { held_btn = m; }
EXPORT void web_keys(uint32_t m) { held_keys = m; }
EXPORT void web_enc(int role, int32_t n)
{
    if (role >= 0 && role < NE)
        enc_acc[role] += n;
}
EXPORT void web_master(uint32_t v) { master = v > 4096u ? 4096u : v; }
EXPORT uint32_t web_lit_buttons(void) { return lit_btn; }
EXPORT uint32_t web_lit_keys(void) { return lit_keys; }
EXPORT uint32_t web_dim_buttons(void) { return dim_btn; }
EXPORT uint32_t web_dim_keys(void) { return dim_keys; }
EXPORT void web_midi_in(uint32_t port, uint32_t pkt)
{
    (void)port;
    if (mi_w - mi_r < MQ)
        min_q[mi_w++ % MQ] = pkt;
}
EXPORT uint16_t *web_fb(void) { return fb; }
EXPORT uint32_t web_blits(void) { return blits; }
EXPORT uint32_t web_now(void) { return now_ms; }
EXPORT uint32_t web_cpu(void) { return 0; }
EXPORT uint8_t *web_store(uint32_t obj) { return obj < OBJ_NOBJ ? store[obj] : 0; }
EXPORT int web_store_len(uint32_t obj) { return obj < OBJ_NOBJ ? store_len[obj] : -1; }
EXPORT void web_store_set_len(uint32_t obj, int n)
{
    if (obj < OBJ_NOBJ)
        store_len[obj] = n < 0 ? -1 : (n > (int)STORE_MAX ? (int)STORE_MAX : n);
}
EXPORT uint32_t web_store_writes(void) { return store_writes; }
EXPORT uint32_t web_nobj(void) { return OBJ_NOBJ; }
EXPORT uint32_t web_store_max(void) { return STORE_MAX; }
/* (fm1-base's screen reader and MIDI out: nothing here) */
static uint8_t said_none[1], sysex_in[64];
EXPORT uint8_t *web_said_buf(void) { return said_none; }
EXPORT uint32_t web_said_len(void) { return 0; }
EXPORT void web_said_clear(void) {}
EXPORT uint8_t *web_sysex_buf(void) { return sysex_in; }
EXPORT void web_sysex(uint32_t n) { (void)n; }
EXPORT void web_reader(int on) { (void)on; }
EXPORT uint32_t web_out_n(void) { return 0; }
