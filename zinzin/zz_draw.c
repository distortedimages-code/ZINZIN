/* ZINZIN: the screen. A scanline renderer: there is no frame buffer (the FM-1 has no RAM for one), so the picture
 * is a scene (worlds, the herd, the windows, the webcam, the cat, flyers, pop-ups, rain, the HUD) that can draw any
 * one of its rows on demand. The whole-screen effects then cost nothing: a row can be fetched from another height,
 * shifted, mirrored, fetched once for a block of rows (pixelation), dithered to grey. Rows go out 40 at a time in
 * two bands of gfx.c's canvas, one drawn while the other is sent.
 *
 * The scene moves at 12.5 frames a second, like the browser proto it comes from; each frame is drawn over a few
 * passes of the main loop. Every random number a frame needs is drawn in scene_update, so its rows agree.
 * A unit of its own, at -O2 (fm1.toml o2); the app sends the rows (zz_draw.h). */
#include "plat.h"
#include "zz_draw.h"
#include "zz_engine.h"
#include "zz_math.h"
#include "zz_assets.h"
#include "zz_font.h"

zz_view_t zz_view;

/* the screen's own dice */
static uint32_t d_rng = 0x6D2B79F5u;
static uint32_t xr32(void)
{
    d_rng ^= d_rng << 13;
    d_rng ^= d_rng >> 17;
    d_rng ^= d_rng << 5;
    return d_rng;
}
static float ur(void) { return (float)(xr32() >> 8) * (1.0f / 16777216.0f); }

#define SW 240
#define BAND 40
#define FRAME_MS 80u

