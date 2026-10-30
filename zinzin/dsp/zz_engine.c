/* ZINZIN's sound engine (zz_engine.h). Single precision, no libm (zz_math.h), 44.1 kHz.
 *
 * Per track the same four macros: BIDOCHE (weight), CHATOUILLE (character, with notches that switch it at once),
 * MOISI (rot), FLEMME (length and space); BOUCAN its level. For everyone: HUMEUR (seven moods, euphoria to the low
 * one), APÉRO (every macro drifts on its own), CAFÉINE (tempo, rolls). Everything control-rate is worked out every
 * 64 samples (update); the samples only run the voices and the effects.
 *
 * Memory (the FM-1 has ~430 KiB of RAM and no external RAM): every delay line is int16 in the pool. 1 s of echo,
 * an 8-line room, the chorus, the tape's wobble and BAZAR's 0.6 s of memory: zz_ram_bytes(). */
#include "zz_engine.h"
#include "zz_math.h"
#include "zz_tables.h"

#ifdef PLAT_HOST
#define ZZ_POOL
#elif defined(ZZ_LIBM)
#define ZZ_POOL
#else
#define ZZ_POOL __attribute__((section(".pool")))
#endif

#define SR ZZ_SR
#define CTL 64

volatile float zz_T[ZZ_NTRK][ZZ_NTK] = {
    {0.55f, 0.25f, 0.0f, 0.30f, 0.80f},
    {0.35f, 0.30f, 0.0f, 0.40f, 0.65f},
    {0.60f, 0.35f, 0.0f, 0.25f, 0.75f},
    {0.50f, 0.40f, 0.0f, 0.30f, 0.75f}};
volatile float zz_G[ZZ_NG] = {0.12f, 0.15f, 0.45f};
volatile float zz_ET[ZZ_NTRK][ZZ_NTK];
volatile float zz_EG[ZZ_NG];
volatile uint8_t zz_mute[ZZ_NTRK];
zz_pat_t zz_pat;

/* ------------------------------------------------------------------ buffers (int16, in the pool) */
#define DL_N 44100                /* the echo: 1 s, mono; its two taps make it ping-pong */
#define CH_N 2048                 /* chorus */
#define WOW_N 1024                /* the tape's wobble */
#define BZ_N 26460                /* BAZAR's memory: 0.6 s */
#define PD_N 1103                 /* the room's pre-delay: 25 ms */
static const uint16_t RL[8] = {947, 1219, 1399, 1611, 1837, 2123, 2404, 2667};   /* the room's lines at 44.1 kHz */
#define RL_SUM (947 + 1219 + 1399 + 1611 + 1837 + 2123 + 2404 + 2667)
#define AP1_N 130
#define AP2_N 348
static int16_t dl[DL_N] ZZ_POOL;
static int16_t chl[CH_N] ZZ_POOL, chr_[CH_N] ZZ_POOL;
static int16_t wl[WOW_N] ZZ_POOL, wr[WOW_N] ZZ_POOL;
static int16_t bzb[BZ_N] ZZ_POOL;
static int16_t pdb[PD_N] ZZ_POOL;
static int16_t rbuf[RL_SUM] ZZ_POOL;
static int16_t ap1[AP1_N] ZZ_POOL, ap2[AP2_N] ZZ_POOL;

uint32_t zz_ram_bytes(void)
{
    return (uint32_t)(sizeof dl + sizeof chl + sizeof chr_ + sizeof wl + sizeof wr + sizeof bzb + sizeof pdb +
                      sizeof rbuf + sizeof ap1 + sizeof ap2) + 2048u;
}

#define Q 16384.0f
#define IQ (1.0f / 16384.0f)
static inline int16_t w16(float x)
{
    float v = x * Q;
    int32_t i = (int32_t)(v + (v >= 0.0f ? 0.5f : -0.5f));
    return (int16_t)(i > 32767 ? 32767 : (i < -32768 ? -32768 : i));
}

