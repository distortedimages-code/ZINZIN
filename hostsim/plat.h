/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* ZINZIN's own test bench: the part of fm1-base's platform interface (base/include/plat.h) that ZINZIN uses, with
 * the same names and values, so app.c builds on a computer without the platform. The firmware itself is built
 * against the real plat.h. */
#pragma once
#include <stdint.h>

#define PLAT_POOL

enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
#define NKEYS 27
#define PLAT_WHITE_KEYS {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26}
#define PLAT_BLACK_KEYS {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25}
#define LED_PLAY_GREEN (1u << NB)
#define PLAT_TICKS_PER_US 24u

uint32_t plat_ms(void);
uint32_t plat_buttons(void);
uint32_t plat_keys(void);
int32_t plat_enc(int role);
uint32_t plat_master(void);
void plat_lights(uint32_t buttons, uint32_t keys, uint32_t dim_buttons, uint32_t dim_keys, int glow);
int plat_midi_in(uint32_t *pkt, int *port);
uint32_t plat_ticks24(void);
uint32_t plat_cpu_pct(void);
int plat_store_load(uint32_t obj, void *dst, uint32_t max);
int plat_store_save(uint32_t obj, const void *src, uint32_t len);