#define ZH(h) ((uint16_t)((((((h) >> 16) & 0xFFu) >> 3) << 11) | (((((h) >> 8) & 0xFFu) >> 2) << 5) | (((h) & 0xFFu) >> 3)))
static inline int c_r(uint16_t c) { return ((c >> 11) << 3) | (c >> 13); }
static inline int c_g(uint16_t c) { return (((c >> 5) & 63) << 2) | ((c >> 9) & 3); }
static inline int c_b(uint16_t c) { return ((c & 31) << 3) | ((c >> 2) & 7); }
static inline uint16_t c565(int r, int g, int b)
{
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static inline uint16_t mix565(uint16_t a, uint16_t b, int t)   /* t 0..256 */
{
    return c565(c_r(a) + ((c_r(b) - c_r(a)) * t >> 8), c_g(a) + ((c_g(b) - c_g(a)) * t >> 8),
                c_b(a) + ((c_b(b) - c_b(a)) * t >> 8));
}
static inline int iround(float v) { return (int)(v + (v >= 0.0f ? 0.5f : -0.5f)); }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float cl01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static int isqrt(int v)
{
    int r = 0, b = 1 << 14;
    if (v <= 0)
        return 0;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

#define C_BLACK0 0x0000u
#define C_WHITE0 0xFFFFu
#define C_SILVER ZH(0xc0c0c0)
#define C_G80 ZH(0x808080)
#define C_G40 ZH(0x404040)
static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static const uint32_t TRK_COL[4] = {0xff7a1a, 0xff3d8b, 0xffd400, 0x2ee6d6};
static const char *const TRK_NAME[4] = {"BOUM", "MATOU", "MAMMOUTH", "BAZAR"};
static const char *const MACRO_FILE[8] = {"BIDOCHE.EXE", "CHATOUILLE.SYS", "MOISI.ZIP", "FLEMME.BAT", "BOUCAN.CFG",
                                         "HUMEUR.INI", "APERO.EXE", "CAFEINE.COM"};
static const char *const BET_FILE[7] = {"", "BEGAIE.EXE", "RECULE.EXE", "FIGE.EXE", "COULE.EXE", "PLONGE.EXE",
                                        "ECRASE.EXE"};

/* ------------------------------------------------------------------ pictures */
enum { S_HEART, S_STAR, S_SMILEY, S_GHOST, S_UFO, S_CD, S_FLOPPY, S_ALIEN, S_SKULL, S_FLAME, S_EYE, S_TEAR, S_RAIN,
       S_BROKEN, I_CUP, I_COIN, I_CAT2, I_MOON, ND };
static const zz_img_t *const DRAW[ND] = {&ZZ_S_HEART, &ZZ_S_STAR, &ZZ_S_SMILEY, &ZZ_S_GHOST, &ZZ_S_UFO, &ZZ_S_CD,
                                         &ZZ_S_FLOPPY, &ZZ_S_ALIEN, &ZZ_S_SKULL, &ZZ_S_FLAME, &ZZ_S_EYE, &ZZ_S_TEAR,
                                         &ZZ_S_RAIN, &ZZ_S_BROKEN, &ZZ_CUP, &ZZ_COIN, &ZZ_CAT2, &ZZ_MOON};

/* the same pixels, other palettes: on the FM-1 a tint is 16 colours, not a blend */
static uint16_t pal_meadow_k[16], pal_meadow_g[16], pal_moon_r[16], pal_cat_r[16], pal_cat_c[16], pal_warhol[4][16],
    pal_bird_doom[16], pal_fire[37];
static void pal_tint(uint16_t *d, const uint16_t *s, uint32_t col, float a)
{
    int i, t = (int)(a * 256.0f);
    for (i = 0; i < 16; i++)
        d[i] = mix565(s[i], ZH(col), t);
}
static void palettes_init(void)
{
    static const uint32_t WH[4][4] = {{0xffe14d, 0xc8102e, 0xff7ab8, 0xfff7d6},
                                      {0x2a7fff, 0x1d1730, 0x43ff86, 0xe6fff0},
                                      {0xff3d8b, 0x2b0b3d, 0xffb000, 0xfff3b0},
                                      {0x43ff86, 0x0a2a7a, 0xff4fa3, 0xffe0ef}};
    static const int16_t ST[7][4] = {{0, 7, 7, 7}, {8, 103, 31, 7}, {16, 175, 63, 7}, {24, 223, 95, 7},
                                     {30, 207, 151, 39}, {34, 191, 191, 71}, {36, 255, 255, 255}};
    int i, w;
    pal_tint(pal_meadow_k, ZZ_MEADOW.pal, 0xffb4e1, 0.3f);
    for (i = 0; i < 16; i++) {
        uint16_t c = ZZ_MEADOW.pal[i];
        int v = (c_r(c) * 30 + c_g(c) * 59 + c_b(c) * 11) * 75 / 10000;
        pal_meadow_g[i] = c565(v, v, v);
    }
    pal_tint(pal_moon_r, ZZ_MOON.pal, 0xff1a00, 0.55f);
    pal_tint(pal_cat_r, ZZ_CAT.pal, 0xff0050, 0.8f);
    pal_tint(pal_cat_c, ZZ_CAT.pal, 0x00e5ff, 0.8f);
    for (w = 0; w < 4; w++)
        for (i = 0; i < 16; i++) {
            uint16_t c = ZZ_CAT.pal[i];
            int l = (c_r(c) * 30 + c_g(c) * 59 + c_b(c) * 11) / 100;
            pal_warhol[w][i] = ZH(WH[w][i == 0 ? 0 : (l < 75 ? 1 : (l < 150 ? 2 : 3))]);
        }
    for (i = 0; i < 16; i++) {
        uint16_t c = ZZ_BIRD.pal[i];
        pal_bird_doom[i] = c565(c_r(c) * 255 / 255, c_g(c) * 0x5a / 255, c_b(c) * 0x4a / 255);
    }
    for (i = 0; i < 37; i++) {
        int j = 0;
        while (j < 5 && i > ST[j + 1][0])
            j++;
        {
            int span = ST[j + 1][0] - ST[j][0], tt = span ? (i - ST[j][0]) * 256 / span : 0;
            pal_fire[i] = c565(ST[j][1] + ((ST[j + 1][1] - ST[j][1]) * tt >> 8), ST[j][2] + ((ST[j + 1][2] - ST[j][2]) * tt >> 8),
                               ST[j][3] + ((ST[j + 1][3] - ST[j][3]) * tt >> 8));
        }
    }
}

/* ------------------------------------------------------------------ row primitives (L: one row, native RGB565) */
static inline void hspan(uint16_t *L, int x0, int x1, uint16_t c)
{
    if (x0 < 0)
        x0 = 0;
    if (x1 > SW)
        x1 = SW;
    for (; x0 < x1; x0++)
        L[x0] = c;
}
static inline void rect_row(uint16_t *L, int sy, int x, int y, int w, int h, uint16_t c)
{
    if (sy >= y && sy < y + h)
        hspan(L, x, x + w, c);
}
static inline void pset(uint16_t *L, int x, uint16_t c)
{
    if ((unsigned)x < SW)
        L[x] = c;
}

#define F_FLIPX 1
#define F_CHECK 2     /* every other pixel: a free 50 % */
#define F_SIL 4       /* one colour: a silhouette */
#define F_OPAQUE 8    /* colour 0 drawn too */

/* one source row of a picture (frame fr), columns sx0..sx0+sw, stretched over dw pixels from dx */
static void img_line(uint16_t *L, int sy, const zz_img_t *im, int fr, const uint16_t *pal, int srow, int dx, int dw,
                     int sx0, int sw, int fl, uint16_t silc)
{
    int stride = (im->w + 1) >> 1, x, x0, x1;
    const uint8_t *row;
    uint32_t u, du;
    if (dw <= 0 || sw <= 0 || srow < 0 || srow >= im->h || dx >= SW || dx + dw <= 0)
        return;
    row = im->px + (uint32_t)fr * (uint32_t)stride * im->h + (uint32_t)srow * (uint32_t)stride;
    du = ((uint32_t)sw << 16) / (uint32_t)dw;
    x0 = dx < 0 ? 0 : dx;
    x1 = dx + dw > SW ? SW : dx + dw;
    u = (uint32_t)(x0 - dx) * du + (du >> 1);
    for (x = x0; x < x1; x++, u += du) {
        int sx = (int)(u >> 16), i;
        if (sx >= sw)
            sx = sw - 1;
        if (fl & F_FLIPX)
            sx = sw - 1 - sx;
        sx += sx0;
        i = (row[sx >> 1] >> ((sx & 1) ? 0 : 4)) & 15;
        if (!i && im->t && !(fl & F_OPAQUE))
            continue;
        if ((fl & F_CHECK) && ((x + sy) & 1))
            continue;
        L[x] = (fl & F_SIL) ? silc : pal[i];
    }
}
/* a picture (or a part of it) stretched over a rectangle */
static void img_row(uint16_t *L, int sy, const zz_img_t *im, int fr, const uint16_t *pal, int dx, int dy, int dw, int dh,
                    int sx0, int sy0, int sw, int sh, int fl, uint16_t silc)
{
    if (sy < dy || sy >= dy + dh || dh <= 0)
        return;
    img_line(L, sy, im, fr, pal, sy0 + (sy - dy) * sh / dh, dx, dw, sx0, sw, fl, silc);
}
/* a picture centred on (cx, cy) at scale s */
static void spr_row(uint16_t *L, int sy, const zz_img_t *im, const uint16_t *pal, float cx, float cy, float s, int fl,
                    uint16_t silc)
{
    int w = iround((float)im->w * s), h = iround((float)im->h * s);
    img_row(L, sy, im, 0, pal ? pal : im->pal, iround(cx - (float)w * 0.5f), iround(cy - (float)h * 0.5f), w, h, 0, 0,
            im->w, im->h, fl, silc);
}

/* a sprite turned and stretched: the inverse map, fixed point (scene_update works it out once a frame) */
typedef struct {
    const zz_img_t *im;
    const uint16_t *pal;
    int16_t cx, cy, r;
    uint8_t fl;
    uint16_t silc;
    int32_t a, b, c, d;   /* source per destination pixel, 16.16 */
} aff_t;
static void aff_set(aff_t *f, const zz_img_t *im, float cx, float cy, float s, float xs, float ang, int fl, uint16_t silc)
{
    float co = zz_cosf(ang), si = zz_sinf(ang), sx = s * xs, rr;
    if (sx < 0.0f)
        sx = -sx;
    if (sx < 0.05f)
        sx = 0.05f;
    f->im = im;
    f->pal = im->pal;
    f->cx = (int16_t)iround(cx);
    f->cy = (int16_t)iround(cy);
    rr = 0.5f * s * (float)(im->w > im->h ? im->w : im->h) * 1.42f + 1.0f;
    f->r = (int16_t)(rr > 200.0f ? 200 : rr);
    f->fl = (uint8_t)fl;
    f->silc = silc;
    f->a = (int32_t)(co / sx * 65536.0f);
    f->b = (int32_t)(si / sx * 65536.0f);
    f->c = (int32_t)(-si / s * 65536.0f);
    f->d = (int32_t)(co / s * 65536.0f);
}
static void aff_row(uint16_t *L, int sy, const aff_t *f)
{
    const zz_img_t *im = f->im;
    int stride = (im->w + 1) >> 1, x0, x1, x, fy;
    int32_t u, v, hw = (int32_t)im->w << 15, hh = (int32_t)im->h << 15;
    if (sy < f->cy - f->r || sy > f->cy + f->r)
        return;
    x0 = f->cx - f->r;
    x1 = f->cx + f->r + 1;
    if (x0 < 0)
        x0 = 0;
    if (x1 > SW)
        x1 = SW;
    fy = sy - f->cy;
    u = f->a * (x0 - f->cx) + f->b * fy + hw;
    v = f->c * (x0 - f->cx) + f->d * fy + hh;
    for (x = x0; x < x1; x++, u += f->a, v += f->c) {
        int su = u >> 16, sv = v >> 16, i;
        if ((unsigned)su >= im->w || (unsigned)sv >= im->h)
            continue;
        i = (im->px[sv * stride + (su >> 1)] >> ((su & 1) ? 0 : 4)) & 15;
        if (!i)
            continue;
        if ((f->fl & F_CHECK) && ((x + sy) & 1))
            continue;
        L[x] = (f->fl & F_SIL) ? f->silc : f->pal[i];
    }
}

/* a segment one pixel wide (rain, grids, shooting stars) or thick (the lasers): the run it covers on this row */
static void seg_row(uint16_t *L, int sy, float x0, float y0, float x1, float y1, float th, uint16_t c)
{
    float ylo = y0 < y1 ? y0 : y1, yhi = y0 < y1 ? y1 : y0, dy = y1 - y0, ht = th * 0.5f, ya, yb, xa, xb;
    if ((float)sy + 1.0f < ylo - ht || (float)sy > yhi + ht)
        return;
    if (dy > -0.01f && dy < 0.01f) {          /* flat */
        hspan(L, iround(x0 < x1 ? x0 : x1), iround(x0 < x1 ? x1 : x0) + 1, c);
        return;
    }
    ya = (float)sy - ht;
    yb = (float)sy + 1.0f + ht;
    if (ya < ylo)
        ya = ylo;
    if (yb > yhi)
        yb = yhi;
    if (yb < ya)
        yb = ya;
    xa = x0 + (x1 - x0) * (ya - y0) / dy;
    xb = x0 + (x1 - x0) * (yb - y0) / dy;
    if (xa > xb) {
        float tt = xa;
        xa = xb;
        xb = tt;
    }
    hspan(L, (int)xa, (int)xb + 1, c);
}

/* text: UTF-8 (Latin-1 is drawn, the rest skipped) */
static uint32_t utf8_next(const char **ps)
{
    const uint8_t *s = (const uint8_t *)*ps;
    uint32_t cp = *s++;
    if (cp >= 0xC0u && cp < 0xE0u && (*s & 0xC0u) == 0x80u)
        cp = ((cp & 0x1Fu) << 6) | (*s++ & 0x3Fu);
    else if (cp >= 0x80u) {
        while ((*s & 0xC0u) == 0x80u)
            s++;
        cp = '?';
    }
    *ps = (const char *)s;
    return cp;
}
static int ztext_w(const zz_font_t *f, const char *s)
{
    int w = 0;
    while (*s) {
        int g = zz_glyph(utf8_next(&s));
        if (g >= 0)
            w += f->adv[g];
    }
    return w;
}
static void text_row(uint16_t *L, int sy, const zz_font_t *f, int x, int y, const char *s, uint16_t c)
{
    int r = sy - y;
    if (r < 0 || r >= f->h)
        return;
    while (*s && x < SW) {
        int g = zz_glyph(utf8_next(&s)), b;
        uint16_t bits;
        if (g < 0)
            continue;
        bits = f->rows[g * f->h + r];
        for (b = 0; bits && b < 16; b++, bits = (uint16_t)(bits << 1))
            if (bits & 0x8000u)
                pset(L, x + b, c);
        x += f->adv[g];
    }
}

/* Windows 98: a raised (or sunk) grey box, and a title bar */
static void bevel_row(uint16_t *L, int sy, int x, int y, int w, int h, int inset)
{
    int r = sy - y;
    uint16_t hi = inset ? C_G80 : C_WHITE0, lo = inset ? C_WHITE0 : C_G40, in = inset ? C_G40 : C_G80;
    if (r < 0 || r >= h)
        return;
    hspan(L, x, x + w, C_SILVER);
    if (r == 0)
        hspan(L, x, x + w, hi);
    pset(L, x, hi);
    if (r == h - 1)
        hspan(L, x, x + w, lo);
    pset(L, x + w - 1, lo);
    if (inset) {
        if (r == 1)
            hspan(L, x + 1, x + w - 1, in);
        if (r >= 1 && r <= h - 2)
            pset(L, x + 1, in);
    } else {
        if (r == h - 2)
            hspan(L, x + 1, x + w - 1, in);
        if (r >= 1 && r <= h - 2)
            pset(L, x + w - 2, in);
    }
}
#define TITLE_H 12
static void title_row(uint16_t *L, int sy, int x, int y, int w, uint32_t c0, uint32_t c1, const char *title)
{
    int r = sy - y, i;
    uint16_t a = ZH(c0), b = ZH(c1);
    if (r < 0 || r >= TITLE_H)
        return;
    for (i = 0; i < w; i++)
        pset(L, x + i, mix565(a, b, i * 256 / (w > 1 ? w - 1 : 1)));
    text_row(L, sy, &ZZ_FONT_T, x + 3, y, title, C_WHITE0);
    bevel_row(L, sy, x + w - 12, y + 1, 11, 10, 0);
    for (i = 0; i < 5; i++)
        if (r == 3 + i) {
            pset(L, x + w - 9 + i, C_BLACK0);
            pset(L, x + w - 5 - i, C_BLACK0);
        }
}

/* ------------------------------------------------------------------ the scene */
static const uint8_t WORLD_OF[7] = {0, 0, 1, 1, 1, 2, 3};
static int zone_of(float h)
{
    int m = (int)(h * 7.0f);
    return WORLD_OF[m < 0 ? 0 : (m > 6 ? 6 : m)];
}
static void weights(float h, float *w)
{
    float f = h * 7.0f - 0.5f;
    int i0;
    w[0] = w[1] = w[2] = w[3] = 0.0f;
    if (f <= 0.0f) {
        w[0] = 1.0f;
        return;
    }
    if (f >= 6.0f) {
        w[3] = 1.0f;
        return;
    }
    i0 = (int)f;
    w[WORLD_OF[i0]] += 1.0f - (f - (float)i0);
    w[WORLD_OF[i0 + 1]] += f - (float)i0;
}

typedef struct {
    const char *title[3];
    uint8_t icon;
    uint32_t bar[2], ghost;
    const char *txt[7];
    uint8_t ntxt;
} msg_t;
static const msg_t MSG[4] = {
    {{"calin.exe", "msn_rose", "bisous.ini"}, S_HEART, {0xff6fb5, 0xffc2e2}, 0xff9ad5,
     {"Ton chat t'aime.", "licorne_92 vient de se connecter", "Câlin envoyé à 100 %", "Paillettes installées",
      "Tu as reçu un bisou", "Le poney est content", "Fond d'écran : arc-en-ciel"}, 7},
    {{"cosmos.zip", "modem.exe", "Minitel"}, S_UFO, {0x000080, 0x1084d0}, 0x43c6ff,
     {"Signal alien reçu", "Connexion 56k : bip krrr", "Le CD-ROM est en orbite", "Le Minitel te salue",
      "Téléchargement : 3 jours restants", "Ton modem chante faux"}, 6},
    {{"666.sys", "enfer.bat", "ERREUR"}, S_SKULL, {0x5a0000, 0xe01b1b}, 0xff3a1a,
     {"RIP ta RAM", "Erreur 666 : âme introuvable", "Le grille-pain a rejoint les enfers", "Bug de l'an 2000 détecté",
      "Ton disque dur hurle", "Le chat a vu des choses"}, 6},
    {{"seul.txt", "pluie.wav", "bof.ini"}, S_BROKEN, {0x3a3a3a, 0x8a8a8a}, 0xa0a0a0,
     {"Personne n'a signé ton livre d'or", "Ton tamagotchi est mort", "0 nouveau message", "Le modem a raccroché",
      "Il pleut sur ta page perso", "Même le chat s'ennuie"}, 6},
};
static const char *const MARQ[4] = {
    "*~ ZINZIN ~* bienvenue sur ma page perso ~ meilleur rendu en 240x240 ~ signe mon livre d'or ~ ",
    "*~ ZINZIN ~* connexion au cosmos en cours ~ ne decroche pas le telephone ~ ",
    "*~ ZINZIN ~* systeme corrompu ~ 666 visiteurs dans l'enfer ~ redemarrage impossible ~ ",
    "... zinzin ... page pas mise a jour depuis 1999 ... plus personne ne passe ici ... "};
static const uint8_t POOLS[4][7] = {{S_HEART, S_STAR, S_SMILEY, S_GHOST, I_CUP, I_COIN, I_CAT2},
                                    {S_UFO, S_CD, S_FLOPPY, S_ALIEN, I_CUP, I_MOON, S_STAR},
                                    {S_SKULL, S_FLAME, S_EYE, I_COIN, S_SKULL, S_FLAME, S_SKULL},
                                    {S_BROKEN, S_RAIN, S_TEAR, I_CUP, I_MOON, S_BROKEN, S_RAIN}};
static const uint8_t POOLN[4] = {7, 7, 6, 6};

/* the visual state (vs): what the sounds and the knobs leave behind */
static struct {
    uint32_t frame, now, hits, bass_ms;
    float t, vt, sp, flash, cat_pulse, bird_kick, fire_kick, glitch, shake, quake, horse_x, bird_f;
    int bird_dir, flash_z;
    struct { int k; uint32_t start, until; } bet;
    struct { int k, tr; float v; uint32_t t; } prog;             /* a knob turned: its window */
    struct { char a[28], b[28]; uint32_t until; } say;            /* app_say */
    /* this frame (scene_update) */
    float w[4], euph, rain, dep, ET[4][5], EG[3];
    int z, wa, wb, wlvl, fire_on, herd_n, cradle_f, bird_fi, bird_mode, warhol, cat_size, cat_d, cat_dbl, word, bet_now;
    float bp;
    int16_t herd_x[3], herd_y[3], herd_s;
    uint16_t herd_c;
    float bird_zoom, bird_rot;
    int16_t cat_off[4][64];
    float eyes[2][2];
    int neyes, eye_fx, laser;
    float laser_ang;
    int marq_off, marq_w;
    /* the post effects */
    int warp, dy, qk, sx, slide, bl, lv, mirror, mul, dark, fa;
    uint16_t flash_c;
    int glitch_n, glitch_y[8], glitch_h[8], glitch_dx[8];
    int mo_band, mo_h, mo_dx, noise;
    uint32_t seed;
    int bw_th;
} vs;

/* particles */
#define NAMB 60
static struct { uint8_t k, z; float x, y, vx, vy, ph, ox, oy; } amb[NAMB] PLAT_POOL;
static int namb;
#define NFLY 16
static struct { uint8_t d, coin, z, sad, hn; float sc, x, y, vx, vy, r, vr, hx[12], hy[12], hr[12]; } fly[NFLY] PLAT_POOL;
static int nfly;
#define NPOP 3
typedef struct { float x, y, dx, dy, born, life, hx[12], hy[12]; int w, h, z, hn; uint8_t icon; const char *title; char l1[28], l2[28]; } pop_t;
static pop_t pops[NPOP];
static int npop;
#define NCONF 90
static struct { float x, y, vx, vy, ph; uint16_t c; } conf[NCONF] PLAT_POOL;
static int nconf;
#define NTEAR 50
static struct { float x, y, vy; } tears[NTEAR];
static int ntear;
#define NRAIN 70
static int16_t rain_x[NRAIN], rain_y[NRAIN];
static int nrain;
static uint16_t rain_c;
#define NAFF 64
static aff_t affs[NAFF] PLAT_POOL;
static int naff;
static uint8_t fire[80 * 52] PLAT_POOL;   /* (the pool: the RAM proper is the platform's) */

static int pick_zone(void)
{
    float r = ur() * (vs.w[0] + vs.w[1] + vs.w[2] + vs.w[3]);
    int i;
    for (i = 0; i < 4; i++) {
        r -= vs.w[i];
        if (r <= 0.0f)
            return i;
    }
    return 3;
}

/* the words of a message on two lines of the window */
static void wrap2(const char *s, int maxw, char *l1, char *l2)
{
    int n = 0, w = 0, lastsp = -1, i = 0, j;
    const char *p = s;
    l1[0] = l2[0] = 0;
    while (*p) {
        const char *q = p;
        uint32_t cp = utf8_next(&q);
        int g = zz_glyph(cp), cw = g >= 0 ? ZZ_FONT_T.adv[g] : 0;
        if (cp == ' ')
            lastsp = (int)(p - s);
        if (w + cw > maxw && lastsp > 0)
            break;
        w += cw;
        p = q;
    }
    n = *p ? lastsp : (int)(p - s);
    for (i = 0; i < n && i < 27; i++)
        l1[i] = s[i];
    l1[i] = 0;
    if (!*p)
        return;
    p = s + n + 1;
    for (j = 0; p[j] && j < 27; j++)
        l2[j] = p[j];
    l2[j] = 0;
    if (ztext_w(&ZZ_FONT_T, l2) > maxw) {     /* too long: cut, with dots */
        while (j > 0 && ztext_w(&ZZ_FONT_T, l2) > maxw - 12)
            l2[--j] = 0;
        while (j > 0 && (l2[j - 1] & 0xC0) == 0x80)
            l2[--j] = 0;
        if (j > 0 && (uint8_t)l2[j - 1] >= 0xC0)
            l2[--j] = 0;
        l2[j] = '.';
        l2[j + 1] = '.';
        l2[j + 2] = 0;
    }
}

static void spawn_flyer(float n, int acc, int force)
{
    int z = pick_zone(), k = force >= 0 ? force : POOLS[z][(int)(ur() * POOLN[z])], sad;
    float sc;
    if (nfly >= NFLY) {
        int i;
        for (i = 1; i < nfly; i++)
            fly[i - 1] = fly[i];
        nfly--;
    }
    if (k >= I_CUP)
        sc = k == I_COIN ? 1.2f : (k == I_MOON ? 0.7f : 0.84f);
    else
        sc = 2.0f + (acc ? 1.0f : 0.0f);
    sc *= 0.75f + vs.ET[1][ZZ_BIDOCHE] * 0.7f;
    sad = vs.dep > 0.5f;
    fly[nfly].d = (uint8_t)k;
    fly[nfly].sc = sc;
    fly[nfly].x = clampf((n - 41.0f) / 50.0f * SW, 16.0f, 224.0f);
    fly[nfly].y = sad ? -20.0f : SW + 20.0f;
    fly[nfly].vx = (ur() - 0.5f) * (sad ? 1.5f : 7.0f);
    fly[nfly].vy = sad ? 1.0f + ur() : -(15.0f + ur() * 7.0f + (acc ? 3.0f : 0.0f));
    fly[nfly].r = 0.0f;
    fly[nfly].vr = (ur() - 0.5f) * (sad ? 0.15f : 0.5f);
    fly[nfly].coin = k == I_COIN;
    fly[nfly].z = (uint8_t)z;
    fly[nfly].sad = (uint8_t)sad;
    fly[nfly].hn = 0;
    nfly++;
}

static pop_t *pop_new(void)
{
    if (npop >= NPOP) {
        int i;
        for (i = 1; i < npop; i++)
            pops[i - 1] = pops[i];
        npop--;
    }
    pops[npop].hn = 0;
    return &pops[npop++];
}
static void spawn_pop(void)
{
    int z = pick_zone();
    const msg_t *m = &MSG[z];
    pop_t *last = npop ? &pops[npop - 1] : 0, *p;
    float x, y, sp = 1.0f + vs.ET[3][ZZ_FLEMME] * 2.0f;
    const int w = 138, h = 64;
    if (last && vs.t - last->born < 1.4f) {
        x = last->x + 12.0f;
        y = last->y + 12.0f;
    } else {
        x = 4.0f + ur() * (SW - w - 8);
        y = 18.0f + ur() * 110.0f;
    }
    if (x > SW - w - 2 || y > SW - h - 26) {
        x = 6.0f + ur() * 30.0f;
        y = 20.0f + ur() * 30.0f;
    }
    p = pop_new();
    p->x = x;
    p->y = y;
    p->w = w;
    p->h = h;
    p->z = z;
    p->icon = m->icon;
    p->title = m->title[(int)(ur() * 3.0f) % 3];
    wrap2(m->txt[(int)(ur() * m->ntxt) % m->ntxt], w - 38, p->l1, p->l2);
    p->born = vs.t;
    p->life = 1.1f + vs.ET[3][ZZ_FLEMME] * 2.0f;
    p->dx = (ur() - 0.5f) * 2.4f * sp;
    p->dy = (ur() - 0.5f) * 1.6f * sp;
}

/* ------------------------------------------------------------------ events from the engine */
static void on_hit(int tr, int v, int acc)
{
    int z = zone_of(zz_EG[ZZ_HUMEUR]);
    vs.hits++;
    zz_view.note_ms = vs.now;
    if (tr == 0) {
        if (v == 0) {
            float s = (acc ? 5.0f : 2.0f) * (0.4f + zz_ET[3][ZZ_BIDOCHE]) + zz_ET[0][ZZ_BIDOCHE] * 3.0f;
            vs.bird_kick = 1.0f;
            vs.fire_kick = 1.0f;
            if (s > vs.shake)
                vs.shake = s;
        } else if (v == 1) {
            float f = acc ? 0.85f : 0.5f;
            if (f > vs.flash)
                vs.flash = f;
            vs.flash_z = z;
            if (acc && ur() < 0.25f)
                spawn_pop();
        } else if (v == 2) {
            static const uint8_t HAT[4] = {S_STAR, S_CD, S_EYE, S_TEAR};
            if (ur() < 0.5f)
                spawn_flyer(60.0f + ur() * 30.0f, 0, HAT[z]);
        } else
            spawn_flyer(50.0f + ur() * 30.0f, acc, I_COIN);
    } else if (tr == 1) {
        vs.cat_pulse = 1.0f;
        spawn_flyer((float)v, acc, -1);
        if ((acc && ur() < 0.6f) || ur() < 0.08f)
            spawn_pop();
    } else if (tr == 2) {
        float q = (acc ? 1.0f : 0.6f) * (0.4f + zz_ET[2][ZZ_BIDOCHE] * 0.9f);
        if (q > vs.quake)
            vs.quake = q;
        vs.bass_ms = vs.now;
    }
    if (zz_ET[3][ZZ_MOISI] > 0.35f && ur() < 0.4f)
        vs.glitch = 1.0f;
}
static void on_bet(int k, int ms)
{
    pop_t *p;
    vs.bet.k = k;
    vs.bet.start = vs.now;
    vs.bet.until = vs.now + (uint32_t)ms;
    p = pop_new();
    p->x = 8.0f + ur() * 90.0f;
    p->y = 30.0f + ur() * 100.0f;
    p->w = 138;
    p->h = 64;
    p->z = zone_of(zz_EG[ZZ_HUMEUR]);
    p->icon = MSG[p->z].icon;
    p->title = BET_FILE[k];
    wrap2("a cessé de fonctionner", 100, p->l1, p->l2);
    p->born = vs.t;
    p->life = (float)ms / 1000.0f + 0.4f;
    if (p->life < 0.8f)
        p->life = 0.8f;
    p->dx = p->dy = 0.0f;
}

/* the sounds' events wait for the next frame (a frame's rows must agree) */
#define NVQ 32
static struct { uint8_t bet, tr, v, acc; uint16_t ms; } vq[NVQ];
static int nvq;
static void vis_hit(int tr, int v, int acc)
{
    if (nvq < NVQ) {
        vq[nvq].bet = 0;
        vq[nvq].tr = (uint8_t)tr;
        vq[nvq].v = (uint8_t)v;
        vq[nvq].acc = (uint8_t)(acc != 0);
        nvq++;
    }
}
static void vis_pop(void)
{
    if (nvq < NVQ) {
        vq[nvq].bet = 2;
        nvq++;
    }
}
static void vis_bet(int k, int ms)
{
    if (nvq < NVQ && k > 0 && k < 7) {
        vq[nvq].bet = 1;
        vq[nvq].v = (uint8_t)k;
        vq[nvq].ms = (uint16_t)(ms > 60000 ? 60000 : ms);
        nvq++;
    }
}

/* ------------------------------------------------------------------ once a frame: move everything */
static void fire_step(int src)
{
    int x, y;
    for (x = 0; x < 80; x++)
        fire[51 * 80 + x] = (uint8_t)src;
    for (x = 0; x < 80; x++)
        for (y = 1; y < 52; y++) {
            int i = y * 80 + x, p = fire[i], r, d;
            if (!p) {
                fire[i - 80] = 0;
                continue;
            }
            r = (int)(ur() * 3.99f);
            d = i - r + 1 - 80;
            if (d >= 0)
                fire[d] = (uint8_t)(p - (r & 1) < 0 ? 0 : p - (r & 1));
        }
}

static void amb_update(float sp)
{
    static const uint8_t AMBK[4] = {S_HEART, S_STAR, S_STAR, S_GHOST};
    int i;
#define AMB_ADD(KK, ZZ, X, Y, VX, VY)                                                                          \
    if (namb < NAMB) {                                                                                         \
        amb[namb].k = (uint8_t)(KK);                                                                          \
        amb[namb].z = (uint8_t)(ZZ);                                                                          \
        amb[namb].x = (X);                                                                                    \
        amb[namb].y = (Y);                                                                                    \
        amb[namb].vx = (VX);                                                                                  \
        amb[namb].vy = (VY);                                                                                  \
        amb[namb].ph = ur() * 6.0f;                                                                           \
        amb[namb].ox = amb[namb].x;                                                                           \
        amb[namb].oy = amb[namb].y;                                                                           \
        namb++;                                                                                               \
    }
    if (ur() < vs.w[0] * 0.5f)
        AMB_ADD(AMBK[(int)(ur() * 4.0f) & 3], 0, ur() * SW, SW + 8.0f, 0.0f, -(1.5f + ur() * 2.0f))
    if (ur() < vs.w[1] * 0.8f)
        AMB_ADD(0, 1, 120.0f, 96.0f, (ur() - 0.5f) * 14.0f, (ur() - 0.5f) * 10.0f)
    if (ur() < vs.w[2] * 0.9f)
        AMB_ADD(0, 2, ur() * SW, SW - 30.0f, 0.0f, -(2.0f + ur() * 4.0f))
    if (ur() < vs.w[2] * 0.06f)
        AMB_ADD(S_SKULL, 3, ur() * SW, -10.0f, 0.0f, 2.0f + ur() * 2.0f)
    if (ur() < vs.w[3] * 0.25f)
        AMB_ADD(S_BROKEN, 4, ur() * SW, -10.0f, 0.0f, 0.8f + ur())
    for (i = namb - 1; i >= 0; i--) {
        if (amb[i].z == 1) {
            amb[i].ox = amb[i].x;
            amb[i].oy = amb[i].y;
            amb[i].x += amb[i].vx * sp;
            amb[i].y += amb[i].vy * sp;
            amb[i].vx *= 1.18f;
            amb[i].vy *= 1.18f;
        } else
            amb[i].y += amb[i].vy * sp;
        if (amb[i].y < -20.0f || amb[i].y > SW + 20.0f || amb[i].x < -20.0f || amb[i].x > SW + 20.0f) {
            amb[i] = amb[--namb];
        }
    }
}

static void scene_update(void)
{
    float h, h7, ap, sp, ch, b;
    int i, j, t, k;
    uint32_t now = plat_ms();
    vs.now = now;
    vs.t = (float)(now % 3600000u) / 1000.0f;   /* (an hour, then again: floats keep their precision) */
    vs.frame++;
    for (t = 0; t < 4; t++)
        for (k = 0; k < 5; k++)
            vs.ET[t][k] = zz_ET[t][k];
    for (k = 0; k < 3; k++)
        vs.EG[k] = zz_EG[k];
    if (zz_view.splash) {                           /* the first screen: kawaii, still */
        vs.EG[ZZ_HUMEUR] = 0.0f;
        vs.EG[ZZ_APERO] = 0.0f;
    }
    h = vs.EG[ZZ_HUMEUR];
    h7 = h * 7.0f;
    ap = vs.EG[ZZ_APERO];
    weights(h, vs.w);
    vs.euph = cl01(1.0f - h7);
    vs.rain = cl01(1.0f - zz_fabsf(h7 - 4.5f));
    if (cl01((h7 - 5.6f) / 0.6f) > vs.rain)
        vs.rain = cl01((h7 - 5.6f) / 0.6f);
    vs.dep = cl01(h7 - 5.5f);
    vs.z = zone_of(h);
    for (i = 0; i < nvq; i++) {
        if (vq[i].bet == 2)
            spawn_pop();
        else if (vq[i].bet)
            on_bet(vq[i].v, vq[i].ms);
        else
            on_hit(vq[i].tr, vq[i].v, vq[i].acc);
    }
    nvq = 0;
    vs.bet_now = (int32_t)(vs.bet.until - now) > 0 ? vs.bet.k : 0;
    vs.bp = vs.bet_now ? (float)(now - vs.bet.start) / (float)(vs.bet.until - vs.bet.start + 1u) : 0.0f;
    sp = (1.5f - vs.ET[3][ZZ_FLEMME] * 1.1f) * (1.0f + 0.4f * vs.euph) * (1.0f - 0.5f * vs.dep);
    vs.sp = sp;
    vs.vt += 0.08f * sp;
    if (vs.vt > 6283.1853f)                      /* 1000 turns: the whole multiples of vt come round */
        vs.vt -= 6283.1853f;
    amb_update(sp);
    /* the worlds: the two heaviest, cross-faded by a dither mask */
    {
        int o[4] = {0, 1, 2, 3}, a, c;
        for (a = 0; a < 4; a++)
            for (c = a + 1; c < 4; c++)
                if (vs.w[o[c]] > vs.w[o[a]]) {
                    int tt = o[a];
                    o[a] = o[c];
                    o[c] = tt;
                }
        vs.wa = o[0];
        vs.wb = o[1];
        vs.wlvl = vs.w[o[1]] > 0.03f ? iround(vs.w[o[1]] / (vs.w[o[0]] + vs.w[o[1]]) * 16.0f) : 0;
    }
    vs.fire_on = vs.w[2] > 0.01f;
    if (vs.fire_on) {
        float s = 0.55f + 0.25f * vs.ET[0][ZZ_BIDOCHE] + 0.38f * vs.fire_kick;
        fire_step(iround(36.0f * (s > 1.0f ? 1.0f : s)));
    }
    /* the herd, on the bass */
    vs.herd_n = 0;
    if (now - vs.bass_ms < 2500u && vs.bass_ms) {
        const float *m = vs.ET[2];
        static const uint32_t HC[4] = {0xff6fb5, 0x43c6ff, 0x140808, 0x9a9a9a};
        float s = 1.1f + m[ZZ_BIDOCHE] * 1.1f;
        vs.horse_x += (3.0f + vs.quake * 6.0f) * sp * (1.3f - m[ZZ_FLEMME] * 0.8f);
        if (vs.horse_x > SW + 40)
            vs.horse_x = -110.0f;
        vs.herd_n = 1 + (int)(m[ZZ_CHATOUILLE] * 2.99f);
        vs.herd_s = (int16_t)iround(s * 64.0f);
        vs.herd_c = ZH(HC[vs.z]);
        for (i = 0; i < vs.herd_n; i++) {
            vs.herd_x[i] = (int16_t)iround(vs.horse_x - (float)i * 38.0f * s * 0.6f);
            vs.herd_y[i] = (int16_t)iround(178.0f - 39.0f * s + (float)((i % 2) * 6) -
                                           zz_fabsf(zz_sinf(vs.vt * 9.0f + (float)i)) * 5.0f * (0.5f + vs.quake));
        }
    }
    /* Newton's cradle, on the beat */
    {
        float spb = 60000.0f / (70.0f + vs.EG[ZZ_CAFEINE] * 110.0f), ph;
        if (zz_view.playing) {
            float f = (float)(now - zz_view.beat_ms) / spb;
            ph = ((float)(zz_view.beat_n % 2u) + (f > 1.0f ? 1.0f : f)) * 0.5f;
        } else {
            ph = (float)now / spb * 0.5f;
            ph -= zz_floorf(ph);
        }
        vs.cradle_f = (int)(ph * ZZ_CRADLE.n) % ZZ_CRADLE.n;
    }
    /* the cockatoo: CHATOUILLE mirrors it, FLEMME slows the film */
    {
        const float *d = vs.ET[0];
        float spd = 1.5f - d[ZZ_FLEMME] * 1.1f, n = (float)ZZ_BIRD.n;
        ch = d[ZZ_CHATOUILLE];
        vs.bird_f += (float)vs.bird_dir * (0.25f + ch * 1.9f) * spd + vs.bird_kick * 2.0f * (float)vs.bird_dir;
        if (vs.bird_f >= n - 1.0f) {
            vs.bird_f = n - 1.0f;
            vs.bird_dir = -1;
        }
        if (vs.bird_f <= 0.0f) {
            vs.bird_f = 0.0f;
            vs.bird_dir = 1;
        }
        vs.bird_fi = iround(vs.bird_f);
        vs.bird_mode = ch < 0.2f ? 0 : (ch < 0.45f ? 1 : (ch < 0.7f ? 2 : (ch < 0.88f ? 3 : 4)));
        vs.bird_zoom = 1.0f + (float)iround(vs.bird_kick * 4.0f * (0.5f + d[ZZ_BIDOCHE])) / 32.0f;
        vs.bird_rot = vs.bird_mode == 3 ? vs.vt * 1.4f : 0.0f;
    }
    /* the cat: BIDOCHE its size (then Warhol), CHATOUILLE the wobble and the colour split */
    {
        const float *s = vs.ET[1];
        float amp, fq, ph;
        static const float PH[4] = {1.0f, 0.4f, -0.4f, 0.0f};
        b = s[ZZ_BIDOCHE];
        ch = s[ZZ_CHATOUILLE];
        vs.cat_size = iround(60.0f + b * 76.0f + vs.cat_pulse * 12.0f);
        vs.warhol = b > 0.74f ? (b > 0.9f ? 3 : 2) : 0;
        amp = ch * ch * 16.0f + vs.cat_pulse * 3.0f;
        fq = 0.09f + ch * 0.12f;
        ph = vs.vt * 8.0f;
        for (j = 0; j < 4; j++)
            for (i = 0; i < 64; i++)
                vs.cat_off[j][i] = (int16_t)iround(zz_sinf((float)i * 2.0f * fq + ph + PH[j]) * amp);
        vs.cat_dbl = ap > 0.08f;
        vs.cat_d = iround(ch * 7.0f + vs.cat_pulse * 4.0f);
        vs.neyes = 0;
        if (!vs.warhol) {
            static const int8_t EY[2][2] = ZZ_CAT_EYES;
            float sc = (float)vs.cat_size / 64.0f;
            for (i = 0; i < 2; i++) {
                vs.eyes[i][0] = 80.0f - (float)vs.cat_size * 0.5f + (float)EY[i][0] * sc;
                vs.eyes[i][1] = 146.0f - (float)vs.cat_size * 0.5f + (float)EY[i][1] * sc;
            }
            vs.neyes = 2;
        }
        vs.eye_fx = vs.euph > 0.4f ? 1 : (vs.w[0] > 0.3f ? 2 : 0);
        vs.laser = h > 0.62f && vs.neyes;
        vs.laser_ang = 0.32f + zz_sinf(vs.vt * 3.0f) * 0.08f + (ur() - 0.5f) * 0.05f;
    }
    /* flyers: physics, then their turned-sprite maps (after-images first) */
    naff = 0;
    {
        int gk = iround(vs.ET[3][ZZ_FLEMME] * 3.0f);
        for (i = 0; i < nfly; i++) {
            float sw;
            int hp = fly[i].hn % 12;
            fly[i].hx[hp] = fly[i].x;
            fly[i].hy[hp] = fly[i].y;
            fly[i].hr[hp] = fly[i].r;
            fly[i].hn++;
            fly[i].x += fly[i].vx * sp;
            fly[i].y += fly[i].vy * sp;
            fly[i].vy += (fly[i].sad ? 0.25f : 1.5f) * sp;
            fly[i].r += fly[i].vr * sp;
            for (j = 0; j < gk && naff < NAFF; j++) {
                int back = 1 + j * 2;
                if (back > fly[i].hn || back > 12)
                    break;
                hp = (fly[i].hn - back) % 12;
                aff_set(&affs[naff++], DRAW[fly[i].d], fly[i].hx[hp], fly[i].hy[hp], fly[i].sc, 1.0f,
                        fly[i].coin ? 0.0f : fly[i].hr[hp], F_SIL | F_CHECK, ZH(MSG[fly[i].z].ghost));
            }
            sw = fly[i].coin ? zz_cosf(vs.vt * 10.0f + (float)i) : 1.0f;
            if (naff < NAFF)
                aff_set(&affs[naff++], DRAW[fly[i].d], fly[i].x, fly[i].y, fly[i].sc, sw, fly[i].coin ? 0.0f : fly[i].r, 0, 0);
        }
        for (i = nfly - 1; i >= 0; i--)
            if (fly[i].y > SW + 60.0f) {
                for (j = i + 1; j < nfly; j++)
                    fly[j - 1] = fly[j];
                nfly--;
            }
    }
    /* the moods: confetti in euphoria, rain from melancholy down, tears when it gets low */
    if (vs.euph > 0.05f) {
        static const uint32_t CONF[7] = {0xff3d8b, 0xffe14d, 0x43ff86, 0x2a7fff, 0xff7a1a, 0xc08fff, 0xffffff};
        int n = iround(vs.euph * 4.0f);
        for (i = 0; i < n; i++) {
            if (nconf >= NCONF) {
                for (j = 1; j < nconf; j++)
                    conf[j - 1] = conf[j];
                nconf--;
            }
            conf[nconf].x = ur() * SW;
            conf[nconf].y = -4.0f;
            conf[nconf].vx = (ur() - 0.5f) * 2.0f;
            conf[nconf].vy = 2.0f + ur() * 3.0f;
            conf[nconf].c = ZH(CONF[(int)(ur() * 7.0f) % 7]);
            conf[nconf].ph = ur() * 6.0f;
            nconf++;
        }
    }
    for (i = nconf - 1; i >= 0; i--) {
        conf[i].x += conf[i].vx + zz_sinf(vs.vt * 4.0f + conf[i].ph);
        conf[i].y += conf[i].vy;
        if (conf[i].y > SW)
            conf[i] = conf[--nconf];
    }
    nrain = 0;
    if (vs.rain > 0.03f) {
        nrain = iround(70.0f * vs.rain);
        rain_c = vs.dep > 0.5f ? ZH(0xc8c8c8) : ZH(0xa8c8ff);
        for (i = 0; i < nrain; i++) {
            rain_x[i] = (int16_t)(ur() * (SW + 40));
            rain_y[i] = (int16_t)(ur() * SW);
        }
    }
    {
        float tw = vs.dep > vs.rain * 0.5f ? vs.dep : vs.rain * 0.5f;
        if (tw > 0.15f) {
            float src[4][2];
            int ns = 0;
            for (i = 0; i < vs.neyes; i++) {
                src[ns][0] = vs.eyes[i][0];
                src[ns++][1] = vs.eyes[i][1];
            }
            src[ns][0] = 154.0f;
            src[ns++][1] = 68.0f;
            src[ns][0] = 206.0f;
            src[ns++][1] = 68.0f;
            for (i = 0; i < ns; i++)
                if (ur() < tw * 0.45f) {
                    if (ntear >= NTEAR) {
                        for (j = 1; j < ntear; j++)
                            tears[j - 1] = tears[j];
                        ntear--;
                    }
                    tears[ntear].x = src[i][0] + (ur() - 0.5f) * 3.0f;
                    tears[ntear].y = src[i][1] + 4.0f;
                    tears[ntear].vy = 0.8f;
                    ntear++;
                }
        }
    }
    for (i = ntear - 1; i >= 0; i--) {
        tears[i].vy += 0.45f;
        tears[i].y += tears[i].vy;
        if (tears[i].y > SW) {
            for (j = i + 1; j < ntear; j++)
                tears[j - 1] = tears[j];
            ntear--;
        }
    }
    /* pop-ups */
    for (i = 0; i < npop; i++) {
        pop_t *p = &pops[i];
        int hp;
        if (vs.t - p->born > p->life) {
            for (j = i + 1; j < npop; j++)
                pops[j - 1] = pops[j];
            npop--;
            i--;
            continue;
        }
        hp = p->hn % 12;
        p->hx[hp] = p->x;
        p->hy[hp] = p->y;
        p->hn++;
        p->x += p->dx;
        p->y += p->dy;
        if (p->x < 0.0f || p->x > (float)(SW - p->w))
            p->dx = -p->dx;
        if (p->y < 12.0f || p->y > (float)(SW - p->h - 26))
            p->dy = -p->dy;
    }
    /* the big ZINZIN, while nothing plays */
    vs.word = zz_view.splash || (!zz_view.playing && now - zz_view.note_ms > 8000u);
    /* the marquee */
    vs.marq_w = ztext_w(&ZZ_FONT_S, MARQ[vs.z]);
    vs.marq_off = -(int)((float)vs.frame * (1.0f + vs.EG[ZZ_CAFEINE] * 4.0f)) % (vs.marq_w ? vs.marq_w : 1);
    /* the post effects: only what rows can do */
    {
        const float *bz = vs.ET[3];
        float mo = bz[ZZ_MOISI], mo2 = (vs.ET[0][ZZ_MOISI] + vs.ET[1][ZZ_MOISI] + vs.ET[2][ZZ_MOISI]) / 3.0f * 0.7f, bb;
        int bet = zz_view.splash ? 0 : vs.bet_now;
        if (mo2 > mo)
            mo = mo2;
        vs.mirror = bet == ZZ_RECULE;
        vs.sx = iround((ur() - 0.5f) * vs.shake);
        vs.slide = bet == ZZ_COULE ? iround(vs.bp * vs.bp * 90.0f) : 0;
        vs.warp = !zz_view.splash && (ap > 0.03f || vs.shake > 0.6f || vs.quake > 0.05f || vs.slide);
        vs.dy = iround(zz_sinf(vs.t * 1.6f) * ap * 5.0f + (ur() - 0.5f) * vs.shake);
        vs.qk = iround(zz_sinf(vs.t * 47.0f) * vs.quake * 7.0f);
        vs.shake *= 0.5f;
        vs.quake *= 0.6f;
        vs.glitch_n = 0;
        if (vs.glitch > 0.05f) {
            vs.glitch_n = 2 + (int)(vs.glitch * (2.0f + bz[ZZ_CHATOUILLE] * 6.0f));
            if (vs.glitch_n > 8)
                vs.glitch_n = 8;
            for (i = 0; i < vs.glitch_n; i++) {
                vs.glitch_y[i] = (int)(ur() * SW);
                vs.glitch_h[i] = 2 + (int)(ur() * 14.0f);
                vs.glitch_dx[i] = (int)((ur() - 0.5f) * 40.0f * vs.glitch);
            }
            vs.glitch *= 0.55f;
        }
        vs.mo_h = 0;
        vs.noise = 0;
        if (mo > 0.35f) {
            float bnd = vs.t * 55.0f;
            bnd -= zz_floorf(bnd / 300.0f) * 300.0f;
            vs.mo_band = iround(bnd - 30.0f);
            vs.mo_h = iround(6.0f + mo * 10.0f);
            vs.mo_dx = iround((ur() - 0.5f) * mo * 24.0f);
            vs.noise = iround((mo - 0.35f) * 500.0f);
        }
        bb = (mo - 0.08f) / 0.92f;
        if (bb < 0.0f)
            bb = 0.0f;
        if (bet == ZZ_ECRASE && bb < 0.85f)
            bb = 0.85f;
        vs.bl = 1;
        vs.lv = 0;
        if (bb > 0.02f) {
            vs.bl = 1 + (int)(bb * bb * 8.0f);
            vs.lv = iround(24.0f - bb * 21.0f);
            if (vs.lv < 2)
                vs.lv = 2;
        }
        vs.bw_th = iround(vs.dep * 16.0f);
        vs.mul = bet == ZZ_PLONGE ? (vs.bp < 0.5f ? 1 : 2) : 0;
        vs.dark = bet == ZZ_COULE && vs.bp > 0.5f ? (vs.bp > 0.8f ? 2 : 1) : 0;
        vs.fa = vs.flash > 0.6f ? 2 : (vs.flash > 0.25f ? 1 : 0);
        {
            static const uint32_t FC[4] = {0xffffff, 0x78dcff, 0xff1e0a, 0xc8c8c8};
            vs.flash_c = ZH(FC[vs.flash_z & 3]);
        }
        vs.flash *= 0.55f;
        vs.seed = xr32();
    }
    vs.cat_pulse *= 0.6f;
    vs.bird_kick *= 0.5f;
    vs.fire_kick *= 0.7f;
}

/* ------------------------------------------------------------------ one row of the scene */
static void world_row(int w, int sy, uint16_t *L)
{
    int i;
    if (w == 0) {                                     /* kawaii: the meadow in pink, a rainbow, hearts going up */
        static const uint32_t RB[6] = {0xff8fc8, 0xffc48f, 0xfff38f, 0x8fffb6, 0x8fd8ff, 0xc08fff};
        img_line(L, sy, &ZZ_MEADOW, 0, pal_meadow_k, sy >> 1, 0, SW, 0, 120, F_OPAQUE, 0);
        if (sy <= 176) {
            int dy = 176 - sy;
            for (i = 0; i < 6; i++) {
                int ro = 126 - i * 5 + 2, ri = ro - 5, xo, xi;
                if (dy > ro)
                    continue;
                xo = isqrt(ro * ro - dy * dy);
                xi = dy < ri ? isqrt(ri * ri - dy * dy) : 0;
                hspan(L, 120 - xo, 120 - xi, ZH(RB[i]));
                hspan(L, 120 + xi, 120 + xo, ZH(RB[i]));
            }
        }
        for (i = 0; i < namb; i++)
            if (amb[i].z == 0)
                spr_row(L, sy, DRAW[amb[i].k], 0, amb[i].x + zz_sinf(vs.vt * 2.0f + amb[i].ph) * 6.0f, amb[i].y,
                        amb[i].ph > 3.0f ? 2.0f : 1.0f, 0, 0);
    } else if (w == 1) {                              /* cosmos: stars scrolling, a striped sun, a grid to the horizon */
        const int hz = 140;
        int o = (int)(vs.vt * 12.0f) % SW;
        if (sy < hz) {
            img_line(L, sy, &ZZ_SPACE, 0, ZZ_SPACE.pal, sy >> 1, -o, SW, 0, 120, F_OPAQUE, 0);
            img_line(L, sy, &ZZ_SPACE, 0, ZZ_SPACE.pal, sy >> 1, SW - o, SW, 0, 120, F_OPAQUE, 0);
            if (sy >= hz - 52) {
                int dy = hz - sy, hw = isqrt(52 * 52 - dy * dy);
                hspan(L, 120 - hw, 120 + hw, mix565(ZH(0xfff36b), ZH(0xff3d8b), (sy - (hz - 56)) * 256 / 56));
            }
            for (i = 0; i < 6; i++) {
                int yy = hz - 6 - i * 8, hh = 1 + (int)((float)i * 0.6f + 0.5f);
                rect_row(L, sy, 60, yy, 120, hh, ZH(0x14002a));
            }
        } else {
            float sc = vs.vt * 1.4f;
            hspan(L, 0, SW, ZH(0x14002a));
            sc -= zz_floorf(sc);
            for (i = 0; i < 12; i++) {
                float zz = ((float)i + sc) / 12.0f;
                int yy = hz + (int)(zz_powf(zz, 2.2f) * (float)(SW - hz));
                if (yy == sy)
                    hspan(L, 0, SW, ZH(0xff3df0));
            }
            for (i = -9; i <= 9; i++)
                seg_row(L, sy, 120.0f + (float)i * 7.0f, (float)hz, 120.0f + (float)i * 48.0f, (float)SW, 0.0f,
                        ZH(0xff3df0));
        }
        for (i = 0; i < namb; i++)
            if (amb[i].z == 1)
                seg_row(L, sy, amb[i].ox, amb[i].oy, amb[i].x, amb[i].y, 0.0f, C_WHITE0);
    } else if (w == 2) {                              /* hell: a red sky, a red moon, the Doom fire */
        int fy = sy - 84;
        hspan(L, 0, SW, mix565(ZH(0x120000), ZH(0x6a0800), sy >= 170 ? 256 : sy * 256 / 170));
        img_row(L, sy, &ZZ_MOON, 0, pal_moon_r, 150, 22, 78, 78, 0, 0, 56, 56, 0, 0);
        if (fy >= 0) {
            const uint8_t *fr = fire + (fy / 3) * 80;
            int x;
            for (x = 0; x < SW; x++) {
                int v = fr[x / 3];
                if (v >= 2)
                    L[x] = pal_fire[v > 36 ? 36 : v];
            }
        }
        for (i = 0; i < namb; i++) {
            if (amb[i].z == 2)
                rect_row(L, sy, iround(amb[i].x + zz_sinf(vs.vt * 3.0f + amb[i].ph) * 4.0f), iround(amb[i].y), 2, 2,
                         ((int)(amb[i].ph * 10.0f) % 2) ? ZH(0xffb000) : ZH(0xff4a1a));
            else if (amb[i].z == 3)
                spr_row(L, sy, &ZZ_S_SKULL, 0, amb[i].x + zz_sinf(vs.vt * 2.0f + amb[i].ph) * 8.0f, amb[i].y, 2.0f, 0, 0);
        }
    } else {                                          /* grey: the meadow without colour, clouds, broken hearts */
        img_line(L, sy, &ZZ_MEADOW, 0, pal_meadow_g, sy >> 1, 0, SW, 0, 120, F_OPAQUE, 0);
        for (i = 0; i < 3; i++) {
            float x = vs.vt * 6.0f + (float)i * 90.0f;
            x -= zz_floorf(x / (float)(SW + 60)) * (float)(SW + 60);
            spr_row(L, sy, &ZZ_S_RAIN, 0, x - 30.0f, 22.0f + (float)i * 14.0f, 3.0f, 0, 0);
        }
        for (i = 0; i < namb; i++)
            if (amb[i].z == 4)
                spr_row(L, sy, &ZZ_S_BROKEN, 0, amb[i].x + zz_sinf(vs.vt + amb[i].ph) * 4.0f, amb[i].y,
                        amb[i].ph > 4.0f ? 2.0f : 1.0f, 0, 0);
    }
}

static uint16_t LB[SW], LW[SW];

static void cradle_row(int sy, uint16_t *L)
{
    const int x = 4, y = 16, w = 100, h = 88;
    if (sy < y || sy >= y + h)
        return;
    bevel_row(L, sy, x, y, w, h, 0);
    title_row(L, sy, x + 2, y + 2, w - 4, MSG[vs.z].bar[0], MSG[vs.z].bar[1], "tempo.gif");
    img_row(L, sy, &ZZ_CRADLE, vs.cradle_f, ZZ_CRADLE.pal, x + 2, y + 15, 96, 72, 0, 0, 48, 36, F_OPAQUE, 0);
}

/* the webcam: the cockatoo, CHATOUILLE turns it into mirrors and a kaleidoscope */
static void bird_row(int sy, uint16_t *L)
{
    const int bx = 124, by = 16, bs = 112, BW = 56;
    const uint16_t *pal = vs.z == 2 ? pal_bird_doom : ZZ_BIRD.pal;
    const uint16_t fc = ZH(TRK_COL[0]);
    int stride = (BW + 1) >> 1, x, m = iround(BW * 0.18f);
    const uint8_t *fr = ZZ_BIRD.px + (uint32_t)vs.bird_fi * (uint32_t)stride * BW;
    float co, si, iz, fy;
    int32_t u, v, du, dv;
    if (sy < by || sy >= by + bs)
        return;
    iz = 1.0f / vs.bird_zoom;
    co = zz_cosf(vs.bird_rot) * iz;
    si = zz_sinf(vs.bird_rot) * iz;
    fy = (float)(sy - by) - bs * 0.5f + 0.5f;
    /* box pixel (x, sy) -> picture pixel: undo the zoom and the turn, around the centre */
    du = (int32_t)(co * 65536.0f);
    dv = (int32_t)(-si * 65536.0f);
    u = (int32_t)(((-bs * 0.5f + 0.5f) * co + fy * si + bs * 0.5f) * 65536.0f);
    v = (int32_t)((-(-bs * 0.5f + 0.5f) * si + fy * co + bs * 0.5f) * 65536.0f);
    for (x = bx; x < bx + bs; x++, u += du, v += dv) {
        int px = u >> 16, py = v >> 16, sx, sy2, i;
        if ((unsigned)px >= (unsigned)bs || (unsigned)py >= (unsigned)bs) {
            L[x] = C_BLACK0;
            continue;
        }
        switch (vs.bird_mode) {
        case 0:
            sx = px >> 1;
            sy2 = py >> 1;
            break;
        case 1:
            sx = (px < 56 ? px : 111 - px) >> 1;
            sy2 = py >> 1;
            break;
        case 2:
        case 3:
            sx = 14 + ((px < 56 ? px : 111 - px) >> 1);
            sy2 = 14 + ((py < 56 ? py : 111 - py) >> 1);
            break;
        default: {
            int lx = px % 28, ly = py % 28;
            if ((px / 28) & 1)
                lx = 27 - lx;
            if ((py / 28) & 1)
                ly = 27 - ly;
            sx = m + lx * (BW - 2 * m) / 28;
            sy2 = m + ly * (BW - 2 * m) / 28;
        }
        }
        i = (fr[sy2 * stride + (sx >> 1)] >> ((sx & 1) ? 0 : 4)) & 15;
        L[x] = pal[i];
    }
    if (sy < by + 3 || sy >= by + bs - 3)
        hspan(L, bx, bx + bs, fc);
    hspan(L, bx, bx + 3, fc);
    hspan(L, bx + bs - 3, bx + bs, fc);
    if ((vs.frame >> 2) % 2 == 0)
        rect_row(L, sy, bx + 7, by + 7, 6, 6, ZH(0xff1a1a));
    text_row(L, sy, &ZZ_FONT_S, bx + 17, by + 7, "REC", C_BLACK0);
    text_row(L, sy, &ZZ_FONT_S, bx + 16, by + 6, "REC", C_WHITE0);
    {
        char tc[10];
        uint32_t s = vs.now / 1000u;
        tc[0] = '0';
        tc[1] = '0';
        tc[2] = ':';
        tc[3] = '0';
        tc[4] = (char)('0' + (s / 60u) % 10u);
        tc[5] = ':';
        tc[6] = (char)('0' + (s % 60u) / 10u);
        tc[7] = (char)('0' + s % 10u);
        tc[8] = 0;
        text_row(L, sy, &ZZ_FONT_S, bx + 7, by + bs - 14, tc, C_BLACK0);
        text_row(L, sy, &ZZ_FONT_S, bx + 6, by + bs - 15, tc, C_WHITE0);
    }
}

static void herd_row(int sy, uint16_t *L)
{
    int i, s = vs.herd_s, w = (48 * s) >> 6, h = (39 * s) >> 6;
    for (i = 0; i < vs.herd_n; i++)
        img_row(L, sy, &ZZ_HORSE, 0, ZZ_HORSE.pal, vs.herd_x[i], vs.herd_y[i], w, h, 0, 0, 48, 39, F_SIL, vs.herd_c);
}

/* the cat, in slices that sway (and two colour ghosts, and a checkered double when APÉRO is up) */
static void cat_slices(int sy, uint16_t *L, const uint16_t *pal, int fl, float cx, float cy, int size, int layer)
{
    float top = cy - (float)size * 0.5f, rh = (float)size / 64.0f;
    int r = (int)(((float)sy - top) / rh);
    if ((float)sy < top || r < 0 || r >= 64)
        return;
    img_line(L, sy, &ZZ_CAT, 0, pal, r, iround(cx - (float)size * 0.5f) + vs.cat_off[layer][r], size, 0, 64, fl, 0);
}
static void cat_row(int sy, uint16_t *L)
{
    const float cx = 80.0f, cy = 146.0f;
    int i, j;
    if (vs.warhol) {
        int n = vs.warhol, area = 140, x0 = (int)cx - area / 2 + 6, y0 = (int)cy - area / 2 - 4;
        float cs = (float)area / (float)n;
        if (sy >= y0 && sy < y0 + area) {
            j = (int)((float)(sy - y0) / cs);
            for (i = 0; i < n; i++) {
                int jit = iround(zz_sinf(vs.vt * 6.0f + (float)(i * 2 + j)) * vs.ET[1][ZZ_CHATOUILLE] * 3.0f),
                    cy0 = iround((float)y0 + (float)j * cs), ch = (int)(cs + 0.999f);
                img_row(L, sy, &ZZ_CAT, 0, pal_warhol[(i + j * n + (int)((vs.frame >> 1) % 4u)) % 4],
                        iround((float)x0 + (float)i * cs) + jit, cy0, ch, ch, 0, 0, 64, 64, F_OPAQUE, 0);
            }
            for (i = 0; i <= n; i++)
                hspan(L, iround((float)x0 + (float)i * cs) - 1, iround((float)x0 + (float)i * cs) + 1, C_BLACK0);
            for (i = 0; i <= n; i++) {
                int ly = iround((float)y0 + (float)i * cs) - 1;
                if (sy == ly || sy == ly + 1)
                    hspan(L, x0, x0 + area, C_BLACK0);
            }
        }
    } else {
        int size = vs.cat_size, d = vs.cat_d;
        if (vs.cat_dbl) {
            float a = vs.EG[ZZ_APERO];
            cat_slices(sy, L, ZZ_CAT.pal, F_CHECK, cx + (float)iround(a * 12.0f), cy + (float)iround(a * 5.0f), size, 0);
        }
        if (d > 0) {
            cat_slices(sy, L, pal_cat_r, 0, cx - (float)d, cy, size, 1);
            cat_slices(sy, L, pal_cat_c, 0, cx + (float)d, cy, size, 2);
        }
        cat_slices(sy, L, ZZ_CAT.pal, 0, cx, cy, size, 3);
    }
    for (i = 0; i < vs.neyes; i++) {
        float ex = vs.eyes[i][0], ey = vs.eyes[i][1];
        if (vs.eye_fx == 1)
            spr_row(L, sy, &ZZ_S_HEART, 0, ex, ey, 1.0f + (float)((vs.frame >> 1) % 2u), 0, 0);
        else if (vs.eye_fx == 2)
            spr_row(L, sy, &ZZ_S_STAR, 0, ex + 4.0f, ey - 4.0f, (vs.frame + (uint32_t)ex) % 6u < 3u ? 2.0f : 1.0f, 0, 0);
    }
    if (vs.laser)
        for (i = 0; i < vs.neyes; i++) {
            float ex = vs.eyes[i][0], ey = vs.eyes[i][1], x2 = ex + zz_cosf(vs.laser_ang) * 320.0f,
                  y2 = ey + zz_sinf(vs.laser_ang) * 320.0f;
            seg_row(L, sy, ex, ey, x2, y2, 6.0f, ZH(0xff1a1a));
            seg_row(L, sy, ex, ey, x2, y2, 2.0f, ZH(0xffd0c8));
            rect_row(L, sy, iround(ex) - 4, iround(ey) - 4, 8, 8, ZH(0xff4a3a));
        }
}

static void moodfx_row(int sy, uint16_t *L)
{
    int i;
    for (i = 0; i < nconf; i++)
        rect_row(L, sy, iround(conf[i].x), iround(conf[i].y), conf[i].ph > 3.0f ? 3 : 2, 2, conf[i].c);
    for (i = 0; i < nrain; i++)
        if (sy >= rain_y[i] && sy <= rain_y[i] + 8)
            pset(L, rain_x[i] - (sy - rain_y[i]) * 3 / 8, rain_c);
    for (i = 0; i < ntear; i++)
        spr_row(L, sy, &ZZ_S_TEAR, 0, tears[i].x, tears[i].y, 2.0f, 0, 0);
}

static void pops_row(int sy, uint16_t *L)
{
    int i, j, k = iround(vs.ET[3][ZZ_FLEMME] * 5.0f);
    for (i = 0; i < npop; i++) {
        const pop_t *p = &pops[i];
        const msg_t *m = &MSG[p->z];
        int x, y;
        for (j = k; j >= 1; j--) {        /* after-images: where it was */
            int back = j * 2 + 1, hp, hx, hy;
            if (back > p->hn || back > 12)
                continue;
            hp = (p->hn - back) % 12;
            hx = iround(p->hx[hp]);
            hy = iround(p->hy[hp]);
            if (sy < hy || sy >= hy + p->h)
                continue;
            bevel_row(L, sy, hx, hy, p->w, p->h, 0);
            title_row(L, sy, hx + 2, hy + 2, p->w - 4, m->bar[0], m->bar[1], p->title);
        }
        x = iround(p->x);
        y = iround(p->y);
        if (sy < y || sy >= y + p->h)
            continue;
        bevel_row(L, sy, x, y, p->w, p->h, 0);
        title_row(L, sy, x + 2, y + 2, p->w - 4, m->bar[0], m->bar[1], p->title);
        spr_row(L, sy, DRAW[p->icon], 0, (float)(x + 16), (float)(y + 32), 2.0f, 0, 0);
        text_row(L, sy, &ZZ_FONT_T, x + 32, y + 17, p->l1, C_BLACK0);
        text_row(L, sy, &ZZ_FONT_T, x + 32, y + 29, p->l2, C_BLACK0);
        bevel_row(L, sy, x + p->w / 2 - 16, y + p->h - 18, 32, 14, 0);
        text_row(L, sy, &ZZ_FONT_T, x + p->w / 2 - 6, y + p->h - 17, "OK", C_BLACK0);
    }
}

/* the big ZINZIN, WordArt style */
static void big_glyph(uint16_t *L, int sy, char ch, int x, int y, int sc, uint16_t c, int grad_y)
{
    int g = zz_glyph((uint32_t)(uint8_t)ch), r = (sy - y) / sc, b;
    uint16_t bits;
    if (g < 0 || sy < y || r >= 8)
        return;
    bits = ZZ_FONT_S.rows[g * 8 + r];
    if (grad_y) {              /* white, sky, then pink to yellow */
        int tt = (sy - grad_y) * 256 / 28;
        if (tt < 0)
            tt = 0;
        if (tt > 255)
            tt = 255;
        c = tt < 115 ? mix565(C_WHITE0, ZH(0x8fd8ff), tt * 256 / 115)
                     : (tt < 128 ? ZH(0x8fd8ff) : mix565(ZH(0xff3d8b), ZH(0xfff36b), (tt - 128) * 2));
    }
    for (b = 0; b < 8; b++)
        if (bits & (0x8000u >> b))
            hspan(L, x + b * sc, x + b * sc + sc, c);
}
static void word_row(int sy, uint16_t *L)
{
    static const char TXT[] = "ZINZIN";
    int i, d;
    if (sy >= 80 && sy < 160)
        for (i = 0; i < 6; i++) {
            int cx = 48 + 24 * i + 12, cy = iround(118.0f + zz_sinf(vs.t * 4.0f + (float)i * 0.7f) * 9.0f), x = cx - 12,
                y = cy - 12;
            for (d = 6; d > 0; d--)
                big_glyph(L, sy, TXT[i], x + d, y + d, 3, d > 3 ? ZH(0x1d1730) : ZH(0x4a35a6), 0);
            big_glyph(L, sy, TXT[i], x - 1, y, 3, ZH(0x1d1730), 0);
            big_glyph(L, sy, TXT[i], x + 1, y, 3, ZH(0x1d1730), 0);
            big_glyph(L, sy, TXT[i], x, y - 1, 3, ZH(0x1d1730), 0);
            big_glyph(L, sy, TXT[i], x, y + 1, 3, ZH(0x1d1730), 0);
            big_glyph(L, sy, TXT[i], x, y, 3, 0, cy - 14);
        }
    if (!zz_view.splash && (vs.frame >> 3) % 2 == 0 && sy >= 190 && sy < 204) {
        const char *s = "> APPUIE SUR PLAY";
        hspan(L, 48, 192, C_BLACK0);
        text_row(L, sy, &ZZ_FONT_S, 120 - ztext_w(&ZZ_FONT_S, s) / 2, 193, s, ZH(0xfff36b));
    }
}

/* the HUD: the marquee and the visit counter on top; the selected track's 16 steps at the bottom; a knob's window */
static void num_text(uint32_t v, int digits, char *t)
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        t[i] = (char)('0' + v % 10u);
        v /= 10u;
    }
    t[digits] = 0;
}
static void hud_row(int sy, uint16_t *L)
{
    static const uint32_t TOP[4] = {0xff6fb5, 0x000080, 0x3a0000, 0x2a2a2a}, TXT[4] = {0xfff36b, 0x43ff86, 0xff3a2a, 0xbdbdbd};
    int z = vs.z, i;
    if (sy < 12) {
        char t[8];
        hspan(L, 0, SW, ZH(TOP[z]));
        text_row(L, sy, &ZZ_FONT_S, vs.marq_off, 2, MARQ[z], ZH(TXT[z]));
        text_row(L, sy, &ZZ_FONT_S, vs.marq_off + vs.marq_w, 2, MARQ[z], ZH(TXT[z]));
        hspan(L, SW - 58, SW, C_BLACK0);
        num_text(vs.hits % 1000000u, 6, t);
        text_row(L, sy, &ZZ_FONT_S, SW - 54, 2, t, ZH(0x43ff86));
        return;
    }
    if (sy >= SW - 17 && !zz_view.splash) {
        const int y0 = SW - 17, cw = 11, x0 = 56;
        int sel = zz_view.sel;
        uint16_t tc = ZH(TRK_COL[sel]);
        const char *name = zz_view.step_mode && ((vs.frame >> 2) % 2u) ? "PAS" : TRK_NAME[sel];
        hspan(L, 0, SW, C_BLACK0);
        rect_row(L, sy, 1, y0 + 1, 52, 15, zz_mute[sel] ? ZH(0x3a3a3a) : tc);
        text_row(L, sy, &ZZ_FONT_T, 27 - ztext_w(&ZZ_FONT_T, name) / 2, y0 + 3, name, zz_mute[sel] ? tc : C_BLACK0);
        if (zz_view.rec && (vs.frame >> 2) % 2u == 0)
            rect_row(L, sy, 3, y0 + 3, 4, 4, ZH(0xff1a1a));
        if (sy == y0)
            return;
        for (i = 0; i < 16; i++) {
            int x = x0 + i * cw, on = i == zz_view.cur_step && zz_view.playing, r = sy - y0;
            hspan(L, x, x + cw - 1, on ? C_WHITE0 : (i % 4 == 0 ? ZH(0x3a3354) : ZH(0x241f36)));
            if (r < 1 || r > 15)
                continue;
            if (sel == 0) {
                int l;
                for (l = 0; l < 4; l++) {
                    int v = zz_view.drum[l][i], ly = y0 + 2 + (l * 7) / 2;
                    if (v && sy >= ly && sy < ly + 3) {
                        uint16_t c = v == 2 ? C_WHITE0 : tc;
                        if (zz_view.step_mode && l != zz_view.lane)
                            c = v == 2 ? ZH(0x8a8296) : ZH(0x7a4a2a);
                        hspan(L, x + 1, x + cw - 2, c);
                    }
                }
            } else if (sel == 3) {
                int k = zz_view.bz[i];
                if (k) {
                    char t[2];
                    t[0] = (char)('0' + k);
                    t[1] = 0;
                    rect_row(L, sy, x + 1, y0 + 2, cw - 3, 13, tc);
                    text_row(L, sy, &ZZ_FONT_T, x + 2, y0 + 3, t, C_BLACK0);
                }
            } else {
                int n = sel == 1 ? zz_pat.syn_n[i] : zz_pat.bas_n[i], a = sel == 1 ? zz_view.syn_a[i] : zz_view.bas_a[i];
                if (n >= 0) {
                    int lo = sel == 1 ? 40 : 20, hi = sel == 1 ? 88 : 60, hg = iround((float)(n - lo) / (float)(hi - lo) * 13.0f);
                    if (hg < 2)
                        hg = 2;
                    if (hg > 13)
                        hg = 13;
                    rect_row(L, sy, x + 1, y0 + 15 - hg, cw - 3, hg, a ? C_WHITE0 : tc);
                }
            }
        }
        return;
    }
    /* a knob's window ("Copie de ..."), or a message */
    {
        const int x = 26, y = 166, w = 188, h = 50;
        int shown = (int32_t)(vs.say.until - vs.now) > 0 ? 2 : (vs.prog.t && vs.now - vs.prog.t < 1300u ? 1 : 0);
        static const uint32_t PB[4] = {0xff3d8b, 0x000080, 0xc8102e, 0x6a6a6a};
        if (!shown || zz_view.splash || sy < y || sy >= y + h)
            return;
        bevel_row(L, sy, x, y, w, h, 0);
        if (shown == 1) {
            char title[32] = "Copie de ", val[28];
            const char *f = MACRO_FILE[vs.prog.k];
            int n = 9, nb = iround(vs.prog.v * 22.0f);
            float bw = (float)(w - 14) / 22.0f;
            while (*f && n < 31)
                title[n++] = *f++;
            title[n] = 0;
            title_row(L, sy, x + 2, y + 2, w - 4, MSG[z].bar[0], MSG[z].bar[1], title);
            bevel_row(L, sy, x + 5, y + 17, w - 10, 13, 1);
            if (sy >= y + 19 && sy < y + 28)
                for (i = 0; i < nb; i++)
                    hspan(L, iround((float)(x + 7) + (float)i * bw), iround((float)(x + 7) + (float)i * bw) +
                          (iround(bw) - 1 < 1 ? 1 : iround(bw) - 1), vs.prog.tr >= 0 ? ZH(TRK_COL[vs.prog.tr]) : ZH(PB[z]));
            zz_value_text(vs.prog.k, vs.prog.v, val);
            if (vs.prog.tr >= 0) {
                char t2[40];
                const char *a = TRK_NAME[vs.prog.tr];
                int m = 0;
                while (*a)
                    t2[m++] = *a++;
                t2[m++] = ' ';
                a = val;
                while (*a && m < 39)
                    t2[m++] = *a++;
                t2[m] = 0;
                text_row(L, sy, &ZZ_FONT_T, x + 6, y + 34, t2, C_BLACK0);
            } else
                text_row(L, sy, &ZZ_FONT_T, x + 6, y + 34, val, C_BLACK0);
        } else {
            title_row(L, sy, x + 2, y + 2, w - 4, MSG[z].bar[0], MSG[z].bar[1], vs.say.a);
            text_row(L, sy, &ZZ_FONT_B, x + 8, y + 22, vs.say.b, C_BLACK0);
        }
    }
}

