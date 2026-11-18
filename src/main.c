// KNULLI Drumkit - drum computer for Anbernic handhelds running KNULLI
// TR-808 / TR-909 synthesized voices + 8-slot WAV sampler, 16-step sequencer.
//
// Controls (KNULLI gamepad layout):
//   D-pad         move cursor (track / step)
//   A (south)     toggle step + preview sound
//   B (east)      preview sound
//   X             next bank (808 / 909 / sampler)
//   Y             previous bank
//   L shoulder    BPM -2        R shoulder    BPM +2
//   Select        play / stop   Start         save pattern
//   Select+Start  quit (pattern is saved)

#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE        44100
#define NSTEP       16
#define TBP         8
#define NBANK       3
#define NTR         (TBP * NBANK)
#define NVOICE      32
#define MASTER      0.85f

#define WIN_W       1280
#define WIN_H       720

#define CFGPATH     "drumkit.pattern"
#define SAMPLE_FMT  "samples/%02d.wav"
typedef struct {
    char     name[12];
    uint8_t  pat[NSTEP];
    float   *buf;
    int      len;
    float    level;
} Track;

typedef struct {
    const float *buf;
    int      len;
    int      pos;
    float    gain;
} Voice;

static Track tr[NTR];
static Voice voices[NVOICE];
static int   vnext = NVOICE - 1;

static volatile int playing = 0;
static int  curStep = -1;
static float bpm = 120.0f;
static int   bank = 0;
static int   curTrack = 0, curStepEd = 0;

static const char *bankNames[NBANK] = {"TR-808", "TR-909", "SAMPLER"};
static const uint8_t bankCol[NBANK][3] = {
    {235, 90, 60},   // 808 orange/red
    {80, 150, 255},  // 909 blue
    {90, 220, 110},  // sampler green
};

// ---------------------------------------------------------------- mixing

static double stepDur(void) { return (double)RATE * 60.0 / bpm / 4.0; }

static void trigger(int track, float gain)
{
    if (!tr[track].buf || tr[track].len < 1) return;
    Voice *v = &voices[vnext];
    if (--vnext < 0) vnext = NVOICE - 1;
    v->buf = tr[track].buf;
    v->len = tr[track].len;
    v->pos = 0;
    v->gain = gain * tr[track].level;
}

static void triggerStep(int step)
{
    for (int i = 0; i < NTR; i++)
        if (tr[i].pat[step]) trigger(i, 1.0f);
}

static void audioCB(void *ud, uint8_t *stream, int bytes)
{
    (void)ud;
    static float acc = 0.0f;
    int16_t *out = (int16_t *)stream;
    int frames = bytes / 4;
    for (int f = 0; f < frames; f++) {
        float L = 0.0f, R = 0.0f;
        if (playing) {
            acc += 1.0f;
            if (acc >= stepDur()) {
                acc -= (float)stepDur();
                curStep = (curStep + 1) & (NSTEP - 1);
                triggerStep(curStep);
            }
        }
        for (int i = 0; i < NVOICE; i++) {
            Voice *v = &voices[i];
            if (v->pos < v->len) {
                float s = v->buf[v->pos++] * v->gain;
                L += s; R += s;
            }
        }
        L *= MASTER; R *= MASTER;
        if (L > 1.0f)  L = 1.0f;
        if (L < -1.0f) L = -1.0f;
        if (R > 1.0f)  R = 1.0f;
        if (R < -1.0f) R = -1.0f;
        out[2 * f]     = (int16_t)(L * 32767.0f);
        out[2 * f + 1] = (int16_t)(R * 32767.0f);
    }
}

// ---------------------------------------------------------------- synthesis

static float *mkbuf(int len) { return (float *)calloc((size_t)len, sizeof(float)); }

static void norm(Track *t, float target)
{
    float peak = 1e-6f;
    for (int i = 0; i < t->len; i++) {
        float a = fabsf(t->buf[i]);
        if (a > peak) peak = a;
    }
    float k = target / peak;
    for (int i = 0; i < t->len; i++) t->buf[i] *= k;
}

static float rndn(void) { return ((float)rand() / RAND_MAX) * 2.0f - 1.0f; }

