/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* ZINZIN: a four-track groovebox for the M-VAVE FM-1 that looks like a personal web page from 1999.
 * This header is the app's half of the contract with fm1-base (base/device/unity.c, base/host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "ZINZIN"
#define APP_TAGLINE "meilleur rendu en 240x240"
#define APP_SPLASH_RGB RGB(255, 61, 139)
#define APP_BLOCK 256u                    /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_CANVAS_PX (240u * 80u)        /* gfx.c's canvas: our two 240 x 40 bands (one drawn while the other goes
                                           * out), and room for the platform's own screens */
#define APP_SPLASH                        /* our own first screen */
#define APP_OVERLOAD                      /* the render ran late: the engine sheds its unison copies */
#define APP_WEB                           /* in the browser emulator (tools/build.py --web): web.c's exports, for
                                           * the page beside the screen (web/emu/) */

/* the store: the song (knobs, patterns, mutes) and the panel's calibration (the platform's), at their flash offsets */
enum { OBJ_SONG, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xDA000u, 0xDC000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_splash(void);
void app_overload(void);
