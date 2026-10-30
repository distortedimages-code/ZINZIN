/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* ZINZIN: the song. Melodies are kept as scale degrees, so HUMEUR recolours the same tune: seven moods from
 * euphoria (lydian) down to the low one (locrian), and the drums follow the mood until someone edits them.
 * Part of app.c's unity build (ur(), the panel's dice, is app.c's). */

typedef struct { const char *name; int8_t iv[7]; } mood_t;
static const mood_t MOODS[7] = {
    {"euphorie", {0, 2, 4, 6, 7, 9, 11}},
    {"joie", {0, 2, 4, 5, 7, 9, 11}},
    {"insouciance", {0, 2, 4, 5, 7, 9, 10}},
    {"rêverie", {0, 2, 3, 5, 7, 9, 10}},
    {"mélancolie", {0, 2, 3, 5, 7, 8, 10}},
    {"colère", {0, 1, 3, 5, 7, 8, 10}},
    {"déprime", {0, 1, 3, 5, 6, 8, 10}},
};
static const int8_t ROOT[3] = {0, 57, 33};       /* MATOU around A3, MAMMOUTH around A1 */
#define NODEG (-128)

/* what is kept (OBJ_SONG): the knobs, the mutes, the octave and the patterns, degrees for the melodies */
#define SONG_MAGIC 0x314E5A5Au                    /* "ZZN1" */
typedef struct {
    uint32_t magic;
    uint8_t T[4][5], G[3], mute[4];
    int8_t oct;
    uint8_t drum_edited;
    int8_t drum[4][16], syn_d[16], syn_a[16], bas_d[16], bas_a[16], bz[16];
} song_t;
static song_t song;
static uint8_t song_dirty;

static int mood_of(float h)
{
    int m = (int)(h * 7.0f);
    return m < 0 ? 0 : (m > 6 ? 6 : m);
}
static int mood(void) { return mood_of(zz_G[ZZ_HUMEUR]); }

static int deg_to_midi(int tr, int d)
{
    const int8_t *iv = MOODS[mood()].iv;
    int o = d >= 0 ? d / 7 : -((6 - d) / 7), k = d - o * 7;
    return ROOT[tr] + 12 * o + iv[k];
}
static int midi_to_deg(int tr, int n)
{
    int best = 0, bd = 1000, d;
    for (d = -14; d < 22; d++) {
        int dd = deg_to_midi(tr, d) - n;
        if (dd < 0)
            dd = -dd;
        if (dd < bd) {
            bd = dd;
            best = d;
        }
    }
    return best;
}
static int snap(int tr, int n) { return deg_to_midi(tr, midi_to_deg(tr, n)); }

/* the degrees, as notes in today's mood, to the engine */
static void render_notes(void)
{
    int s;
    for (s = 0; s < 16; s++) {
        zz_pat.syn_n[s] = (int8_t)(song.syn_d[s] == NODEG ? -1 : deg_to_midi(1, song.syn_d[s]));
        zz_pat.syn_a[s] = song.syn_a[s];
        zz_pat.bas_n[s] = (int8_t)(song.bas_d[s] == NODEG ? -1 : deg_to_midi(2, song.bas_d[s]));
        zz_pat.bas_a[s] = song.bas_a[s];
    }
}
static void drums_out(void)
{
    int l, s;
    for (l = 0; l < 4; l++)
        for (s = 0; s < 16; s++)
            zz_pat.drum[l][s] = song.drum[l][s];
    for (s = 0; s < 16; s++)
        zz_pat.bz[s] = song.bz[s];
}

static int in3(int s, int a, int b, int c) { return s == a || s == b || s == c; }

/* a beat for each mood: four on the floor in joy, half time in melancholy, almost nothing at the bottom */
static void gen_drums(int mi)
{
    int s;
    for (s = 0; s < 16; s++) {
        int k = 0, sn = 0, h = 0, b = 0;
        if (mi == 0) {
            k = s % 4 == 0 || (s == 14 && ur() < 0.6f);
            sn = (s == 4 || s == 12) ? 2 : (ur() < 0.12f);
            h = ur() < 0.9f ? (s % 2 ? 1 : 2) : 0;
            b = ur() < 0.25f ? (ur() < 0.4f ? 2 : 1) : 0;
        } else if (mi == 1) {
            k = s % 4 == 0;
            sn = (s == 4 || s == 12) ? 2 : 0;
            h = s % 2 == 0 ? (s % 4 == 2 ? 2 : 1) : (ur() < 0.2f);
            b = ur() < 0.1f;
        } else if (mi <= 3) {
            k = in3(s, 0, 6, 10) || (s == 13 && ur() < 0.5f);
            sn = (s == 4 || s == 12) ? 2 : (ur() < 0.06f);
            h = s % 4 == 2 ? 2 : (ur() < 0.35f);
            b = ur() < 0.14f;
        } else if (mi == 4) {
            k = s == 0 || s == 8 || (s == 11 && ur() < 0.4f);
            sn = s == 12 ? 2 : 0;
            h = ur() < 0.25f;
            b = ur() < 0.08f;
        } else if (mi == 5) {
            k = in3(s, 0, 3, 10) || s == 11 || ur() < 0.08f;
            sn = s == 8 ? 2 : (ur() < 0.1f);
            h = ur() < 0.6f ? (ur() < 0.3f ? 2 : 1) : 0;
            b = ur() < 0.12f ? 2 : 0;
        } else {
            k = s == 0 ? 2 : (s == 2);
            sn = s == 12 && ur() < 0.5f;
            h = 0;
            b = ur() < 0.05f;
        }
        song.drum[0][s] = (int8_t)(k && s == 0 && mi != 6 ? 2 : k);
        song.drum[1][s] = (int8_t)sn;
        song.drum[2][s] = (int8_t)h;
        song.drum[3][s] = (int8_t)b;
    }
    drums_out();
}