static void render808kick(Track *t)
{
    int len = RATE / 2;
    t->buf = mkbuf(len); t->len = len; t->level = 1.0f;
    float ph = 0.0f;
    for (int i = 0; i < len; i++) {
        float tt = (float)i / RATE;
        float f = 48.0f + 110.0f * expf(-tt * 22.0f);
        ph += 2.0f * (float)M_PI * f / RATE;
        t->buf[i] = sinf(ph) * expf(-tt * 6.5f);
    }
}

static void render909kick(Track *t)
{
    int len = RATE * 3 / 5;
    t->buf = mkbuf(len); t->len = len; t->level = 1.0f;
    float ph = 0.0f, lpy = 0.0f;
    for (int i = 0; i < len; i++) {
        float tt = (float)i / RATE;
        float f = 46.0f + 260.0f * expf(-tt * 38.0f);
        ph += 2.0f * (float)M_PI * f / RATE;
        lpy += 0.35f * (rndn() - lpy);                      // filtered click
        float click = (rndn() - lpy) * expf(-tt * 260.0f);
        t->buf[i] = (sinf(ph) * 1.1f + click * 0.9f) * expf(-tt * 7.0f);
    }
}

static void render808snare(Track *t)
{
    int len = RATE / 4;
    t->buf = mkbuf(len); t->len = len; t->level = 0.85f;
    float p1 = 0, p2 = 0, lpy = 0;
    for (int i = 0; i < len; i++) {
        float tt = (float)i / RATE;
        p1 += 2 * (float)M_PI * 185.0f / RATE;
        p2 += 2 * (float)M_PI * 330.0f / RATE;
        float w = rndn();
        lpy += 0.25f * (w - lpy);
        float tone = (sinf(p1) + 0.7f * sinf(p2)) * expf(-tt * 32.0f);
        t->buf[i] = tone * 0.6f + (w - lpy) * expf(-tt * 30.0f);
    }
}

static void render909snare(Track *t)
{
    int len = RATE / 4;
    t->buf = mkbuf(len); t->len = len; t->level = 0.85f;
    float p = 0, l1 = 0, l2 = 0;
    for (int i = 0; i < len; i++) {
        float tt = (float)i / RATE;
        p += 2 * (float)M_PI * (190.0f + 60.0f * expf(-tt * 40.0f)) / RATE;
        float w = rndn();
        l1 += 0.45f * (w - l1);
        l2 += 0.15f * (w - l2);
        t->buf[i] = (l1 - l2) * expf(-tt * 24.0f) * 0.9f + sinf(p) * expf(-tt * 26.0f) * 0.45f;
    }
}

// flavor '8' (808, metallic-ish) or '9' (909, brighter noise)
static void renderHat(Track *t, char flavor, int open)
{
    int len = open ? RATE / 2 : RATE / 16;
    t->buf = mkbuf(len); t->len = len; t->level = 0.55f;
    float l1 = 0, l2 = 0;
    float cutoff = (flavor == '9') ? 0.55f : 0.40f;
    float decay = open ? (flavor == '9' ? 5.0f : 4.5f)
                       : (flavor == '9' ? 42.0f : 55.0f);
    for (int i = 0; i < len; i++) {
        float tt = (float)i / RATE;
        float w = rndn();
        l1 += cutoff * (w - l1);
        l2 += 0.14f * (w - l2);
        t->buf[i] = (l1 - l2) * expf(-tt * decay);
    }
}

static void render808clap(Track *t)
{
    int len = RATE / 3;
    t->buf = mkbuf(len); t->len = len; t->level = 0.75f;
    float lpy = 0;
    for (int i = 0; i < len; i++) {
        float tt = (float)i / RATE;
        float w = rndn();
        lpy += 0.30f * (w - lpy);
        float hp = w - lpy;
        float env = 0.0f;
        for (int b = 0; b < 3; b++) {
            float t0 = b * 0.010f;
            if (tt >= t0) env = fmaxf(env, expf(-(tt - t0) * 140.0f));
        }
        float tail = (tt >= 0.030f) ? expf(-(tt - 0.030f) * 18.0f) : 0.0f;
        t->buf[i] = hp * fmaxf(env, tail);
    }
}