static void scene_row(int sy, uint16_t *L)
{
    int i;
    world_row(vs.wa, sy, L);
    if (vs.wlvl > 0) {                          /* the second world through a dither mask: no blend */
        const uint8_t *by = BAYER + ((sy & 3) << 2);
        int lvl = vs.wlvl;
        world_row(vs.wb, sy, LW);
        for (i = 0; i < SW; i++)
            if (by[i & 3] < lvl)
                L[i] = LW[i];
    }
    if (zz_view.splash) {
        word_row(sy, L);
        hud_row(sy, L);
        return;
    }
    herd_row(sy, L);
    cradle_row(sy, L);
    bird_row(sy, L);
    cat_row(sy, L);
    for (i = 0; i < naff; i++)
        aff_row(L, sy, &affs[i]);
    moodfx_row(sy, L);
    pops_row(sy, L);
    if (vs.word)
        word_row(sy, L);
    hud_row(sy, L);
}

/* ------------------------------------------------------------------ the post effects, row by row */
static inline uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
static int cached_sy = -1;
static uint32_t cached_frame;

/* output row y -> its pixels (byte-swapped for the panel) */
static void post_row(int y, uint16_t *out)
{
    int yq = vs.bl > 1 ? (y / vs.bl) * vs.bl : y, sy = yq, dx = 0, x, i;
    const uint8_t *by = BAYER + ((y & 3) << 2);
    if (vs.warp) {
        sy = yq - vs.dy - (yq > 120 ? vs.qk : 0) - vs.slide;
        dx = iround(zz_sinf((float)(yq & ~1) * 0.045f + vs.t * 2.3f) * vs.EG[ZZ_APERO] * 9.0f) + vs.sx;
    }
    for (i = 0; i < vs.glitch_n; i++)
        if (yq >= vs.glitch_y[i] && yq < vs.glitch_y[i] + vs.glitch_h[i])
            dx += vs.glitch_dx[i];
    if (vs.mo_h && yq >= vs.mo_band && yq < vs.mo_band + vs.mo_h)
        dx += vs.mo_dx;
    if (sy < 0 || sy >= SW) {
        for (x = 0; x < SW; x++)
            out[x] = 0;
        return;
    }
    if (sy != cached_sy || cached_frame != vs.frame) {
        scene_row(sy, LB);
        if (vs.noise) {                          /* MOISI: snow */
            uint32_t hsh = hash32(vs.seed ^ (uint32_t)sy * 2654435761u);
            int n = vs.noise / SW + ((int)(hsh & 255u) < (vs.noise % SW) * 256 / SW ? 1 : 0);
            for (i = 0; i < n; i++) {
                hsh = hash32(hsh + (uint32_t)i);
                LB[hsh % SW] = C_WHITE0;
            }
        }
        cached_sy = sy;
        cached_frame = vs.frame;
    }
    for (x = 0; x < SW; x++) {
        int xs = (vs.bl > 1 ? (x / vs.bl) * vs.bl : x) - dx;
        uint16_t c;
        if (vs.mirror)
            xs = SW - 1 - xs;
        c = (unsigned)xs < SW ? LB[xs] : 0;
        if (vs.lv) {                             /* ÉCRASE, MOISI: fewer colours, ordered dither */
            int step = 255 / (vs.lv - 1), bx = x / vs.bl, byy = y / vs.bl,
                th = ((int)BAYER[(bx & 3) + ((byy & 3) << 2)] - 8) * step / 16, r = c_r(c) + th, g = c_g(c) + th, b = c_b(c) + th;
            r = (r + step / 2) / step * step;
            g = (g + step / 2) / step * step;
            b = (b + step / 2) / step * step;
            c = c565(r, g, b);
        }
        if (vs.bw_th && by[x & 3] < vs.bw_th) {  /* the low mood: grey, through the dither */
            int l = (c_r(c) * 77 + c_g(c) * 151 + c_b(c) * 28) >> 8;
            c = c565(l, l, l);
        }
        if (vs.mul)                              /* PLONGE: under water */
            c = vs.mul == 1 ? c565(c_r(c) >> 1, c_g(c) >> 1, c_b(c)) : c565(c_r(c) * 3 >> 4, c_g(c) * 3 >> 4, c_b(c) * 5 >> 3);
        if (vs.dark)
            c = vs.dark == 2 ? (uint16_t)((c >> 1) & 0x7BEFu) : (uint16_t)(c - ((c >> 2) & 0x39E7u));
        if (vs.fa)
            c = vs.fa == 2 ? (uint16_t)(((c >> 1) & 0x7BEFu) + ((vs.flash_c >> 1) & 0x7BEFu))
                           : (uint16_t)(c - ((c >> 2) & 0x39E7u) + ((vs.flash_c >> 2) & 0x39E7u));
        out[x] = (uint16_t)((c >> 8) | (c << 8));
    }
}

