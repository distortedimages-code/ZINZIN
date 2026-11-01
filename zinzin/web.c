/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* ZINZIN in the browser: what the page shows beside the screen (where the knobs are, the track picked, the mood,
 * the tempo), read by web/emu/app_worklet.js about 15 times a second. fm1-base's emulator (base/web/emu/emu_web.c)
 * includes this file at its end (app.h APP_WEB), after the whole app, so app.c's own state is in scope; the
 * firmware never sees it. */
enum { WS_T = 0, WS_G = 20, WS_SEL = 23, WS_STEP, WS_LANE, WS_BET, WS_DEG, WS_PLAY, WS_REC, WS_OCT, WS_MOOD, WS_BPM,
       WS_MUTE, WS_CUR = WS_MUTE + 4, WS_N };
static float web_state[WS_N];

__attribute__((used, visibility("default"))) float *zz_web_state(void)
{
    float *s = web_state;
    int t, k;
    for (t = 0; t < ZZ_NTRK; t++)                /* BIDOCHE CHATOUILLE MOISI FLEMME BOUCAN of each track, 0..1 */
        for (k = 0; k < ZZ_NTK; k++)
            s[WS_T + t * ZZ_NTK + k] = zz_T[t][k];
    for (k = 0; k < ZZ_NG; k++)                  /* HUMEUR APÉRO CAFÉINE */
        s[WS_G + k] = zz_G[k];
    s[WS_SEL] = (float)zz_view.sel;
    s[WS_STEP] = (float)zz_view.step_mode;
    s[WS_LANE] = (float)zz_view.lane;            /* PAS: the drum, the bêtise (1..6), the degree the white keys write */
    s[WS_BET] = (float)ui.bet_sel;
    s[WS_DEG] = (float)ui.pick_deg;
    s[WS_PLAY] = (float)zz_view.playing;
    s[WS_REC] = (float)zz_view.rec;
    s[WS_OCT] = (float)song.oct;
    s[WS_MOOD] = (float)mood();
    s[WS_BPM] = zz_bpm();
    for (t = 0; t < 4; t++)
        s[WS_MUTE + t] = (float)zz_mute[t];
    s[WS_CUR] = (float)zz_view.cur_step;
    return web_state;
}
__attribute__((used, visibility("default"))) uint32_t zz_web_state_n(void) { return WS_N; }