static void render909clap(Track *t)
{
    int len = RATE / 2;
    t->buf = mkbuf(len); t->len = len; t->level = 0.75f;
    float lpy = 0;
    for (int i = 0; i < len; i++) {
        float tt = (float)i / RATE;
        float w = rndn();
        lpy += 0.35f * (w - lpy);
        float hp = w - lpy;
        float env = 0.0f;
        for (int b = 0; b < 3; b++) {
            float t0 = b * 0.009f;
            if (tt >= t0) env = fmaxf(env, expf(-(tt - t0) * 120.0f));
        }
        float tail = (tt >= 0.027f) ? expf(-(tt - 0.027f) * 11.0f) : 0.0f;
        t->buf[i] = hp * fmaxf(env, tail * 0.9f);
    }
}

static float sq(float p) { float x = p / (2 * (float)M_PI); return (x - floorf(x)) < 0.5f ? 1.0f : -1.0f; }

static void renderCowbell(Track *t)
{
    int len = RATE / 4;
    t->buf = mkbuf(len); t->len = len; t->level = 0.6f;
    float p1 = 0, p2 = 0;
    for (int i = 0; i < len; i++) {
        float tt = (float)i / RATE;
        p1 += 2 * (float)M_PI * 560.0f / RATE;
        p2 += 2 * (float)M_PI * 845.0f / RATE;
        t->buf[i] = 0.6f * sq(p1) * expf(-tt * 20.0f) + 0.4f * sq(p2) * expf(-tt * 12.0f);
    }
}

static void render909rim(Track *t)
{
    int len = RATE / 20;
    t->buf = mkbuf(len); t->len = len; t->level = 0.7f;
    float p1 = 0, p2 = 0;
    for (int i = 0; i < len; i++) {
        float tt = (float)i / RATE;
        p1 += 2 * (float)M_PI * 1700.0f / RATE;
        p2 += 2 * (float)M_PI * 437.0f / RATE;
        t->buf[i] = (0.5f * sinf(p1) + sinf(p2)) * expf(-tt * 90.0f);
    }
}

static void renderTom(Track *t, float f0, float f1)
{
    int len = RATE / 3;
    t->buf = mkbuf(len); t->len = len; t->level = 0.9f;
    float ph = 0;
    for (int i = 0; i < len; i++) {
        float tt = (float)i / RATE;
        float f = f1 + (f0 - f1) * expf(-tt * 18.0f);
        ph += 2 * (float)M_PI * f / RATE;
        t->buf[i] = sinf(ph) * expf(-tt * 10.0f);
    }
}

// ---------------------------------------------------------------- wav loader

static int loadWav(const char *path, float **out, int *outLen)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, fp) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        fclose(fp); return 0;
    }
    int fmt = -1, ch = 0, rateHz = 0, bits = 0;
    int dataLen = 0;
    for (;;) {
        uint8_t ck[8];
        if (fread(ck, 1, 8, fp) != 8) { fclose(fp); return 0; }
        int sz = ck[4] | (ck[5] << 8) | (ck[6] << 16) | ((int)ck[7] << 24);
        if (!memcmp(ck, "fmt ", 4)) {
            uint8_t fbuf[16];
            int rd = (int)fread(fbuf, 1, sizeof fbuf, fp);
            if (rd >= 16) {
                fmt = fbuf[0] | (fbuf[1] << 8);
                ch = fbuf[2] | (fbuf[3] << 8);
                rateHz = fbuf[4] | (fbuf[5] << 8) | (fbuf[6] << 16) | ((int)fbuf[7] << 24);
                bits = fbuf[14] | (fbuf[15] << 8);
            }
            fseek(fp, (long)(sz - rd), SEEK_CUR);
        } else if (!memcmp(ck, "data", 4)) {
            dataLen = sz; break;
        } else {
            fseek(fp, sz, SEEK_CUR);
        }
    }
    if (fmt != 1 || bits != 16 || ch < 1 || ch > 8 || rateHz < 4000 || dataLen < 2) {
        fclose(fp); return 0;
    }
    int frames = dataLen / 2 / ch;
    if (frames < 2) { fclose(fp); return 0; }
    int16_t *raw = (int16_t *)malloc((size_t)frames * ch * 2);
    if (!raw) { fclose(fp); return 0; }
    if (fread(raw, 2, (size_t)frames * ch, fp) != (size_t)frames * ch) {
        free(raw); fclose(fp); return 0;
    }
    fclose(fp);
    float *mono = (float *)malloc((size_t)frames * sizeof(float));
    if (!mono) { free(raw); return 0; }
    for (int i = 0; i < frames; i++) {
        float s = 0;
        for (int c = 0; c < ch; c++) s += raw[(size_t)i * ch + c];
        mono[i] = s / (ch * 32768.0f);
    }
    free(raw);
    if (rateHz == RATE) { *out = mono; *outLen = frames; return 1; }
    int n = (int)((double)frames * RATE / rateHz);
    if (n < 2) { free(mono); return 0; }
    float *rs = (float *)malloc((size_t)n * sizeof(float));
    if (!rs) { free(mono); return 0; }
    for (int i = 0; i < n; i++) {
        float src = (float)((double)i * rateHz / RATE);
        int i0 = (int)src;
        if (i0 >= frames - 1) { rs[i] = mono[frames - 1]; continue; }
        float fr = src - i0;
        rs[i] = mono[i0] * (1.0f - fr) + mono[i0 + 1] * fr;
    }
    free(mono);
    *out = rs; *outLen = n;
    return 1;
}