/* ------------------------------------------------------------------ small helpers */
static uint32_t rng = 0x2545F491u;
static inline uint32_t xr(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static inline float frand(void) { return (float)(xr() >> 8) * (1.0f / 16777216.0f); }   /* 0..1 */
static inline float lk(float p)                                                          /* sine of a phase in turns */
{
    float x;
    int32_t i;
    p -= zz_floorf(p);
    x = p * (float)ZZ_SIN_N;
    i = (int32_t)x;
    return ZZ_SIN[i] + (ZZ_SIN[i + 1] - ZZ_SIN[i]) * (x - (float)i);
}
static inline float tf(float x)                                                          /* triangle fold */
{
    float z = x * 0.25f + 0.25f;
    z -= zz_floorf(z);
    return z < 0.5f ? 4.0f * z - 1.0f : 3.0f - 4.0f * z;
}
static inline float tri4(float z)                                                        /* the 4-bit triangle */
{
    float t = z < 0.5f ? 4.0f * z - 1.0f : 3.0f - 4.0f * z;
    int32_t q = (int32_t)(t * 7.5f + (t >= 0.0f ? 0.5f : -0.5f));
    return (float)q / 7.5f;
}
static inline float p2(float semis) { return zz_powf(2.0f, semis / 12.0f); }
static inline float mtof(int n) { return 440.0f * p2((float)(n - 69)); }

/* ------------------------------------------------------------------ voices */
typedef struct { float amp, amp2, pe, ph, mph, lp, vel; } drum_t;
typedef struct {
    float f, ft, amp, fe, vel, pe, sg, dp, sp, a1, a2, b1, b2, g, k;
    float fu[3], cp[3], mp[3], mo[3];
    uint8_t atk;
} syn_t;
typedef struct { float f, ft, amp, fe, vel, ph, sp, a1, a2, g, k; uint8_t atk; } bas_t;
typedef struct { int k; int32_t t, len, cap, seg, grain; float rp, s1, s2, s3, s4, h, hr, g, kk; int32_t cc; } bet_t;

static drum_t D[4];
static syn_t V[4];
static bas_t BV;
static bet_t bet;
static uint32_t lfsr = 1;
static int32_t nzc;
static float nzv = 1.0f;

/* control-rate values (update) */
static struct {
    float doom, kaw, dep, euph, sag, bpm, ratch, swing, skip, hic, pdrift, jump;
    float R[3], lv[3];
    float kF, kPunch, kPe, kDec, kIdx, kRat, kTri, kClick, sF, sBody, sNoise, sMix, hDec, hMetal, hLvl, bF, bRat, bIdx,
        bDec, dDrv, dComp, send0, lvl0, duck;
    int32_t nzDiv;
    int nzShort;
    float U, det, width, sub, punch, sDrv, sAsym, sTA, sGain, vg, mr, I, mfb, shape, G, bias, res, bright, noise, atkC,
        decC, glide, glC, chor, send1, lvl1;
    float bSub, bOct, bDrv, bGain, bDuty, bTri, bG, bRes, bCut, bEnv, bFdec, bAdec, bGlide, send2, lvl2;
    float wet, ratio, mDrv, mGain, div, chz, betLen, dFb, dDamp, rDamp, wow, flut, dropP, hiss, tape, vol, tgtD;
    float rg[8];
} c;

static volatile uint8_t shed;     /* the render ran late: fewer unison copies, no chorus (zz_shed) */
void zz_shed(int level) { shed = (uint8_t)(level < 0 ? 0 : (level > 2 ? 2 : level)); }

/* drift (APÉRO): one slow random walk per macro */
static float rw[18], rt[18];
static const float DA[4] = {0.30f, 0.45f, 0.30f, 0.30f};

/* effects state */
static float dT = 0.3f * SR, fbk;
static int32_t dw, cw, ww, pdi, a1i, a2i, bzw;
static float cph, wph, fph;
static int32_t ri[8];
static float rlp[8], ry[8];
static float dcx[2], dcy[2], tp[2], cenv, cg = 1.0f, cgT = 1.0f, drop = 1.0f, dropT = 1.0f;
static struct { float h, r; int32_t c; } cr[3];

/* sequencer */
static volatile uint8_t seq_on;
static uint32_t step, clock_;
static float next_;
typedef struct { uint32_t at; int8_t tr, lane, n, a, slide, beat; float v; int8_t bk; } qev_t;
#define QN 48
static qev_t qv[QN];
static int qn;

/* UI -> engine: notes from the keys */
#define PN 16
static struct { int8_t tr, what; float vel; } pq[PN];
static volatile uint32_t pq_w, pq_r;
/* engine -> UI: events */
#define EN 128
static zz_ev_t evq[EN];
static volatile uint32_t ev_w, ev_r;
static void emit(uint8_t t, uint8_t tr, uint8_t a, uint8_t b)
{
    uint32_t w = ev_w;
    if (w - ev_r >= EN)
        return;                                   /* the UI is behind: drop it */
    evq[w % EN].t = t;
    evq[w % EN].tr = tr;
    evq[w % EN].a = a;
    evq[w % EN].b = b;
    ev_w = w + 1u;
}
int zz_ev_get(zz_ev_t *e)
{
    uint32_t r = ev_r;
    if (r == ev_w)
        return 0;
    *e = evq[r % EN];
    ev_r = r + 1u;
    return 1;
}
void zz_play(int tr, int what, float vel)
{
    uint32_t w = pq_w;
    if (w - pq_r >= PN)
        return;
    pq[w % PN].tr = (int8_t)tr;
    pq[w % PN].what = (int8_t)what;
    pq[w % PN].vel = vel;
    pq_w = w + 1u;
}
void zz_seq(int on)
{
    if (on && !seq_on) {
        step = 0;
        next_ = (float)clock_;
    }
    if (!on)
        qn = 0;
    seq_on = (uint8_t)(on != 0);
}
int zz_seq_on(void) { return seq_on; }
float zz_bpm(void) { return c.bpm; }

/* ------------------------------------------------------------------ hits */
static void drum_hit(int l, float v)
{
    drum_t *d = &D[l];
    d->vel = v;
    d->amp = v;
    d->amp2 = v;
    d->pe = 1.0f;
    if (l == 0 || l == 3) {
        d->ph = 0.0f;
        d->mph = 0.0f;
    }
}
static void synth_on(int n, float vel)
{
    syn_t *b = &V[0];
    int i, u;
    for (i = 1; i < 4; i++)
        if (V[i].amp < b->amp)
            b = &V[i];
    b->ft = mtof(n);
    if (b->amp < 0.02f || c.glide < 0.002f)
        b->f = b->ft;
    if (b->amp < 0.02f) {
        for (u = 0; u < 3; u++) {
            b->cp[u] = frand();
            b->mp[u] = 0.0f;
            b->mo[u] = 0.0f;
        }
        b->a1 = b->a2 = b->b1 = b->b2 = 0.0f;
        b->fe = 0.0f;
    }
    b->vel = vel;
    b->atk = 1;
    b->sg = 0.0f;
    b->pe = c.punch * 100.0f * (vel > 0.9f ? 1.0f : 0.55f);
    if (frand() < c.hic)
        b->pe += frand() < 0.5f ? 1200.0f : -1200.0f;
}
static void bass_on(int n, float vel, int slide)
{
    BV.ft = mtof(n);
    if (slide && BV.amp > 0.05f) {
        if (vel * 0.8f > BV.vel)
            BV.vel = vel * 0.8f;
        return;
    }
    BV.f = BV.ft;
    BV.vel = vel;
    BV.atk = 1;
    if (BV.amp < 0.02f) {
        BV.a1 = BV.a2 = 0.0f;
        BV.ph = 0.0f;
        BV.sp = 0.0f;
    }
}
static void bet_on(int k)
{
    float spb = SR * 60.0f / c.bpm / 4.0f;
    int32_t len = (int32_t)(spb * c.betLen);
    if (k <= 0)
        return;
    if (len > BZ_N - 8)
        len = BZ_N - 8;
    bet.k = k;
    bet.t = 0;
    bet.len = len;
    bet.cap = bzw;
    bet.seg = (int32_t)(SR * 60.0f / c.bpm / c.div);
    if (bet.seg < 64)
        bet.seg = 64;
    bet.grain = (int32_t)(SR * (0.04f + c.chz * 0.08f));
    bet.rp = bet.s1 = bet.s2 = bet.s3 = bet.s4 = bet.h = bet.hr = 0.0f;
    bet.g = 0.0f;
    bet.cc = 0;
    emit(ZZ_EV_BET, ZZ_BAZAR, (uint8_t)k, (uint8_t)(len * 100 / (int32_t)SR > 255 ? 255 : len * 100 / (int32_t)SR));
}

/* ------------------------------------------------------------------ control rate */
static void crush(int i, float mo)
{
    float a = zz_sm(mo, 0.25f, 1.0f);
    c.R[i] = 1.0f + zz_floorf(zz_powf(a, 1.5f) * 26.0f);
    c.lv[i] = zz_powf(2.0f, 16.0f - zz_sm(mo, 0.3f, 1.0f) * 12.5f) * 0.5f;
}
static inline float dexp(float sec) { return zz_expf(-6.9f / (SR * sec)); }   /* -60 dB in sec */
static inline float onepole(float hz) { return 1.0f - zz_expf(-2.0f * ZZ_PI * hz / SR); }

static void update(void)
{
    const float B = (float)CTL / SR, ap = zz_G[ZZ_APERO];
    float h, h7, z, cut;
    int tr, q, j = 0, ri_, i;
    for (tr = 0; tr < 4; tr++)
        for (q = 0; q < 4; q++, j++) {
            float v;
            if (frand() < B * (0.4f + ap * 2.5f))
                rt[j] = frand() * 2.0f - 1.0f;
            rw[j] += (rt[j] - rw[j]) * (B * (0.6f + ap * 4.0f) > 1.0f ? 1.0f : B * (0.6f + ap * 4.0f));
            v = zz_T[tr][q] + ap * DA[q] * rw[j];
            zz_ET[tr][q] = zz_clamp01(v);
        }
    for (tr = 0; tr < 4; tr++)
        zz_ET[tr][ZZ_BOUCAN] = zz_T[tr][ZZ_BOUCAN];
    for (q = 0; q < 2; q++) {
        int jj = 16 + q, gk = q ? ZZ_CAFEINE : ZZ_HUMEUR;
        float a = q ? 0.12f : 0.2f;
        if (frand() < B * (0.4f + ap * 2.5f))
            rt[jj] = frand() * 2.0f - 1.0f;
        rw[jj] += (rt[jj] - rw[jj]) * (B * (0.6f + ap * 4.0f) > 1.0f ? 1.0f : B * (0.6f + ap * 4.0f));
        zz_EG[gk] = zz_clamp01(zz_G[gk] + ap * a * rw[jj]);
    }
    zz_EG[ZZ_APERO] = ap;

    h = zz_EG[ZZ_HUMEUR];
    h7 = h * 7.0f;
    c.kaw = zz_clamp01(1.0f - (h7 - 1.5f));
    c.doom = zz_clamp01(1.0f - zz_fabsf(h7 - 5.5f));
    c.dep = zz_clamp01(h7 - 5.5f);
    c.euph = zz_clamp01(1.0f - h7);
    z = h7 < 2.0f ? 0.0f : (h7 < 5.0f ? 1.0f : 2.0f);
    c.sag = c.dep * 90.0f;
    c.bpm = 70.0f + zz_EG[ZZ_CAFEINE] * 110.0f;
    c.ratch = zz_sm(zz_EG[ZZ_CAFEINE], 0.7f, 1.0f) * 0.5f;
    c.swing = ap * 0.3f;
    c.skip = ap * 0.2f;
    c.hic = ap * 0.15f;
    c.pdrift = ap * 30.0f;
    c.jump = ap * 0.15f;
    {   /* BOUM */
        const volatile float *d = zz_ET[0];
        float bi = d[ZZ_BIDOCHE], ch = d[ZZ_CHATOUILLE], fl = d[ZZ_FLEMME];
        static const float KF[3] = {50.0f, 46.0f, 40.0f}, KR[4] = {1.0f, 1.5f, 2.0f, 2.76f},
                           BF[3] = {392.0f, 330.0f, 220.0f}, BR[6] = {1.41f, 2.0f, 2.76f, 3.5f, 1.5f, 4.2f};
        int k4 = (int)(ch * 4.0f), k6 = (int)(ch * 6.0f);
        c.kF = KF[(int)z];
        c.kPunch = (2.0f + bi * 7.0f) * (1.0f - 0.35f * c.dep);
        c.kPe = zz_expf(-1.0f / (SR * (0.012f + bi * 0.03f)));
        c.kDec = dexp(0.12f + fl * 1.1f + bi * 0.25f);
        c.kIdx = ch * ch * 4.0f;
        c.kRat = KR[k4 > 3 ? 3 : k4];
        c.kTri = zz_sm(ch, 0.4f, 0.8f);
        c.kClick = 0.2f + bi * 0.5f;
        c.sF = 185.0f * (1.0f + ch * 0.5f);
        c.sBody = dexp(0.06f + fl * 0.25f + bi * 0.08f);
        c.sNoise = dexp(0.09f + fl * 0.6f);
        c.sMix = 0.55f - bi * 0.2f;
        c.hDec = 0.025f + fl * 0.3f;
        c.hMetal = ch;
        c.hLvl = (1.0f - 0.6f * c.dep) * (1.0f + 0.3f * c.euph);
        c.bF = BF[(int)z] * (1.0f + c.euph);
        c.bRat = BR[k6 > 5 ? 5 : k6];
        c.bIdx = 2.0f + ch * 5.0f;
        c.bDec = dexp(0.08f + fl * 1.4f);
        c.nzDiv = 1 + (int32_t)(ch * 6.0f);
        c.nzShort = ch > 0.55f;
        c.dDrv = 1.0f + bi * bi * 4.0f + c.doom * 2.0f;
        c.dComp = 1.0f / (1.0f + (c.dDrv - 1.0f) * 0.25f);
        crush(0, d[ZZ_MOISI]);
        c.send0 = 0.04f + fl * 0.25f;
        c.lvl0 = zz_powf(d[ZZ_BOUCAN], 1.3f) * (zz_mute[0] ? 0.0f : 1.0f);
        c.duck = bi * 0.5f;
    }
    {   /* MATOU */
        const volatile float *s = zz_ET[1];
        float b = s[ZZ_BIDOCHE], ch = s[ZZ_CHATOUILLE], fl = s[ZZ_FLEMME], atk;
        static const float KT[10] = {1, 2, 1, 3, 2, 4, 1, 3, 5, 8}, DT[10] = {1, 1.414f, 0.5f, 2.76f, 1.5f, 3.14f,
                                                                              0.71f, 4.2f, 6.66f, 7.13f};
        ri_ = (int)(ch * 10.0f);
        if (ri_ > 9)
            ri_ = 9;
        c.U = 1.0f + b * 2.0f;
        if (shed && c.U > 3.0f - (float)shed)
            c.U = 3.0f - (float)shed;
        c.det = 2.0f + b * b * 24.0f + c.doom * 8.0f;
        c.width = 0.2f + b * 0.8f;
        c.sub = zz_sm(b, 0.25f, 0.85f) * 0.9f + c.doom * 0.25f;
        c.punch = zz_sm(b, 0.35f, 1.0f) * 16.0f;
        c.sDrv = 1.0f + b * b * 3.0f + c.doom * 5.0f;
        c.sAsym = c.doom * 0.3f;
        c.sTA = zz_tanhf(c.sAsym);
        c.sGain = 1.0f / (1.0f + (c.sDrv - 1.0f) * 0.22f);
        c.vg = 0.45f + b * 0.55f;
        c.mr = KT[ri_] + (DT[ri_] - KT[ri_]) * c.doom;
        c.I = ch * ch * 8.5f * (1.0f + c.doom * 0.5f);
        c.mfb = zz_sm(ch, 0.55f, 1.0f) * 1.3f;
        c.shape = ch;
        c.G = 1.0f + zz_powf(ch, 1.5f) * 7.0f;
        c.bias = zz_sinf(ch * 7.0f) * 0.3f * ch;
        c.res = 0.1f + 0.5f * zz_bump(ch, 0.5f, 0.22f) + c.doom * 0.18f + zz_sm(ch, 0.85f, 1.0f) * 0.2f;
        if (c.res > 0.95f)
            c.res = 0.95f;
        c.bright = (250.0f + ch * 7000.0f + c.kaw * 1500.0f + c.euph * 2500.0f + b * 1500.0f) * (1.0f - 0.55f * c.dep);
        c.noise = zz_sm(ch, 0.7f, 1.0f) * 0.12f;
        atk = 0.0012f + zz_powf(zz_sm(fl, 0.55f, 1.0f), 2.0f) * 1.8f;
        c.atkC = 1.0f - zz_expf(-1.0f / (SR * atk / 3.0f));
        c.decC = dexp(0.05f + fl * fl * 8.0f + b * 0.25f + c.dep * 1.5f);
        c.glide = zz_sm(fl, 0.5f, 1.0f) * 0.25f;
        c.glC = c.glide > 0.002f ? 1.0f - zz_expf(-(float)CTL / (SR * c.glide * 0.3f)) : 1.0f;
        c.chor = c.kaw * 0.3f + c.euph * 0.25f + b * 0.15f + c.dep * 0.1f;
        if (c.chor > 0.6f)
            c.chor = 0.6f;
        if (shed)
            c.chor = 0.0f;
        crush(1, s[ZZ_MOISI]);
        c.send1 = zz_sm(fl, 0.15f, 1.0f) * 0.7f + 0.08f + c.dep * 0.3f;
        c.lvl1 = zz_powf(s[ZZ_BOUCAN], 1.3f) * 0.8f * (zz_mute[1] ? 0.0f : 1.0f);
    }
    {   /* MAMMOUTH */
        const volatile float *m = zz_ET[2];
        float bi = m[ZZ_BIDOCHE], ch = m[ZZ_CHATOUILLE], fl = m[ZZ_FLEMME];
        static const float DU[4] = {0.5f, 0.25f, 0.125f, 0.5f};
        int k4 = (int)(ch * 4.0f);
        c.bSub = 0.3f + bi * 0.7f;
        c.bOct = zz_sm(bi, 0.55f, 1.0f) * 0.6f;
        c.bDrv = 1.0f + bi * bi * 5.0f + c.doom * 3.0f;
        c.bGain = 1.0f / (1.0f + (c.bDrv - 1.0f) * 0.2f);
        c.bDuty = DU[k4 > 3 ? 3 : k4];
        c.bTri = 1.0f - ch;
        c.bG = 1.0f + zz_powf(ch, 1.5f) * 5.0f;
        c.bRes = 0.2f + ch * 0.65f;
        if (c.bRes > 0.93f)
            c.bRes = 0.93f;
        c.bCut = (80.0f + ch * 400.0f) * (1.0f - 0.4f * c.dep);
        c.bEnv = 1200.0f + ch * 3500.0f + bi * 800.0f;
        c.bFdec = 1.0f - zz_expf(-1.0f / (SR * (0.06f + fl * 0.7f)));
        c.bAdec = dexp(0.15f + fl * 2.0f);
        c.bGlide = 1.0f - zz_expf(-(float)CTL / (SR * (0.01f + fl * 0.2f)));
        crush(2, m[ZZ_MOISI]);
        c.send2 = fl * 0.15f;
        c.lvl2 = zz_powf(m[ZZ_BOUCAN], 1.3f) * 0.9f * (zz_mute[2] ? 0.0f : 1.0f);
    }
    {   /* BAZAR: the whole mix */
        const volatile float *x = zz_ET[3];
        float bi = x[ZZ_BIDOCHE], ch = x[ZZ_CHATOUILLE], fl = x[ZZ_FLEMME], mo = x[ZZ_MOISI], rtm;
        static const float DV[6] = {1, 2, 4, 8, 3, 6};
        int k6 = (int)(ch * 6.0f);
        c.wet = 0.55f + bi * 0.45f;
        c.ratio = 1.0f + bi * 4.0f + c.doom * 2.0f;
        c.mDrv = 1.0f + bi * bi * 1.5f + c.doom * 0.8f;
        c.mGain = 1.0f / (1.0f + (c.mDrv - 1.0f) * 0.3f);
        c.div = DV[k6 > 5 ? 5 : k6];
        c.chz = ch;
        c.betLen = 1.0f + fl * 7.0f;
        rtm = 0.5f + fl * fl * 7.0f + c.doom * 1.5f + c.dep * 3.0f;
        for (i = 0; i < 8; i++)
            c.rg[i] = zz_powf(10.0f, -3.0f * (float)RL[i] / (SR * rtm));
        c.dFb = 0.15f + fl * 0.6f;
        c.dDamp = onepole(1500.0f + c.kaw * 5000.0f);
        c.rDamp = onepole(9000.0f - c.doom * 6500.0f);
        c.wow = mo * mo * 0.004f * SR;
        if (c.dep * 0.0012f * SR > c.wow)
            c.wow = c.dep * 0.0012f * SR;
        c.flut = zz_sm(mo, 0.5f, 1.0f) * 0.0007f * SR;
        c.dropP = zz_sm(mo, 0.7f, 1.0f) * 0.03f;
        c.hiss = mo * mo * 0.012f;
        c.tape = onepole(17000.0f - mo * 12500.0f);
        c.vol = zz_powf(x[ZZ_BOUCAN], 1.3f);
    }
    c.tgtD = SR * 60.0f / c.bpm * 0.75f;
    if (c.tgtD > (float)(DL_N / 2 - 4))
        c.tgtD = (float)(DL_N / 2 - 4);
    dT += (c.tgtD - dT) * 0.05f;
    if (frand() < c.dropP)
        dropT = 0.03f + frand() * 0.4f;
    else if (frand() < 0.02f)
        dropT = 1.0f;
    cut = cenv;
    cgT = (cut > 0.22f ? zz_powf(cut / 0.22f, 1.0f / c.ratio - 1.0f) : 1.0f) * (1.0f + (c.ratio - 1.0f) * 0.1f);
    {   /* per-block voice coefficients */
        const float pdec = zz_expf(-(float)CTL / (SR * 0.03f));
        for (i = 0; i < 4; i++) {
            syn_t *v = &V[i];
            float fv, fc;
            if (v->amp < 1e-5f && !v->atk)
                continue;
            v->f += (v->ft - v->f) * c.glC;
            v->dp += 2.0f * ZZ_PI * 0.35f * B;
            if (v->dp > 2.0f * ZZ_PI)
                v->dp -= 2.0f * ZZ_PI;
            v->pe *= pdec;
            v->sg += c.sag * B;
            fv = v->f * zz_powf(2.0f, (c.pdrift * zz_sinf(v->dp) + v->pe - v->sg) / 1200.0f);
            v->fu[0] = fv;
            v->fu[1] = fv * zz_powf(2.0f, c.det / 1200.0f);
            v->fu[2] = fv * zz_powf(2.0f, -c.det * 1.07f / 1200.0f);
            fc = 30.0f + zz_powf(v->fe, 1.4f) * c.bright * (0.75f + v->vel * 0.35f);
            if (fc > SR * 0.42f)
                fc = SR * 0.42f;
            v->g = zz_tanf(ZZ_PI * fc / SR);
            v->k = 2.0f - 2.0f * c.res;
        }
        if (BV.amp > 1e-5f || BV.atk) {
            float fc;
            BV.f += (BV.ft - BV.f) * c.bGlide;
            fc = c.bCut + BV.fe * c.bEnv * (0.6f + BV.vel * 0.5f);
            if (fc > SR * 0.42f)
                fc = SR * 0.42f;
            BV.g = zz_tanf(ZZ_PI * fc / SR);
            BV.k = 2.0f - 2.0f * c.bRes;
        }
        if (bet.k == ZZ_PLONGE) {
            float t = (float)bet.t / (float)bet.len, fc = 18000.0f * zz_powf(150.0f / 18000.0f, t > 1.0f ? 1.0f : t);
            if (fc > SR * 0.42f)
                fc = SR * 0.42f;
            bet.g = zz_tanf(ZZ_PI * fc / SR);
            bet.kk = 2.0f - 2.0f * (0.3f + c.chz * 0.6f);
        }
    }
}

/* ------------------------------------------------------------------ sequencer */
static void qpush(uint32_t at, int tr, int lane, int n, float v, int a, int slide, int beat, int bk)
{
    qev_t *e;
    if (qn >= QN)
        return;
    e = &qv[qn++];
    e->at = at;
    e->tr = (int8_t)tr;
    e->lane = (int8_t)lane;
    e->n = (int8_t)n;
    e->v = v;
    e->a = (int8_t)a;
    e->slide = (int8_t)slide;
    e->beat = (int8_t)beat;
    e->bk = (int8_t)bk;
}
static void seq_step(void)
{
    const uint32_t s = step % 16u;
    const float spb = SR * 60.0f / c.bpm / 4.0f;
    float off = ((s & 1u) ? c.swing * spb : 0.0f) + (frand() - 0.5f) * zz_G[ZZ_APERO] * 0.1f * spb;
    uint32_t at;
    int l, n, r;
    if (off < 0.0f)
        off = 0.0f;
    at = clock_ + (uint32_t)off;
    if (!zz_mute[0])
        for (l = 0; l < 4; l++) {
            int v = zz_pat.drum[l][s];
            if (v && frand() >= c.skip) {
                int reps = (l > 0 && frand() < c.ratch) ? 2 + (int)(frand() * 2.0f) : 1;
                for (r = 0; r < reps; r++)
                    qpush(at + (uint32_t)((float)r * spb / (float)reps), 0, l, 0, (v == 2 ? 1.0f : 0.62f) * (r ? 0.7f : 1.0f),
                          v == 2 && !r, 0, 0, 0);
            }
        }
    n = zz_pat.syn_n[s];
    if (!zz_mute[1] && n >= 0 && frand() >= c.skip) {
        int reps = frand() < c.ratch ? 2 : 1, a = zz_pat.syn_a[s];
        if (frand() < c.jump)
            n += frand() < 0.5f ? 12 : -12;
        for (r = 0; r < reps; r++)
            qpush(at + (uint32_t)((float)r * spb / (float)reps), 1, 0, n, (a ? 1.0f : 0.62f) * (r ? 0.7f : 1.0f), a && !r,
                  0, 0, 0);
    }
    n = zz_pat.bas_n[s];
    if (!zz_mute[2] && n >= 0 && frand() >= c.skip * 0.5f) {
        int slide = zz_pat.bas_n[(s + 15u) % 16u] >= 0 && zz_ET[2][ZZ_FLEMME] > 0.5f;
        qpush(at, 2, 0, n, zz_pat.bas_a[s] ? 1.0f : 0.7f, zz_pat.bas_a[s], slide, 0, 0);
    }
    if (!zz_mute[3] && zz_pat.bz[s] > 0)
        qpush(at, 3, 0, 0, 1.0f, 0, 0, 0, zz_pat.bz[s]);
    emit(ZZ_EV_STEP, 0, (uint8_t)s, 0);
    if (s % 4u == 0u)
        qpush(at, -1, 0, 0, 0.0f, 0, 0, 1, 0);
    step++;
    next_ += spb;
}
static void q_run(void)
{
    int j;
    for (j = 0; j < qn; j++) {
        qev_t *e = &qv[j];
        if ((int32_t)(e->at - clock_) > 0)
            continue;
        if (e->beat)
            emit(ZZ_EV_BEAT, 0, 0, 0);
        else if (e->tr == 0) {
            drum_hit(e->lane, e->v);
            emit(ZZ_EV_HIT, 0, (uint8_t)e->lane, (uint8_t)e->a);
        } else if (e->tr == 1) {
            synth_on(e->n, e->v);
            emit(ZZ_EV_HIT, 1, (uint8_t)e->n, (uint8_t)e->a);
        } else if (e->tr == 2) {
            bass_on(e->n, e->v, e->slide);
            emit(ZZ_EV_HIT, 2, (uint8_t)e->n, (uint8_t)e->a);
        } else if (e->tr == 3)
            bet_on(e->bk);
        qv[j] = qv[--qn];
        j--;
    }
}
static void keys_run(void)
{
    while (pq_r != pq_w) {
        uint32_t r = pq_r;
        int tr = pq[r % PN].tr, w = pq[r % PN].what;
        float v = pq[r % PN].vel;
        if (tr == 0)
            drum_hit(w & 3, v);
        else if (tr == 1)
            synth_on(w, v);
        else if (tr == 2)
            bass_on(w, v, 0);
        else if (tr == 3)
            bet_on(w);
        pq_r = r + 1u;
    }
}

/* ------------------------------------------------------------------ the samples */
static inline int16_t rd16(const int16_t *b, int32_t n, int32_t i)
{
    i %= n;
    if (i < 0)
        i += n;
    return b[i];
}

void zz_render(float *outl, float *outr, uint32_t n)
{
    uint32_t i;
    keys_run();
    for (i = 0; i < n; i++) {
        float L, R, dr = 0.0f, sL = 0.0f, sR = 0.0f, bs = 0.0f, nz, duck, send, yL, yR, rin, sum, hh, rL = 0.0f,
                                                                                                       rR = 0.0f;
        int k, u, j;
        if ((clock_ & (CTL - 1u)) == 0u)
            update();
        if (seq_on && (float)clock_ >= next_)
            seq_step();
        if (qn)
            q_run();
        clock_++;
        if (--nzc <= 0) {   /* noise: a 15-bit LFSR as on the NES, long or short (metallic) */
            uint32_t bit;
            nzc = c.nzDiv;
            bit = (lfsr ^ (lfsr >> (c.nzShort ? 6 : 1))) & 1u;
            lfsr = (lfsr >> 1) | (bit << 14);
            nzv = (lfsr & 1u) ? 1.0f : -1.0f;
        }
        nz = nzv;
        /* BOUM */
        {
            drum_t *d = &D[0];
            if (d->amp > 1e-4f) {
                float f, body, tr4;
                d->pe *= c.kPe;
                f = c.kF * (1.0f + (c.kPunch - 1.0f) * d->pe);
                d->mph += f * c.kRat / SR;
                d->mph -= zz_floorf(d->mph);
                d->ph += f / SR;
                d->ph -= zz_floorf(d->ph);
                body = lk(d->ph + c.kIdx * d->pe * lk(d->mph) * (1.0f / (2.0f * ZZ_PI)));
                tr4 = tri4(d->ph);
                dr += (body + (tr4 - body) * c.kTri + (d->pe > 0.85f ? nz * c.kClick : 0.0f)) * d->amp * 1.2f;
                d->amp *= c.kDec;
            }
            d = &D[1];
            if (d->amp > 1e-4f || d->amp2 > 1e-4f) {
                d->pe *= 0.996f;
                d->ph += c.sF * (1.0f + d->pe * 0.6f) / SR;
                d->ph -= zz_floorf(d->ph);
                d->lp += 0.18f * (nz - d->lp);
                dr += lk(d->ph) * d->amp2 * (1.0f - c.sMix) + (nz - d->lp) * d->amp * c.sMix * 1.3f;
                d->amp *= c.sNoise;
                d->amp2 *= c.sBody;
            }
            d = &D[2];
            if (d->amp > 1e-4f) {
                float met, hi;
                d->ph += 2400.0f / SR;
                d->ph -= zz_floorf(d->ph);
                d->mph += 3528.0f / SR;
                d->mph -= zz_floorf(d->mph);
                met = ((d->ph < 0.5f) ^ (d->mph < 0.5f)) ? 1.0f : -1.0f;
                d->lp += 0.55f * (nz - d->lp);
                hi = nz - d->lp;
                dr += (hi * (1.0f - c.hMetal) + met * c.hMetal * 0.6f) * d->amp * 0.7f * c.hLvl;
                d->amp *= zz_expf(-6.9f / (SR * c.hDec * (d->vel > 0.9f ? 3.0f : 1.0f)));
            }
            d = &D[3];
            if (d->amp > 1e-4f) {
                d->pe *= 0.9995f;
                d->mph += c.bF * c.bRat / SR;
                d->mph -= zz_floorf(d->mph);
                d->ph += c.bF / SR;
                d->ph -= zz_floorf(d->ph);
                dr += lk(d->ph + c.bIdx * d->pe * lk(d->mph) * (1.0f / (2.0f * ZZ_PI))) * d->amp * 0.7f;
                d->amp *= c.bDec;
            }
            dr = zz_tanhf(dr * c.dDrv) * c.dComp;
            if (cr[0].c <= 0) {
                cr[0].h = zz_floorf(dr * c.lv[0] + 0.5f) / c.lv[0];
                cr[0].c = (int32_t)c.R[0];
            }
            cr[0].c--;
            dr = cr[0].h * c.lvl0;
        }
        duck = 1.0f - (D[0].amp > 1.0f ? 1.0f : D[0].amp) * c.duck;
        /* MATOU */
        {
            const float U = c.U, w1 = U - 1.0f > 1.0f ? 1.0f : U - 1.0f, w2 = U - 2.0f > 0.0f ? U - 2.0f : 0.0f, wd = c.width,
                        nrm = 1.0f / (1.0f + (U - 1.0f) * 0.55f + c.sub * 0.3f);
            for (k = 0; k < 4; k++) {
                syn_t *v = &V[k];
                float l = 0.0f, r = 0.0f, sub = 0.0f, xL, xR, g, kq, a1, a2, a3, v1, v2, v3, oL, ga;
                if (v->amp < 1e-5f && !v->atk)
                    continue;
                if (v->atk) {
                    v->amp += (v->vel * 1.05f - v->amp) * c.atkC;
                    if (v->amp >= v->vel) {
                        v->amp = v->vel;
                        v->atk = 0;
                    }
                } else
                    v->amp *= c.decC;
                v->fe += (v->amp - v->fe) * (v->amp > v->fe ? 0.02f : 0.0012f);
                for (u = 0; u < 3; u++) {
                    float w = u == 0 ? 1.0f : (u == 1 ? w1 : w2), fu, mp, ph, md, mm, cp, y;
                    if (w <= 0.0f)
                        break;
                    fu = v->fu[u];
                    mp = v->mp[u] + fu * c.mr / SR;
                    mp -= zz_floorf(mp);
                    v->mp[u] = mp;
                    ph = mp + v->mo[u] * c.mfb * 0.16f;
                    md = lk(ph);
                    v->mo[u] = md;
                    mm = md;
                    if (c.shape > 0.33f) {
                        float zz = ph - zz_floorf(ph), t4 = tri4(zz);
                        if (c.shape < 0.66f)
                            mm = md + (t4 - md) * (c.shape - 0.33f) / 0.33f;
                        else
                            mm = t4 + ((zz < 0.25f ? 1.0f : -1.0f) - t4) * (c.shape - 0.66f) / 0.34f;
                    }
                    cp = v->cp[u] + fu / SR;
                    cp -= zz_floorf(cp);
                    v->cp[u] = cp;
                    y = tf(lk(cp + c.I * mm * (1.0f / (2.0f * ZZ_PI))) * c.G + c.bias) * w;
                    if (u == 0) {
                        l += y;
                        r += y;
                    } else if (u == 1) {
                        l += y;
                        r += y * (1.0f - wd);
                    } else {
                        l += y * (1.0f - wd);
                        r += y;
                    }
                }
                if (c.sub > 0.001f) {
                    v->sp += v->fu[0] * 0.5f / SR;
                    v->sp -= zz_floorf(v->sp);
                    sub = (lk(v->sp) * 0.7f + (v->sp < 0.5f ? 0.3f : -0.3f)) * c.sub;
                }
                xL = (l + sub + nz * c.noise) * nrm;
                xR = (r + sub + nz * c.noise) * nrm;
                g = v->g;
                kq = v->k;
                a1 = 1.0f / (1.0f + g * (g + kq));
                a2 = g * a1;
                a3 = g * a2;
                v3 = xL - v->a2;
                v1 = a1 * v->a1 + a2 * v3;
                v2 = v->a2 + a2 * v->a1 + a3 * v3;
                v->a1 = 2.0f * v1 - v->a1;
                v->a2 = 2.0f * v2 - v->a2;
                oL = v2;
                v3 = xR - v->b2;
                v1 = a1 * v->b1 + a2 * v3;
                v2 = v->b2 + a2 * v->b1 + a3 * v3;
                v->b1 = 2.0f * v1 - v->b1;
                v->b2 = 2.0f * v2 - v->b2;
                ga = v->amp * c.vg;
                sL += oL * ga;
                sR += v2 * ga;
            }
            sL = (zz_tanhf(sL * 0.78f * c.sDrv + c.sAsym) - c.sTA) * c.sGain;
            sR = (zz_tanhf(sR * 0.78f * c.sDrv + c.sAsym) - c.sTA) * c.sGain;
            if (c.chor > 0.01f) {
                float dA, dB, p, fr, cL, cR;
                int32_t pi;
                chl[cw] = w16(sL);
                chr_[cw] = w16(sR);
                cph += 2.0f * ZZ_PI * 0.4f / SR;
                if (cph > 2.0f * ZZ_PI)
                    cph -= 2.0f * ZZ_PI;
                dA = SR * (0.011f + 0.003f * zz_sinf(cph));
                dB = SR * (0.013f + 0.003f * zz_sinf(cph * 1.13f + 1.5708f));
                p = (float)cw - dA + (float)CH_N;
                pi = (int32_t)p;
                fr = p - (float)pi;
                pi &= CH_N - 1;
                cL = ((float)chl[pi] + (float)(chl[(pi + 1) & (CH_N - 1)] - chl[pi]) * fr) * IQ;
                p = (float)cw - dB + (float)CH_N;
                pi = (int32_t)p;
                fr = p - (float)pi;
                pi &= CH_N - 1;
                cR = ((float)chr_[pi] + (float)(chr_[(pi + 1) & (CH_N - 1)] - chr_[pi]) * fr) * IQ;
                sL = sL * (1.0f - c.chor * 0.4f) + cL * c.chor;
                sR = sR * (1.0f - c.chor * 0.4f) + cR * c.chor;
                cw = (cw + 1) & (CH_N - 1);
            }
            if (cr[1].c <= 0) {
                cr[1].h = zz_floorf(sL * c.lv[1] + 0.5f) / c.lv[1];
                cr[1].r = zz_floorf(sR * c.lv[1] + 0.5f) / c.lv[1];
                cr[1].c = (int32_t)c.R[1];
            }
            cr[1].c--;
            sL = cr[1].h * c.lvl1 * duck;
            sR = cr[1].r * c.lvl1 * duck;
        }
        /* MAMMOUTH */
        if (BV.amp > 1e-5f || BV.atk) {
            float zz, pu, t4, x, g, kq, a1, a2, a3, v1, v2, v3;
            if (BV.atk) {
                BV.amp += (BV.vel * 1.05f - BV.amp) * 0.03f;
                if (BV.amp >= BV.vel) {
                    BV.amp = BV.vel;
                    BV.atk = 0;
                }
            } else
                BV.amp *= c.bAdec;
            BV.fe += ((BV.atk ? 1.0f : 0.0f) - BV.fe) * (BV.atk ? 0.05f : c.bFdec);
            BV.ph += BV.f / SR;
            BV.ph -= zz_floorf(BV.ph);
            BV.sp += BV.f * 0.5f / SR;
            BV.sp -= zz_floorf(BV.sp);
            zz = BV.ph;
            pu = zz < c.bDuty ? 1.0f : -1.0f;
            t4 = tri4(zz);
            x = tf((pu + (t4 - pu) * c.bTri) * c.bG * 0.8f);
            x += lk(BV.sp) * c.bSub + (BV.sp < 0.5f ? 1.0f : -1.0f) * c.bOct * 0.5f;
            g = BV.g;
            kq = BV.k;
            a1 = 1.0f / (1.0f + g * (g + kq));
            a2 = g * a1;
            a3 = g * a2;
            v3 = x - BV.a2;
            v1 = a1 * BV.a1 + a2 * v3;
            v2 = BV.a2 + a2 * BV.a1 + a3 * v3;
            BV.a1 = 2.0f * v1 - BV.a1;
            BV.a2 = 2.0f * v2 - BV.a2;
            bs = zz_tanhf(v2 * BV.amp * c.bDrv) * c.bGain;
        }
        if (cr[2].c <= 0) {
            cr[2].h = zz_floorf(bs * c.lv[2] + 0.5f) / c.lv[2];
            cr[2].c = (int32_t)c.R[2];
        }
        cr[2].c--;
        bs = cr[2].h * c.lvl2 * duck;
        /* the mix and the shared space */
        L = dr + sL + bs;
        R = dr + sR + bs;
        send = dr * c.send0 + (sL + sR) * 0.5f * c.send1 + bs * c.send2;
        {
            float p = (float)dw - dT, fr;
            int32_t pi, pn;
            if (p < 0.0f)
                p += (float)DL_N;
            pi = (int32_t)p;
            fr = p - (float)pi;
            if (pi >= DL_N)
                pi -= DL_N;
            pn = (pi + 1) % DL_N;
            yL = ((float)dl[pi] + (float)(dl[pn] - dl[pi]) * fr) * IQ;
            p = (float)dw - 2.0f * dT;
            if (p < 0.0f)
                p += (float)DL_N;
            pi = (int32_t)p;
            fr = p - (float)pi;
            if (pi >= DL_N)
                pi -= DL_N;
            pn = (pi + 1) % DL_N;
            yR = ((float)dl[pi] + (float)(dl[pn] - dl[pi]) * fr) * IQ;
            fbk += c.dDamp * (yR - fbk);
            dl[dw] = w16(send * 0.8f + zz_tanhf(fbk * c.dFb * 1.2f) / 1.2f);
            dw = (dw + 1) % DL_N;
        }
        rin = send + (yL + yR) * 0.15f;
        {
            float pdv = (float)pdb[pdi] * IQ, ab, an;
            pdb[pdi] = w16(rin);
            pdi = (pdi + 1) % PD_N;
            rin = pdv;
            ab = (float)ap1[a1i] * IQ;
            an = rin + ab * 0.6f;
            ap1[a1i] = w16(an);
            rin = ab - an * 0.6f;
            a1i = (a1i + 1) % AP1_N;
            ab = (float)ap2[a2i] * IQ;
            an = rin + ab * 0.6f;
            ap2[a2i] = w16(an);
            rin = ab - an * 0.6f;
            a2i = (a2i + 1) % AP2_N;
        }
        sum = 0.0f;
        {
            int32_t base = 0;
            for (j = 0; j < 8; j++) {
                float yy = (float)rbuf[base + ri[j]] * IQ;
                rlp[j] += c.rDamp * (yy - rlp[j]);
                ry[j] = rlp[j];
                sum += ry[j];
                base += RL[j];
            }
            hh = sum * 0.25f;
            base = 0;
            for (j = 0; j < 8; j++) {
                rbuf[base + ri[j]] = w16((ry[j] - hh) * c.rg[j] + rin * ((j & 1) ? 0.35f : -0.35f));
                if (++ri[j] >= (int32_t)RL[j])
                    ri[j] = 0;
                if (j & 1)
                    rR += ry[j];
                else
                    rL += ry[j];
                base += RL[j];
            }
        }
        L += yL * 0.55f + rL * 0.12f;
        R += yR * 0.55f + rR * 0.12f;
        /* BAZAR: the bêtises on the whole mix */
        if (!bet.k || bet.k >= ZZ_PLONGE) {
            bzb[bzw] = w16((L + R) * 0.5f);
            bzw = (bzw + 1) % BZ_N;
        }
        if (bet.k) {
            const int32_t tt = bet.t;
            float fade = (float)tt / 220.0f, f2 = (float)(bet.len - tt) / 220.0f, w, y = 0.0f;
            int have = 1;
            if (f2 < fade)
                fade = f2;
            if (fade > 1.0f)
                fade = 1.0f;
            if (fade < 0.0f)
                fade = 0.0f;
            w = c.wet * fade;
            if (bet.k == ZZ_BEGAIE)
                y = (float)rd16(bzb, BZ_N, bet.cap - bet.seg + tt % bet.seg) * IQ;
            else if (bet.k == ZZ_RECULE)
                y = (float)rd16(bzb, BZ_N, bet.cap - 1 - tt) * IQ;
            else if (bet.k == ZZ_FIGE) {
                int32_t g = bet.grain, p1 = tt % g, pp2 = (tt + (g >> 1)) % g;
                y = (float)rd16(bzb, BZ_N, bet.cap - g + p1) * IQ * lk((float)p1 / (float)g * 0.5f) +
                    (float)rd16(bzb, BZ_N, bet.cap - g + pp2) * IQ * lk((float)pp2 / (float)g * 0.5f);
            } else if (bet.k == ZZ_COULE) {
                float sp = 1.0f - (float)tt / (float)bet.len, ix, f;
                int32_t i0;
                if (sp < 0.0f)
                    sp = 0.0f;
                bet.rp += sp;
                ix = (float)bet.cap - (float)bet.len * 0.5f + bet.rp;
                i0 = (int32_t)zz_floorf(ix);
                f = ix - (float)i0;
                y = ((float)rd16(bzb, BZ_N, i0) + (float)(rd16(bzb, BZ_N, i0 + 1) - rd16(bzb, BZ_N, i0)) * f) * IQ;
            } else
                have = 0;
            if (have) {
                L = L * (1.0f - w) + y * w;
                R = R * (1.0f - w) + y * w;
            } else if (bet.k == ZZ_PLONGE && bet.g > 0.0f) {
                float g = bet.g, kq = bet.kk, a1 = 1.0f / (1.0f + g * (g + kq)), a2 = g * a1, a3 = g * a2, v1, v2, v3, fL;
                v3 = L - bet.s2;
                v1 = a1 * bet.s1 + a2 * v3;
                v2 = bet.s2 + a2 * bet.s1 + a3 * v3;
                bet.s1 = 2.0f * v1 - bet.s1;
                bet.s2 = 2.0f * v2 - bet.s2;
                fL = v2;
                v3 = R - bet.s4;
                v1 = a1 * bet.s3 + a2 * v3;
                v2 = bet.s4 + a2 * bet.s3 + a3 * v3;
                bet.s3 = 2.0f * v1 - bet.s3;
                bet.s4 = 2.0f * v2 - bet.s4;
                L = L * (1.0f - w) + fL * w;
                R = R * (1.0f - w) + v2 * w;
            } else if (bet.k == ZZ_ECRASE) {
                if (bet.cc <= 0) {
                    bet.h = zz_floorf(L * 6.0f + 0.5f) / 6.0f;
                    bet.hr = zz_floorf(R * 6.0f + 0.5f) / 6.0f;
                    bet.cc = 8 + (int32_t)(c.chz * 24.0f + 0.5f);
                }
                bet.cc--;
                L = L * (1.0f - w) + bet.h * w;
                R = R * (1.0f - w) + bet.hr * w;
            }
            if (++bet.t >= bet.len)
                bet.k = 0;
        }
        /* the master: drive, tape rot, glue, limiter */
        {
            float yy, d, qd, qf, det;
            int32_t qi, qn2;
            L = zz_tanhf(L * c.mDrv) * c.mGain;
            R = zz_tanhf(R * c.mDrv) * c.mGain;
            yy = L - dcx[0] + 0.995f * dcy[0];
            dcx[0] = L;
            dcy[0] = yy;
            L = yy;
            yy = R - dcx[1] + 0.995f * dcy[1];
            dcx[1] = R;
            dcy[1] = yy;
            R = yy;
            wl[ww] = w16(L);
            wr[ww] = w16(R);
            wph += 2.0f * ZZ_PI * 0.55f / SR;
            if (wph > 2.0f * ZZ_PI)
                wph -= 2.0f * ZZ_PI;
            fph += 2.0f * ZZ_PI * 6.5f / SR;
            if (fph > 2.0f * ZZ_PI)
                fph -= 2.0f * ZZ_PI;
            if (c.wow > 0.5f || c.flut > 0.5f) {
                d = SR * 0.006f + c.wow * (0.5f + 0.5f * zz_sinf(wph)) + c.flut * zz_sinf(fph);
                qd = (float)ww - d;
                if (qd < 0.0f)
                    qd += (float)WOW_N;
                qi = (int32_t)qd;
                qf = qd - (float)qi;
                qi &= WOW_N - 1;                 /* (-1e-7 + 1024 is 1024 in a float) */
                qn2 = (qi + 1) & (WOW_N - 1);
                L = ((float)wl[qi] + (float)(wl[qn2] - wl[qi]) * qf) * IQ;
                R = ((float)wr[qi] + (float)(wr[qn2] - wr[qi]) * qf) * IQ;
            }
            ww = (ww + 1) & (WOW_N - 1);
            tp[0] += c.tape * (L - tp[0]);
            tp[1] += c.tape * (R - tp[1]);
            L = tp[0];
            R = tp[1];
            drop += (dropT - drop) * 0.003f;
            L *= drop;
            R *= drop;
            if (c.hiss > 0.0f) {
                L += (frand() * 2.0f - 1.0f) * c.hiss;
                R += (frand() * 2.0f - 1.0f) * c.hiss;
            }
            det = zz_fabsf(L) > zz_fabsf(R) ? zz_fabsf(L) : zz_fabsf(R);
            cenv += (det - cenv) * (det > cenv ? 0.004f : 0.00015f);
            cg += (cgT - cg) * 0.002f;
            L *= cg;
            R *= cg;
            outl[i] = zz_tanhf(L * 1.5f) * c.vol;
            outr[i] = zz_tanhf(R * 1.5f) * c.vol;
        }
    }
}

void zz_panic(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        D[i].amp = D[i].amp2 = 0.0f;
        V[i].amp = 0.0f;
        V[i].atk = 0;
    }
    BV.amp = 0.0f;
    BV.atk = 0;
    bet.k = 0;
    qn = 0;
}

void zz_init(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        int l;
        for (l = 0; l < 4; l++)
            zz_pat.drum[l][i] = 0;
        zz_pat.syn_n[i] = zz_pat.bas_n[i] = -1;
        zz_pat.syn_a[i] = zz_pat.bas_a[i] = 0;
        zz_pat.bz[i] = 0;
    }
    for (i = 0; i < 4; i++) {
        V[i].k = 2.0f;
        V[i].g = 0.1f;
        V[i].dp = (float)i * 1.7f;
    }
    BV.k = 2.0f;
    BV.g = 0.1f;
    update();
}
