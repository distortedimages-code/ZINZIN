/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* ZINZIN's screen (zz_draw.c, a unit of its own at -O2): the scene and its rows. The app (app.c) owns the panel, the
 * song and the panel's canvas; it tells the screen what it shows through zz_view, starts a frame with zz_frame_start
 * and fetches its rows band by band (zz_rows), which it sends with lcd_blit. All of it runs in the main loop. */
#pragma once
#include <stdint.h>

typedef struct {
    int sel, step_mode, lane, rec, playing, cur_step, splash;   /* the track picked, PAS, its lane, ENREGISTRE... */
    int debug, fps10, cpu;                                      /* ARP + SAVE: frames a second (x10) and the sound's load */
    uint32_t beat_ms, beat_n, note_ms;
    const int8_t (*drum)[16];                                   /* the song's patterns, for the step strip */
    const int8_t *bz, *syn_a, *bas_a;
} zz_view_t;
extern zz_view_t zz_view;

void zz_draw_init(void);                  /* the palettes; any time before the first frame */
void zz_draw_start(void);                 /* the first screen after the splash: the fire lit, a first pop-up */
int zz_frame_start(void);                 /* a new frame: 1 = draw it (0: FIGE or BÉGAIE keep the last one) */
void zz_rows(int y0, int n, uint16_t *dst);   /* rows y0.. of the frame, byte-swapped for the panel */

/* what happened (the sounds, the keys): shown from the next frame */
void zz_vis_hit(int tr, int v, int acc);  /* BOUM: v the lane; MATOU, MAMMOUTH: v the note */
void zz_vis_bet(int k, int ms);
void zz_vis_pop(void);
/* windows: a knob turned (k 0..4 the track's macros, 5 HUMEUR, 6 APÉRO, 7 CAFÉINE; tr the track or -1), a message */
void zz_show_knob(int k, int tr, float v);
void zz_show_say(const char *title, const char *text);
void zz_value_text(int k, float v, char *t);   /* a knob's value in words (28 bytes) */
int zz_draw_count(int what);              /* (the test bench) 0 pop-ups, 1 flyers */