// ---------------------------------------------------------------- tracks

static void initTracks(void)
{
    static const char *n808[TBP] = {"808 KICK", "808 SNARE", "808 HAT", "808 OPEN",
                                    "808 CLAP", "COWBELL", "TOM HI", "TOM LO"};
    static const char *n909[TBP] = {"909 KICK", "909 SNARE", "909 HAT", "909 OPEN",
                                    "909 CLAP", "RIMSHOT", "TOM HI", "TOM LO"};
    memset(tr, 0, sizeof tr);
    for (int i = 0; i < TBP; i++) {
        snprintf(tr[i].name, 12, "%s", n808[i]);
        snprintf(tr[TBP + i].name, 12, "%s", n909[i]);
        snprintf(tr[2 * TBP + i].name, 12, "SAMPLE %02d", i + 1);
    }
    render808kick(&tr[0]);  render808snare(&tr[1]);
    renderHat(&tr[2], '8', 0); renderHat(&tr[3], '8', 1);
    render808clap(&tr[4]);  renderCowbell(&tr[5]);
    renderTom(&tr[6], 330, 200); renderTom(&tr[7], 205, 70);

    render909kick(&tr[8]);  render909snare(&tr[9]);
    renderHat(&tr[10], '9', 0); renderHat(&tr[11], '9', 1);
    render909clap(&tr[12]); render909rim(&tr[13]);
    renderTom(&tr[14], 380, 240); renderTom(&tr[15], 250, 80);

    for (int i = 0; i < 2 * TBP; i++) {
        float lvl = tr[i].level;
        norm(&tr[i], 0.9f);
        tr[i].level = lvl;
    }

    for (int i = 0; i < TBP; i++) {
        Track *s = &tr[2 * TBP + i];
        char path[64];
        snprintf(path, sizeof path, SAMPLE_FMT, i + 1);
        float *buf = NULL; int len = 0;
        if (loadWav(path, &buf, &len)) {
            s->buf = buf; s->len = len;
        }
    }
}

// ---------------------------------------------------------------- save/load

static int savePattern(void)
{
    FILE *fp = fopen(CFGPATH, "w");
    if (!fp) return 0;
    fprintf(fp, "bpm %.1f\n", bpm);
    for (int i = 0; i < NTR; i++) {
        fprintf(fp, "%d ", i);
        for (int s = 0; s < NSTEP; s++) fputc(tr[i].pat[s] ? '1' : '0', fp);
        fputc('\n', fp);
    }
    fclose(fp);
    return 1;
}

static void loadPattern(void)
{
    FILE *fp = fopen(CFGPATH, "r");
    if (!fp) return;
    float b;
    if (fscanf(fp, "bpm %f", &b) == 1 && b >= 30.0f && b <= 300.0f) bpm = b;
    for (int i = 0; i < NTR; i++) {
        int idx;
        if (fscanf(fp, "%d ", &idx) != 1 || idx != i) break;
        for (int s = 0; s < NSTEP; s++) {
            int c = fgetc(fp);
            tr[i].pat[s] = (c == '1') ? 1 : 0;
        }
    }
    fclose(fp);
}