static int last_note[4] = {-1, 57, 33, -1};

/* AU PIF: a new pattern for one track, in the mood's manner */
static void gen_track(int tr)
{
    int mi = mood(), s;
    if (tr == 0) {
        gen_drums(mi);
        song.drum_edited = 0;
    } else if (tr == 1 || tr == 2) {
        static const float DENS[7] = {0.75f, 0.62f, 0.55f, 0.5f, 0.42f, 0.45f, 0.26f};
        int8_t *d = tr == 1 ? song.syn_d : song.bas_d, *a = tr == 1 ? song.syn_a : song.bas_a;
        int span = tr == 1 ? 12 : 8, prev = tr == 1 ? 6 : 3;
        for (s = 0; s < 16; s++) {
            int on = s % 4 == 0 ? ur() < 0.8f : ur() < DENS[mi], v;
            if (!on) {
                d[s] = NODEG;
                a[s] = 0;
                continue;
            }
            if (mi == 6) {
                v = prev - (ur() < 0.6f) - (ur() < 0.2f);
                if (v < 0)
                    v = 0;
            } else if (mi == 0)
                v = (int)(ur() * (float)span) + (ur() < 0.3f ? 7 : 0);
            else
                v = (int)(ur() * (float)span);
            d[s] = (int8_t)v;
            prev = v;
            a[s] = (int8_t)(s % 4 == 0 && ur() < (mi == 0 ? 0.8f : 0.5f));
        }
        last_note[tr] = ROOT[tr];
    } else {
        int n = 2 + (int)(ur() * 2.0f), i;
        for (s = 0; s < 16; s++)
            song.bz[s] = 0;
        for (i = 0; i < n; i++)
            song.bz[3 + (int)(ur() * 13.0f)] = (int8_t)(1 + (int)(ur() * 6.0f));
    }
    render_notes();
    drums_out();
    song_dirty = 1;
}

/* a new mood: the same melodies in the new scale; the drums follow until someone edits them */
static void mood_changed(void)
{
    render_notes();
    if (!song.drum_edited)
        gen_drums(mood());
}

/* the knobs as bytes, and back */
static uint8_t q8(float v) { return (uint8_t)(v <= 0.0f ? 0 : (v >= 1.0f ? 255 : (int)(v * 255.0f + 0.5f))); }

static void song_capture(void)
{
    int t, k;
    song.magic = SONG_MAGIC;
    for (t = 0; t < 4; t++) {
        for (k = 0; k < 5; k++)
            song.T[t][k] = q8(zz_T[t][k]);
        song.mute[t] = zz_mute[t];
    }
    for (k = 0; k < 3; k++)
        song.G[k] = q8(zz_G[k]);
}

static void song_apply(void)
{
    int t, k;
    for (t = 0; t < 4; t++) {
        for (k = 0; k < 5; k++)
            zz_T[t][k] = (float)song.T[t][k] / 255.0f;
        zz_mute[t] = song.mute[t] ? 1u : 0u;
    }
    for (k = 0; k < 3; k++)
        zz_G[k] = (float)song.G[k] / 255.0f;
    render_notes();
    drums_out();
}

static void song_defaults(void)
{
    static const float T0[4][5] = {{0.55f, 0.25f, 0.0f, 0.30f, 0.80f},
                                   {0.35f, 0.30f, 0.0f, 0.40f, 0.65f},
                                   {0.60f, 0.35f, 0.0f, 0.25f, 0.75f},
                                   {0.50f, 0.40f, 0.0f, 0.30f, 0.75f}};
    int t, k;
    for (t = 0; t < 4; t++) {
        for (k = 0; k < 5; k++)
            zz_T[t][k] = T0[t][k];
        zz_mute[t] = 0;
    }
    zz_G[ZZ_HUMEUR] = 0.12f;
    zz_G[ZZ_APERO] = 0.15f;
    zz_G[ZZ_CAFEINE] = 0.45f;
    song.oct = 0;
    for (t = 0; t < 4; t++)
        gen_track(t);
    song_capture();
    song_dirty = 0;
}

static int song_valid(void)
{
    int s;
    if (song.magic != SONG_MAGIC || song.oct < -2 || song.oct > 2)
        return 0;
    for (s = 0; s < 16; s++) {
        int l;
        for (l = 0; l < 4; l++)
            if (song.drum[l][s] < 0 || song.drum[l][s] > 2)
                return 0;
        if (song.bz[s] < 0 || song.bz[s] > 6)
            return 0;
        if ((song.syn_d[s] != NODEG && (song.syn_d[s] < -14 || song.syn_d[s] > 21)) ||
            (song.bas_d[s] != NODEG && (song.bas_d[s] < -14 || song.bas_d[s] > 21)))
            return 0;
    }
    return 1;
}

static void song_load(void)
{
    if (plat_store_load(OBJ_SONG, &song, sizeof song) == (int)sizeof song && song_valid())
        song_apply();
    else
        song_defaults();
    song_dirty = 0;
}

static int song_save(void)
{
    song_capture();
    if (plat_store_save(OBJ_SONG, &song, sizeof song) != 0)
        return -1;
    song_dirty = 0;
    return 0;
}