/* ------------------------------------------------------------------ the interface (zz_draw.h) */
static int ready;
void zz_draw_init(void)
{
    if (!ready) {
        palettes_init();
        ready = 1;
    }
}

void zz_draw_start(void)
{
    int i;
    if (vs.hits)
        return;
    vs.hits = 1337;
    vs.bird_dir = 1;
    vs.horse_x = -110.0f;
    for (i = 0; i < 40; i++)
        fire_step(30);
    spawn_pop();
}

int zz_frame_start(void)
{
    static int odd;
    uint32_t now = plat_ms();
    int bet = (int32_t)(vs.bet.until - now) > 0 ? vs.bet.k : 0;
    zz_draw_init();
    /* FIGE: the panel keeps its picture when nothing is sent; BÉGAIE: every other frame is not */
    if (!zz_view.splash && bet == ZZ_FIGE)
        return 0;
    if (!zz_view.splash && bet == ZZ_BEGAIE && (odd ^= 1))
        return 0;
    scene_update();
    return 1;
}

void zz_rows(int y0, int n, uint16_t *dst)
{
    int r;
    for (r = 0; r < n; r++)
        post_row(y0 + r, dst + r * SW);
}

void zz_vis_hit(int tr, int v, int acc) { vis_hit(tr, v, acc); }
void zz_vis_bet(int k, int ms) { vis_bet(k, ms); }
void zz_vis_pop(void) { vis_pop(); }