// ---------------------------------------------------------------- 5x7 font

static const uint8_t *glyph(char c)
{
    static const uint8_t A[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t B[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    static const uint8_t C[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static const uint8_t D[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    static const uint8_t E[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t F[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t G[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
    static const uint8_t H[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t I_[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const uint8_t J[7] = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C};
    static const uint8_t K[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const uint8_t L[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const uint8_t M[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t N[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t O[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t P[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t Q[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    static const uint8_t R[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    static const uint8_t S[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t T[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t U[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t V[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
    static const uint8_t W[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
    static const uint8_t Xx[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    static const uint8_t Y[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t Z[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    static const uint8_t D0[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static const uint8_t D1[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const uint8_t D2[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    static const uint8_t D3[7] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E};
    static const uint8_t D4[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    static const uint8_t D5[7] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
    static const uint8_t D6[7] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    static const uint8_t D7[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static const uint8_t D8[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    static const uint8_t D9[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
    static const uint8_t SP[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t MNS[7] = {0, 0, 0, 0x1F, 0, 0, 0};
    static const uint8_t SLS[7] = {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
    static const uint8_t DOT[7] = {0, 0, 0, 0, 0, 0x0C, 0x0C};
    static const uint8_t COL[7] = {0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0};
    static const uint8_t GT[7] = {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08};
    static const uint8_t LT[7] = {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02};
    switch (c) {
    case 'A': return A;  case 'B': return B;  case 'C': return C;  case 'D': return D;
    case 'E': return E;  case 'F': return F;  case 'G': return G;  case 'H': return H;
    case 'I': return I_; case 'J': return J;  case 'K': return K;  case 'L': return L;
    case 'M': return M;  case 'N': return N;  case 'O': return O;  case 'P': return P;
    case 'Q': return Q;  case 'R': return R;  case 'S': return S;  case 'T': return T;
    case 'U': return U;  case 'V': return V;  case 'W': return W;  case 'X': return Xx;
    case 'Y': return Y;  case 'Z': return Z;
    case '0': return D0; case '1': return D1; case '2': return D2; case '3': return D3;
    case '4': return D4; case '5': return D5; case '6': return D6; case '7': return D7;
    case '8': return D8; case '9': return D9;
    case '-': return MNS; case '/': return SLS; case '.': return DOT; case ':': return COL;
    case '>': return GT;  case '<': return LT;
    default:  return SP;
    }
}

// ---------------------------------------------------------------- UI

static SDL_Renderer *ren = NULL;

static void rect(int x, int y, int w, int h, int r, int g, int b)
{
    SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, 255);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(ren, &rc);
}

static void frame(int x, int y, int w, int h, int r, int g, int b)
{
    SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, 255);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderDrawRect(ren, &rc);
}

static void text(int x, int y, int sc, const uint8_t c[3], const char *s)
{
    SDL_SetRenderDrawColor(ren, c[0], c[1], c[2], 255);
    while (*s) {
        const uint8_t *g = glyph(*s++);
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g[row] & (1 << col)) {
                    SDL_Rect px = {x + col * sc, y + row * sc, sc, sc};
                    SDL_RenderFillRect(ren, &px);
                }
        x += 6 * sc;
    }
}

static void drawUI(void)
{
    const uint8_t white[3] = {240, 240, 240};
    const uint8_t gray[3] = {120, 120, 130};
    const uint8_t dim[3] = {70, 70, 80};
    const uint8_t *acc = bankCol[bank];

    rect(0, 0, WIN_W, WIN_H, 18, 18, 24);

    // header
    text(24, 22, 3, white, "KNULLI DRUMKIT");
    text(560, 24, 3, (uint8_t *)acc, bankNames[bank]);

    char bpms[16];
    snprintf(bpms, sizeof bpms, "%3d BPM", (int)(bpm + 0.5f));
    text(900, 24, 3, white, bpms);

    const uint8_t *stc = playing ? (uint8_t *)acc : gray;
    text(1120, 24, 3, stc, playing ? "PLAY" : "STOP");

    // playhead markers
    int gx0 = 190, cw = 60, chh = 52, gap = 4;
    int gy0 = 160;
    for (int s = 0; s < NSTEP; s++) {
        int gx = gx0 + s * (cw + gap);
        if (s % 4 == 0) rect(gx, 132, cw, 8, dim[0], dim[1], dim[2]);
        if (playing && s == curStep) {
            rect(gx, 132, cw, 8, acc[0], acc[1], acc[2]);
        }
    }

    // grid
    int top = gy0 + 24;
    for (int r = 0; r < TBP; r++) {
        int ti = bank * TBP + r;
        int y = top + r * (chh + gap);
        uint8_t nc[3];
        nc[0] = (r == curTrack) ? white[0] : dim[0];
        nc[1] = (r == curTrack) ? white[1] : dim[1];
        nc[2] = (r == curTrack) ? white[2] : dim[2];
        text(24, y + 18, 2, nc, tr[ti].name);
        if (!tr[ti].buf || tr[ti].len < 1) {
            uint8_t red[3] = {160, 60, 60};
            text(24 + 10 * 12 + 8, y + 18, 2, red, "-");
        }
        for (int s = 0; s < NSTEP; s++) {
            int gx = gx0 + s * (cw + gap);
            uint8_t c[3];
            if (tr[ti].pat[s]) {
                c[0] = acc[0]; c[1] = acc[1]; c[2] = acc[2];
            } else if (s % 4 == 3) {
                c[0] = 60; c[1] = 60; c[2] = 72;
            } else {
                c[0] = 42; c[1] = 42; c[2] = 52;
            }
            if (playing && s == curStep) {
                c[0] = (uint8_t)(c[0] * 0.6f + 100.0f);
                c[1] = (uint8_t)(c[1] * 0.6f + 100.0f);
                c[2] = (uint8_t)(c[2] * 0.6f + 100.0f);
            }
            rect(gx, y, cw, chh, c[0], c[1], c[2]);
            if (r == curTrack && s == curStepEd)
                frame(gx - 3, y - 3, cw + 6, chh + 6, 255, 255, 255);
        }
    }

    // footer
    int fy = 630;
    text(24, fy, 2, gray, "DPAD:CURSOR   A:TOGGLE   B:PREVIEW   X:Y:BANK   L1/R1:BPM   SELECT:PLAY   START:SAVE   SEL+START:QUIT");
    text(24, fy + 34, 2, dim, "SAMPLER: PUT WAV FILES 01.WAV..08.WAV IN THE SAMPLES FOLDER");
}

// ---------------------------------------------------------------- input

static void bankChange(int d)
{
    bank = (bank + d + NBANK) % NBANK;
    curTrack = 0;
}

static void previewCur(void) { trigger(bank * TBP + curTrack, 1.0f); }

static void quitNow(void)
{
    if (savePattern()) fprintf(stderr, "pattern saved to %s\n", CFGPATH);
    else fprintf(stderr, "warning: could not save pattern\n");
}

static void bpmAdd(int d)
{
    bpm += d;
    if (bpm < 30.0f) bpm = 30.0f;
    if (bpm > 300.0f) bpm = 300.0f;
}

static void action(int act)   // act: 0=up 1=down 2=left 3=right 4=A 5=B 6=prevbank 7=nextbank 8=playstop 9=save 10=qsave
{
    switch (act) {
    case 0: curTrack = (curTrack + TBP - 1) % TBP; break;
    case 1: curTrack = (curTrack + 1) % TBP; break;
    case 2: curStepEd = (curStepEd + NSTEP - 1) % NSTEP; break;
    case 3: curStepEd = (curStepEd + 1) % NSTEP; break;
    case 4: {
        Track *t = &tr[bank * TBP + curTrack];
        t->pat[curStepEd] ^= 1;
        previewCur();
        break;
    }
    case 5: previewCur(); break;
    case 6: bankChange(-1); break;
    case 7: bankChange(1); break;
    case 8: playing = !playing; if (!playing) curStep = -1; break;
    case 9: if (savePattern()) fprintf(stderr, "pattern saved\n"); break;
    case 10: quitNow(); SDL_Quit(); exit(0);
    }
}

#ifdef DK_TEST
static int selftest(void);
#endif

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    setbuf(stdout, NULL);
#ifdef DK_TEST
    if (argc > 1 && !strcmp(argv[1], "--selftest")) return selftest();
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        SDL_Log("SDL init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    SDL_Window *win = NULL;
    if (SDL_CreateWindowAndRenderer(WIN_W, WIN_H, 0, &win, &ren) != 0) {
        SDL_Log("window failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetWindowTitle(win, "KNULLI DRUMKIT");
    SDL_RenderSetLogicalSize(ren, WIN_W, WIN_H);

    SDL_AudioSpec want, got;
    memset(&want, 0, sizeof want);
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audioCB;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (dev == 0) {
        SDL_Log("audio failed: %s", SDL_GetError());
        return 1;
    }
    SDL_PauseAudioDevice(dev, 0);

    initTracks();
    loadPattern();

    if (SDL_NumJoysticks() > 0 && SDL_IsGameController(0))
        SDL_GameControllerOpen(0);
    else if (SDL_NumJoysticks() > 0)
        SDL_JoystickOpen(0);

    for (;;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                quitNow(); SDL_Quit(); return 0;

            case SDL_CONTROLLERBUTTONDOWN: {
                Uint8 b = ev.cbutton.button;
                if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) action(0);
                else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) action(1);
                else if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT) action(2);
                else if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) action(3);
                else if (b == SDL_CONTROLLER_BUTTON_A) action(4);
                else if (b == SDL_CONTROLLER_BUTTON_B) action(5);
                else if (b == SDL_CONTROLLER_BUTTON_Y) action(6);
                else if (b == SDL_CONTROLLER_BUTTON_X) action(7);
                else if (b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) bpmAdd(-2);
                else if (b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) bpmAdd(2);
                else if (b == SDL_CONTROLLER_BUTTON_BACK) action(8);
                else if (b == SDL_CONTROLLER_BUTTON_START) {
                    if (SDL_GetControllerButton(0, SDL_CONTROLLER_BUTTON_BACK)) action(10);
                    else action(9);
                }
                break;
            }

            case SDL_JOYBUTTONDOWN: {
                int b = ev.jbutton.button;
                if (b == 0) action(4);
                else if (b == 1) action(5);
                else if (b == 3) action(6);
                else if (b == 2) action(7);
                else if (b == 6) bpmAdd(-2);
                else if (b == 7) bpmAdd(2);
                else if (b == 4) {
                    if (SDL_JoystickGetButton(SDL_JoystickFromInstanceID(ev.jbutton.which), 5)) action(10);
                    else action(8);
                }
                else if (b == 5) action(9);
                break;
            }

            case SDL_JOYHATMOTION: {
                switch (ev.jhat.value) {
                case SDL_HAT_UP: action(0); break;
                case SDL_HAT_DOWN: action(1); break;
                case SDL_HAT_LEFT: action(2); break;
                case SDL_HAT_RIGHT: action(3); break;
                default: break;
                }
                break;
            }

            case SDL_KEYDOWN: {   // desktop fallback for testing
                switch (ev.key.keysym.sym) {
                case SDLK_UP: action(0); break;
                case SDLK_DOWN: action(1); break;
                case SDLK_LEFT: action(2); break;
                case SDLK_RIGHT: action(3); break;
                case SDLK_a: action(4); break;
                case SDLK_s: action(5); break;
                case SDLK_q: action(6); break;
                case SDLK_w: action(7); break;
                case SDLK_z: bpmAdd(-2); break;
                case SDLK_x: bpmAdd(2); break;
                case SDLK_SPACE: action(8); break;
                case SDLK_RETURN: action(9); break;
                case SDLK_ESCAPE: action(10); break;
                default: break;
                }
                break;
            }

            case SDL_KEYUP:
            default: break;
            }
        }

        drawUI();
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
}

// ---------------------------------------------------------------- selftest
// compiled only with -DDK_TEST; run with ./drumkit --selftest

#ifdef DK_TEST
#include <unistd.h>
#include <sys/stat.h>

static int selftest(void)
{
    mkdir("/tmp/dktest_self", 0755);
    mkdir("/tmp/dktest_self/samples", 0755);
    chdir("/tmp/dktest_self");

    int fail = 0;
    long check = 0;

    // pattern save/load round trip
    for (int i = 0; i < NTR; i++) memset(tr[i].pat, 0, NSTEP);
    tr[0].pat[0] = 1; tr[0].pat[15] = 1; tr[23].pat[7] = 1;
    bpm = 137.5f;
    check += (savePattern() == 1);
    for (int i = 0; i < NTR; i++) memset(tr[i].pat, 0, NSTEP);
    bpm = 60.0f;
    loadPattern();
    check += (tr[0].pat[0] == 1 && tr[0].pat[15] == 1 && tr[23].pat[7] == 1);
    check += (tr[1].pat[3] == 0 && tr[23].pat[6] == 0);
    check += (bpm > 137.4f && bpm < 137.6f);
    if (check != 4) {
        fprintf(stderr, "FAIL: save/load check=%ld\n", check);
        fprintf(stderr, "  tr[0].pat[0]=%d tr[0].pat[15]=%d tr[23].pat[7]=%d bpm=%f\n",
                tr[0].pat[0], tr[0].pat[15], tr[23].pat[7], bpm);
        fail = 1;
    }

    // 16-bit mono 44.1 kHz wav passthrough
    FILE *f = fopen("samples/01.wav", "wb");
    if (!f) return 2;
    int n = 1000;
    int16_t *d = calloc(n, 2);
    for (int i = 0; i < n; i++) d[i] = (int16_t)(1000 + i);
    int32_t chunk = 36 + n * 2, sr = 44100, fsz = 16, br = sr * 2, dl = n * 2;
    short af2 = 1, cn2 = 1, ba = 2, bd = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&chunk, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fsz, 4, 1, f);
    fwrite(&af2, 2, 1, f); fwrite(&cn2, 2, 1, f);
    fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bd, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dl, 4, 1, f); fwrite(d, 2, n, f); fclose(f);
    free(d);

    // 16-bit stereo 22.05 kHz wav -> mono 44.1 kHz (L+R cancels to 0)
    f = fopen("samples/02.wav", "wb");
    if (!f) return 2;
    int m = 500;
    int16_t *d2 = calloc(m * 2, 2);
    for (int i = 0; i < m; i++) { d2[2 * i] = 32000; d2[2 * i + 1] = -32000; }
    chunk = 36 + m * 4; sr = 22050; br = sr * 4; ba = 4; dl = m * 4;
    af2 = 1; cn2 = 2;
    fwrite("RIFF", 1, 4, f); fwrite(&chunk, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fsz, 4, 1, f);
    fwrite(&af2, 2, 1, f); fwrite(&cn2, 2, 1, f);
    fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bd, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dl, 4, 1, f); fwrite(d2, 2, m * 2, f); fclose(f);
    free(d2);

    initTracks();
    long w2 = (tr[2 * TBP + 0].len == 1000);
    w2 += (fabsf(tr[2 * TBP + 0].buf[500] - (1500.0f / 32768.0f)) < 1e-4);
    w2 += (tr[2 * TBP + 1].len == 1000);
    w2 += (fabsf(tr[2 * TBP + 1].buf[500]) < 1e-4);
    w2 += (tr[2 * TBP + 2].len == 0);
    if (w2 != 5) { fprintf(stderr, "FAIL: wav loader (%ld/5)\n", w2); fail = 1; }

    // synthesized voices exist and are in range
    long w3 = 1;
    for (int i = 0; i < 2 * TBP; i++) {
        if (!tr[i].buf || tr[i].len < 100) w3 = 0;
        for (int j = 0; j < tr[i].len && w3; j += 97) {
            if (!(tr[i].buf[j] >= -1.0001f && tr[i].buf[j] <= 1.0001f)) {
                fprintf(stderr, "  synth %d (%s) out of range at %d: %f\n",
                        i, tr[i].name, j, tr[i].buf[j]);
                w3 = 0;
            }
        }
    }
    if (!w3) { fprintf(stderr, "FAIL: synthesis\n"); fail = 1; }

    printf(fail ? "SELFTEST FAILED\n" : "SELFTEST PASSED\n");
    return fail;
}
#endif
