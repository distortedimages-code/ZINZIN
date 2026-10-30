/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* ZINZIN's sound: four tracks (BOUM the drums, MATOU the synth, MAMMOUTH the bass, BAZAR the bêtises on the whole
 * mix), a 16-step sequencer, the shared space (echoes, an empty room), the tape rot and the glue.
 *
 * Threads: the UI (main loop) writes the macros, the mutes and the pattern and queues notes with zz_play; the audio
 * interrupt runs zz_render and leaves events (hits, bêtises, steps, beats) for the UI in a ring. Every value is a
 * word, written whole: a value read half-way through a change is only a step early or late. */
#pragma once
#include <stdint.h>

#define ZZ_SR 44100.0f
enum { ZZ_BOUM, ZZ_MATOU, ZZ_MAMMOUTH, ZZ_BAZAR, ZZ_NTRK };
enum { ZZ_BIDOCHE, ZZ_CHATOUILLE, ZZ_MOISI, ZZ_FLEMME, ZZ_BOUCAN, ZZ_NTK };   /* per track */
enum { ZZ_HUMEUR, ZZ_APERO, ZZ_CAFEINE, ZZ_NG };                             /* for everyone */
enum { ZZ_BEGAIE = 1, ZZ_RECULE, ZZ_FIGE, ZZ_COULE, ZZ_PLONGE, ZZ_ECRASE };    /* the bêtises */

typedef struct {
    int8_t drum[4][16];        /* 0 off, 1 on, 2 accent; lanes BOUM CLAC TSS BING */
    int8_t syn_n[16], syn_a[16];   /* MIDI note or -1, accent */
    int8_t bas_n[16], bas_a[16];
    int8_t bz[16];             /* 0 or a bêtise */
} zz_pat_t;

extern volatile float zz_T[ZZ_NTRK][ZZ_NTK];   /* the knobs, 0..1 */
extern volatile float zz_G[ZZ_NG];
extern volatile float zz_ET[ZZ_NTRK][ZZ_NTK];  /* what they really are once APÉRO has moved them (the screen) */
extern volatile float zz_EG[ZZ_NG];
extern volatile uint8_t zz_mute[ZZ_NTRK];
extern zz_pat_t zz_pat;

enum { ZZ_EV_HIT = 1, ZZ_EV_BET, ZZ_EV_STEP, ZZ_EV_BEAT };
typedef struct { uint8_t t, tr, a, b; } zz_ev_t;   /* HIT: track, lane or note, accent; BET: kind, ms/10; STEP: step */

void zz_init(void);
void zz_render(float *l, float *r, uint32_t n);     /* the audio interrupt */
void zz_panic(void);
void zz_seq(int on);                                /* ZOU */
int zz_seq_on(void);
void zz_play(int tr, int what, float vel);          /* a key: a lane (BOUM), a note (MATOU, MAMMOUTH), a bêtise (BAZAR) */
int zz_ev_get(zz_ev_t *e);                          /* the UI: the next event, 0 none */
float zz_bpm(void);
uint32_t zz_ram_bytes(void);
void zz_shed(int level);                            /* 0 all of it; 1, 2: lighter (the render ran late) */