void zz_show_knob(int k, int tr, float v)
{
    vs.prog.k = k;
    vs.prog.tr = tr;
    vs.prog.v = v;
    vs.prog.t = plat_ms();
    vs.say.until = 0;
}

static void copy_text(char *d, const char *s, int max)
{
    int n = 0;
    while (s && *s && n < max - 1)
        d[n++] = *s++;
    d[n] = 0;
}
void zz_show_say(const char *title, const char *text)
{
    copy_text(vs.say.a, title, 28);
    copy_text(vs.say.b, text, 28);
    vs.say.until = plat_ms() + 1300u;
}

static void cat_text(char *d, int max, const char *s)
{
    int n = 0;
    while (d[n])
        n++;
    while (*s && n < max - 1)
        d[n++] = *s++;
    d[n] = 0;
}
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
/* a knob's value in words: the mood, coffees, glasses, a percentage */
void zz_value_text(int k, float v, char *t)
{
    static const char *const MOOD[7] = {"euphorie", "joie", "insouciance", "rêverie", "mélancolie", "colère", "déprime"};
    char n[12];
    t[0] = 0;
    if (k == 5) {
        int m = (int)(v * 7.0f);
        cat_text(t, 28, MOOD[m < 0 ? 0 : (m > 6 ? 6 : m)]);
    } else if (k == 7) {
        int c = 1 + iround(v * 8.0f);
        int_text(c, n);
        cat_text(t, 28, n);
        cat_text(t, 28, c > 1 ? " cafés - " : " café - ");
        int_text(iround(70.0f + v * 110.0f), n);
        cat_text(t, 28, n);
        cat_text(t, 28, " bpm");
    } else if (k == 6) {
        int c = iround(v * 6.0f);
        if (!c)
            cat_text(t, 28, "à jeun");
        else {
            int_text(c, n);
            cat_text(t, 28, n);
            cat_text(t, 28, c > 1 ? " verres" : " verre");
        }
    } else {
        int_text(iround(v * 100.0f), n);
        cat_text(t, 28, n);
        cat_text(t, 28, " %");
    }
}

int zz_draw_count(int what) { return what ? nfly : npop; }
