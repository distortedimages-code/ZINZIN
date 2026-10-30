/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* ZINZIN: the engine alone, on a computer (the C library's maths: -DZZ_LIBM). The same runs as the browser
 * proto's test: levels, no NaN, every bêtise, the RAM it takes; and a WAV of each run to listen to.
 *
 *   cc -O2 -DZZ_LIBM -Izinzin/gen -Izinzin/dsp tests/engine_test.c zinzin/dsp/zz_engine.c -lm -o build/engine_test
 *   build/engine_test [out_dir]        exit 0 = every check passed */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "zz_engine.h"

static const int8_t DRUM[4][16] = {{2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 1, 1, 0, 0, 0},
                                   {0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 1},
                                   {1, 0, 1, 0, 1, 0, 2, 0, 1, 0, 1, 0, 1, 0, 2, 0},
                                   {0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0}};
static const int8_t SYN_N[16] = {57, -1, -1, 64, -1, -1, 69, -1, 57, -1, -1, 72, -1, 67, -1, -1};
static const int8_t SYN_A[16] = {1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0};
static const int8_t BAS_N[16] = {33, -1, 33, -1, 36, -1, -1, 33, 33, -1, 40, -1, 38, -1, 36, -1};
static const int8_t BAS_A[16] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
static const int8_t BZ[16] = {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 2, 0, 4};
static const float T0[4][5] = {{0.55f, 0.25f, 0.0f, 0.30f, 0.80f},
                               {0.35f, 0.30f, 0.0f, 0.40f, 0.65f},
                               {0.60f, 0.35f, 0.0f, 0.25f, 0.75f},
                               {0.50f, 0.40f, 0.0f, 0.30f, 0.75f}};

static const char *outdir = "build";
static int fails;

static void reset(void)
{
    int t, k;
    zz_seq(0);
    zz_panic();
    for (t = 0; t < 4; t++) {
        for (k = 0; k < 5; k++)
            zz_T[t][k] = T0[t][k];
        zz_mute[t] = 0;
    }
    zz_G[ZZ_HUMEUR] = 0.12f;
    zz_G[ZZ_APERO] = 0.15f;
    zz_G[ZZ_CAFEINE] = 0.45f;
    memcpy(zz_pat.drum, DRUM, sizeof DRUM);
    memcpy(zz_pat.syn_n, SYN_N, 16);
    memcpy(zz_pat.syn_a, SYN_A, 16);
    memcpy(zz_pat.bas_n, BAS_N, 16);
    memcpy(zz_pat.bas_a, BAS_A, 16);
    memcpy(zz_pat.bz, BZ, 16);
}

static void wav(const char *name, const short *pcm, unsigned frames)
{
    char path[256];
    FILE *f;
    unsigned bytes = frames * 4u, v;
    snprintf(path, sizeof path, "%s/%s.wav", outdir, name);
    if (!(f = fopen(path, "wb")))
        return;
    fwrite("RIFF", 1, 4, f);
    v = 36u + bytes;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16;
    fwrite(&v, 4, 1, f);
    {
        unsigned short fmt[2] = {1, 2};
        unsigned rate = 44100, brate = 44100 * 4;
        unsigned short al[2] = {4, 16};
        fwrite(fmt, 2, 2, f);
        fwrite(&rate, 4, 1, f);
        fwrite(&brate, 4, 1, f);
        fwrite(al, 2, 2, f);
    }
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    fwrite(pcm, 4, frames, f);
    fclose(f);
}

static short pcm[44100 * 8 * 2];

/* render secs seconds with the sequencer on; the levels; a WAV */
static void run(const char *label, const char *file, float secs, float max_peak)
{
    float L[256], R[256], pk = 0.0f;
    double ss = 0.0;
    unsigned n = (unsigned)(44100.0f * secs), i, j, nan = 0, ev[5] = {0};
    zz_ev_t e;
    clock_t t0 = clock();
    double ms;
    zz_seq(1);
    for (i = 0; i < n; i += 256) {
        zz_render(L, R, 256);
        for (j = 0; j < 256 && i + j < n; j++) {
            float v = L[j], w = R[j];
            if (!isfinite(v) || !isfinite(w))
                nan++;
            if (fabsf(v) > pk)
                pk = fabsf(v);
            if (fabsf(w) > pk)
                pk = fabsf(w);
            ss += (double)v * v;
            pcm[2 * (i + j)] = (short)lrintf(fmaxf(-1.0f, fminf(1.0f, v)) * 32767.0f);
            pcm[2 * (i + j) + 1] = (short)lrintf(fmaxf(-1.0f, fminf(1.0f, w)) * 32767.0f);
        }
        while (zz_ev_get(&e))
            if (e.t < 5)
                ev[e.t]++;
    }
    ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    printf("%-22s peak %.2f rms %.3f nan %u  hits %u bet %u steps %u  host %.1f%%\n", label, pk, sqrt(ss / n), nan,
           ev[ZZ_EV_HIT], ev[ZZ_EV_BET], ev[ZZ_EV_STEP], ms / (secs * 10.0));
    if (nan || pk > max_peak || pk < 0.01f) {
        printf("  FAIL: %s\n", nan ? "NaN" : (pk < 0.01f ? "silent" : "too loud"));
        fails++;
    }
    if (file)
        wav(file, pcm, n);
    zz_seq(0);
}

int main(int argc, char **argv)
{
    int t, k;
    if (argc > 1)
        outdir = argv[1];
    zz_init();
    reset();
    run("defaut", "01_defaut", 6.0f, 1.0f);
    reset();
    zz_mute[1] = zz_mute[2] = zz_mute[3] = 1;
    run("batterie seule", "02_boum", 3.0f, 1.0f);
    reset();
    zz_mute[0] = zz_mute[1] = zz_mute[3] = 1;
    run("basse seule", "03_mammouth", 3.0f, 1.0f);
    reset();
    zz_mute[0] = zz_mute[2] = zz_mute[3] = 1;
    run("matou seul", "04_matou", 3.0f, 1.0f);
    reset();
    zz_T[0][0] = zz_T[0][1] = 0.0f, zz_T[0][3] = 0.2f;
    zz_T[1][0] = zz_T[1][1] = 0.0f, zz_T[1][3] = 0.6f;
    zz_T[2][0] = zz_T[2][1] = 0.0f;
    zz_T[3][0] = 0.0f, zz_T[3][3] = 0.5f;
    zz_G[ZZ_APERO] = 0.0f;
    run("tout en finesse", "05_finesse", 6.0f, 1.0f);
    reset();
    for (t = 0; t < 4; t++)
        zz_T[t][0] = 1.0f;
    zz_T[0][1] = zz_T[1][1] = zz_T[2][1] = 0.6f;
    zz_G[ZZ_HUMEUR] = 0.95f;
    run("tout en masse doom", "06_masse_doom", 6.0f, 1.0f);
    reset();
    for (t = 0; t < 4; t++)
        for (k = 0; k < 5; k++)
            zz_T[t][k] = 1.0f;
    zz_G[ZZ_HUMEUR] = zz_G[ZZ_APERO] = zz_G[ZZ_CAFEINE] = 1.0f;
    run("tout a fond", "07_a_fond", 6.0f, 1.0f);
    reset();
    for (t = 0; t < 4; t++)
        for (k = 0; k < 5; k++)
            zz_T[t][k] = k == 4 ? 0.5f : 0.0f;
    zz_G[ZZ_HUMEUR] = zz_G[ZZ_APERO] = zz_G[ZZ_CAFEINE] = 0.0f;
    run("tout a zero", "08_a_zero", 4.0f, 1.0f);
    for (k = 1; k <= 6; k++) {
        static const char *const NAME[7] = {"", "begaie", "recule", "fige", "coule", "plonge", "ecrase"};
        char f[32];
        reset();
        memset(zz_pat.bz, 0, 16);
        zz_pat.bz[0] = zz_pat.bz[8] = (int8_t)k;
        snprintf(f, sizeof f, "1%d_%s", k, NAME[k]);
        run(f + 3, f, 3.0f, 1.0f);
    }
    for (k = 0; k < 7; k++) {   /* every mood */
        char f[32];
        reset();
        zz_G[ZZ_HUMEUR] = ((float)k + 0.5f) / 7.0f;
        snprintf(f, sizeof f, "2%d_humeur%d", k, k);
        run(f + 3, f, 3.0f, 1.0f);
    }
    printf("RAM du son : %.1f Ko\n", zz_ram_bytes() / 1024.0);
    printf(fails ? "%d FAIL\n" : "OK\n", fails);
    return fails != 0;
}
