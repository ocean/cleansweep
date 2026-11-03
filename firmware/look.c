/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Drew Robinson */
/* The look, with no hardware in it: the deep violet screen, the words, CLEAN and SWEEP zipping in, and the
 * progress bar's fizz. The firmware (cleansweep.c) and the host test (tests/look_test.c) give it the screen and the
 * time.
 *
 * The colours follow FM-SWITCH's launch screen: deep violet, darker towards the bottom, a violet glow behind the
 * name, white words. Every pixel is worked out in 8-bit colour and dithered to the panel's RGB565, so the gradient
 * doesn't band.
 *
 * What is on the screen is a scene (texts at places, the bar, the sparks), and any rectangle of it can be drawn
 * again from that: an animation draws only what moved.
 *
 * Hooks:
 *   lk_window(x, y, w, h)  where the next h rows of w pixels go
 *   lk_row(px, w)          one of those rows, RGB565 (the hook may change px)
 *   lk_ms()                milliseconds, counting up
 *   lk_idle()              between frames (the watchdog)
 */
#include <stdint.h>

#define LK_W 240
#define LK_H 240

static void lk_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
static void lk_row(uint16_t *px, uint32_t w);
static uint32_t lk_ms(void);
static void lk_idle(void);

typedef struct { int32_t r, g, b; } lk_rgb_t;

/* FM-SWITCH's launch violet (0x2B1260 to 0x0C0618, its glow 0x6D28D9), with more red: on the FM-1's panel its own
 * values looked blue and greyish (Drew's photos, 3 Nov). */
static const lk_rgb_t LK_TOP = {0x4C, 0x14, 0x78};
static const lk_rgb_t LK_BOTTOM = {0x26, 0x08, 0x40};
static const lk_rgb_t LK_GLOW = {0x8C, 0x2C, 0xE0};       /* behind the name */
static const lk_rgb_t LK_WHITE = {0xFF, 0xFF, 0xFF};
static const lk_rgb_t LK_LAVENDER = {0xB7, 0x9C, 0xFF};   /* its violet phosphor */
static const lk_rgb_t LK_TRACK = {0x60, 0x2E, 0x98};      /* the bar still to go */
#define LK_GLOW_X 120
#define LK_GLOW_Y 46
#define LK_GLOW_R2 (150 * 150)
#define LK_GLOW_MAX 110u                                  /* of 256, at the glow's middle */

#define LK_BAR_X 16
#define LK_BAR_W 208
#define LK_FIZZ CS_FIZZ                                   /* rows above and below the bar the sparks may reach */
#define LK_SPARKS 40
#define LK_SPARK_R 3                                       /* a new spark's arms, in pixels */
#define LK_TICK 16u                                       /* ms: the sparks move this often, however fast frames are */

static lk_rgb_t lk_line[LK_W];
static uint16_t lk_px[LK_W];

/* a + (b - a) * t / 256 */
static lk_rgb_t lk_mix(lk_rgb_t a, lk_rgb_t b, int32_t t)
{
    a.r += (b.r - a.r) * t / 256;
    a.g += (b.g - a.g) * t / 256;
    a.b += (b.b - a.b) * t / 256;
    return a;
}

static lk_rgb_t lk_bg(int32_t x, int32_t y)
{
    int32_t dx = x - LK_GLOW_X, dy = y - LK_GLOW_Y, d2 = dx * dx + dy * dy, f;
    lk_rgb_t c = lk_mix(LK_TOP, LK_BOTTOM, y * 256 / (LK_H - 1));
    if (d2 >= LK_GLOW_R2)
        return c;
    f = (LK_GLOW_R2 - d2) * 256 / LK_GLOW_R2;             /* 0 at the edge, 256 in the middle: eased below */
    return lk_mix(c, LK_GLOW, f * f / 256 * (int32_t)LK_GLOW_MAX / 256);
}

/* lk_line[x, x + w) on row y out to the screen, dithered (4x4 ordered) to RGB565 */
static void lk_out(uint32_t x, uint32_t y, uint32_t w)
{
    static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    uint32_t i;
    for (i = 0; i < w; i++) {
        lk_rgb_t c = lk_line[x + i];
        uint32_t d = bayer[(y & 3u) * 4u + ((x + i) & 3u)];
        uint32_t r = ((uint32_t)c.r * 497u >> 8) + d, g = ((uint32_t)c.g * 1012u >> 8) + d,
                 b = ((uint32_t)c.b * 497u >> 8) + d;          /* in 16ths of a 5- or 6-bit step */
        lk_px[i] = (uint16_t)((r >> 4) << 11 | (g >> 4) << 5 | (b >> 4));
    }
    lk_row(lk_px, w);
}

/* ---- the scene ---- */
typedef struct { const cs_text_t *t; int32_t x; uint32_t from, w; } lk_item_t;   /* columns [from, from + w) of t */
static lk_item_t lk_items[12];
static uint32_t lk_n;
static uint32_t lk_bar_on, lk_bar_px;                     /* the bar, and how much of it is filled */
static struct { int32_t x, y, vx, vy; uint32_t age, life; } lk_spark[LK_SPARKS];   /* x, y in 8ths of a pixel */
static uint32_t lk_rng = 0x9E3779B9u;

static uint32_t lk_rand(uint32_t n)                       /* 0 .. n - 1 */
{
    lk_rng ^= lk_rng << 13;
    lk_rng ^= lk_rng >> 17;
    lk_rng ^= lk_rng << 5;
    return lk_rng % n;
}

static void lk_clear(void)
{
    uint32_t i;
    lk_n = 0;
    lk_bar_on = lk_bar_px = 0;
    for (i = 0; i < LK_SPARKS; i++)
        lk_spark[i].life = 0;
}

static void lk_put(const cs_text_t *t, int32_t x, uint32_t from, uint32_t w)
{
    if (lk_n < sizeof lk_items / sizeof lk_items[0]) {
        lk_items[lk_n].t = t;
        lk_items[lk_n].x = x;
        lk_items[lk_n].from = from;
        lk_items[lk_n].w = w;
        lk_n++;
    }
}

static void lk_add(const cs_text_t *t) { if (t) lk_put(t, t->x, 0, t->w); }

/* one row of an item's runs from *i into lk_line[lo, hi); hi == 0 only skips the row */
static void lk_item_row(const lk_item_t *it, uint32_t *i, int32_t lo, int32_t hi)
{
    uint32_t col = 0, n, level;
    while (col < it->t->w) {
        uint8_t b = it->t->rle[(*i)++];
        level = b >> 4u;
        for (n = (b & 15u) + 1u; n--; col++) {
            int32_t at = it->x + (int32_t)col - (int32_t)it->from;
            if (level && col - it->from < it->w && at >= lo && at < hi)
                lk_line[at] = lk_mix(lk_line[at], LK_WHITE, (int32_t)(level * 17u + (level >> 3)));
        }
    }
}

static void lk_bar_row(int32_t y, int32_t lo, int32_t hi)
{
    int32_t x, end = LK_BAR_X + LK_BAR_W - 1, fill = LK_BAR_X + (int32_t)lk_bar_px, top = CS_BAR_Y,
            bottom = CS_BAR_Y + CS_BAR_H - 1;
    if (!lk_bar_on || y < top || y > bottom)
        return;
    for (x = lo > LK_BAR_X ? lo : LK_BAR_X; x < hi && x <= end; x++) {
        if ((x == LK_BAR_X || x == end) && (y == top || y == bottom))
            continue;                                     /* rounded ends */
        if (x >= fill)
            lk_line[x] = LK_TRACK;
        else if (x >= fill - 3 && lk_bar_px < LK_BAR_W)
            lk_line[x] = LK_LAVENDER;                     /* the glowing head */
        else
            lk_line[x] = LK_WHITE;
    }
}

static void lk_spark_row(int32_t y, int32_t lo, int32_t hi)
{
    uint32_t i;
    for (i = 0; i < LK_SPARKS; i++) {
        int32_t sx = lk_spark[i].x >> 3, sy = lk_spark[i].y >> 3, a, k;
        uint32_t age = lk_spark[i].age, life = lk_spark[i].life;
        lk_rgb_t c;
        int32_t size;
        if (!life)
            continue;
        size = LK_SPARK_R - (int32_t)(age * 3u / life);   /* a star, shrinking a step each third of its life */
        if (y < sy - size || y > sy + size)
            continue;
        a = 256 - (int32_t)(age * 256u / life);           /* fading out */
        if (age & 2u)
            a = a * 5 / 8;                                /* the twinkle */
        c = lk_mix(LK_WHITE, LK_LAVENDER, (int32_t)(age * 512u / life > 256u ? 256u : age * 512u / life));
        for (k = -size; k <= size; k++) {                 /* the row through its middle, or one point of an arm */
            int32_t x = sx + k, d = k < 0 ? -k : k;
            if (y != sy && k)
                continue;
            if (y != sy)
                d = y < sy ? sy - y : y - sy;
            if (x >= lo && x < hi)
                lk_line[x] = lk_mix(lk_line[x], c, a * (size + 1 - d) / (size + 1));
        }
    }
}

/* the scene, drawn again in [x, x + w) x [y, y + h) */
static void lk_redraw(int32_t x, int32_t y, int32_t w, int32_t h)
{
    uint32_t at[sizeof lk_items / sizeof lk_items[0]], k;
    int32_t row, col;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LK_W) w = LK_W - x;
    if (y + h > LK_H) h = LK_H - y;
    if (w <= 0 || h <= 0)
        return;
    for (k = 0; k < lk_n; k++) {                          /* each item's runs, up to the first row drawn */
        at[k] = 0;
        for (row = lk_items[k].t->y; row < y && row < lk_items[k].t->y + lk_items[k].t->h; row++)
            lk_item_row(&lk_items[k], &at[k], 0, 0);
    }
    lk_window((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h);
    for (row = y; row < y + h; row++) {
        for (col = x; col < x + w; col++)
            lk_line[col] = lk_bg(col, row);
        for (k = 0; k < lk_n; k++)
            if (row >= lk_items[k].t->y && row < lk_items[k].t->y + lk_items[k].t->h)
                lk_item_row(&lk_items[k], &at[k], x, x + w);
        lk_bar_row(row, x, x + w);
        lk_spark_row(row, x, x + w);
        lk_out((uint32_t)x, (uint32_t)row, (uint32_t)w);
    }
}

/* a screen: the name and up to three texts */
static void lk_screen(const cs_text_t *a, const cs_text_t *b, const cs_text_t *c)
{
    lk_clear();
    lk_add(&TX_CLEAN);
    lk_add(&TX_SWEEP);
    lk_add(a);
    lk_add(b);
    lk_add(c);
    lk_redraw(0, 0, LK_W, LK_H);
}

/* a number in hex at x on the digits' line (the error's sector) */
static int32_t lk_hex(int32_t x, uint32_t v, uint32_t digits)
{
    int32_t x0 = x;
    while (digits--) {
        lk_put(&TX_DIGITS, x, ((v >> (4u * digits)) & 15u) * CS_DIGIT_W, CS_DIGIT_W);
        x += CS_DIGIT_W;
    }
    lk_redraw(x0, TX_DIGITS.y, x - x0, TX_DIGITS.h);
    return x;
}

/* ---- CLEAN zips in from the left and SWEEP from the right, each overshooting a little and settling ---- */
#define LK_ZIP_MS 460u
#define LK_ZIP_GAP 110u                                   /* SWEEP sets off this much after CLEAN */

static int32_t lk_ease(uint32_t t)                        /* 0..256 -> 0..256, over and back (ease-out-back) */
{
    int32_t s = 256 - (int32_t)t, s2 = s * s / 256;
    return 256 - s2 * (614 * s / 256 - 358) / 256;
}

static int32_t lk_zip(int32_t from, int32_t to, uint32_t ms)
{
    return ms >= LK_ZIP_MS ? to : from + (to - from) * lk_ease(ms * 256u / LK_ZIP_MS) / 256;
}

static void lk_intro(void)
{
    uint32_t t0 = lk_ms(), t;
    int32_t was_clean = 1 << 20, was_sweep = 1 << 20;
    lk_clear();
    lk_put(&TX_CLEAN, -(int32_t)TX_CLEAN.w, 0, TX_CLEAN.w);
    lk_put(&TX_SWEEP, LK_W, 0, TX_SWEEP.w);
    do {
        t = lk_ms() - t0;
        lk_items[0].x = lk_zip(-(int32_t)TX_CLEAN.w - 8, TX_CLEAN.x, t);
        lk_items[1].x = t < LK_ZIP_GAP ? LK_W + 8 : lk_zip(LK_W + 8, TX_SWEEP.x, t - LK_ZIP_GAP);
        if (lk_items[0].x != was_clean)                   /* each word's band, the whole width: no trail */
            lk_redraw(0, TX_CLEAN.y, LK_W, TX_CLEAN.h);
        if (lk_items[1].x != was_sweep)
            lk_redraw(0, TX_SWEEP.y, LK_W, TX_SWEEP.h);
        was_clean = lk_items[0].x;
        was_sweep = lk_items[1].x;
        lk_idle();
    } while (t < LK_ZIP_MS + LK_ZIP_GAP);
}

/* ---- the bar and its fizz ---- */
static uint32_t lk_tick;                                  /* when the sparks last moved */
static int32_t lk_drawn_lo, lk_drawn_hi;                  /* the columns the last frame's sparks covered */

static void lk_bar_start(void)
{
    uint32_t i;
    lk_bar_on = 1;
    lk_bar_px = 0;
    for (i = 0; i < LK_SPARKS; i++)
        lk_spark[i].life = 0;
    lk_tick = lk_ms();
    lk_drawn_lo = lk_drawn_hi = LK_BAR_X;
    lk_redraw(0, CS_BAR_Y - LK_FIZZ, LK_W, CS_BAR_H + 2 * LK_FIZZ);
}

/* the sparks move one tick: the old fade, and while the bar is still going, new ones fizz off its head */
static void lk_fizz(void)
{
    uint32_t i, born = 0;
    int32_t head = LK_BAR_X + (int32_t)lk_bar_px;
    for (i = 0; i < LK_SPARKS; i++) {
        if (lk_spark[i].life) {
            if (++lk_spark[i].age >= lk_spark[i].life) {
                lk_spark[i].life = 0;
                continue;
            }
            lk_spark[i].x += lk_spark[i].vx;
            lk_spark[i].y += lk_spark[i].vy;
            lk_spark[i].vy = lk_spark[i].vy * 7 / 8;      /* bubbles slowing as they rise */
            if ((lk_spark[i].y >> 3) < CS_BAR_Y - LK_FIZZ + LK_SPARK_R
                || (lk_spark[i].y >> 3) > CS_BAR_Y + CS_BAR_H + LK_FIZZ - 1 - LK_SPARK_R
                || (lk_spark[i].x >> 3) < LK_SPARK_R || (lk_spark[i].x >> 3) > LK_W - 1 - LK_SPARK_R)
                lk_spark[i].life = 0;                     /* (a star's arms stay in the fizz's rows) */
        } else if (born < 3u && lk_bar_px < LK_BAR_W && lk_rand(4)) {
            born++;
            lk_spark[i].x = (head - (int32_t)lk_rand(4)) * 8;
            lk_spark[i].y = (CS_BAR_Y + (int32_t)lk_rand(CS_BAR_H)) * 8;
            lk_spark[i].vx = 3 - (int32_t)lk_rand(14);    /* mostly left, falling behind the head */
            lk_spark[i].vy = lk_rand(10) < 7 ? -(6 + (int32_t)lk_rand(16)) : 5 + (int32_t)lk_rand(10);
            lk_spark[i].age = 0;
            lk_spark[i].life = 10 + lk_rand(16);
        }
    }
}

/* draw what changed since the last frame: the bar's new part and wherever sparks are or were */
static void lk_bar_frame(uint32_t was_px)
{
    int32_t lo = LK_BAR_X + (int32_t)(was_px < lk_bar_px ? was_px : lk_bar_px) - 4,
            hi = LK_BAR_X + (int32_t)(was_px > lk_bar_px ? was_px : lk_bar_px) + 1, slo = 1 << 20, shi = -1;
    uint32_t i;
    for (i = 0; i < LK_SPARKS; i++)
        if (lk_spark[i].life) {
            int32_t x = lk_spark[i].x >> 3;
            slo = x - LK_SPARK_R < slo ? x - LK_SPARK_R : slo;
            shi = x + LK_SPARK_R + 1 > shi ? x + LK_SPARK_R + 1 : shi;
        }
    if (lk_drawn_lo < lk_drawn_hi) {
        lo = lk_drawn_lo < lo ? lk_drawn_lo : lo;
        hi = lk_drawn_hi > hi ? lk_drawn_hi : hi;
    }
    if (slo < shi) {
        lo = slo < lo ? slo : lo;
        hi = shi > hi ? shi : hi;
    }
    lk_drawn_lo = slo < shi ? slo : 0;
    lk_drawn_hi = slo < shi ? shi : 0;
    lk_redraw(lo, CS_BAR_Y - LK_FIZZ, hi - lo, CS_BAR_H + 2 * LK_FIZZ);
}

/* the bar at done of total, fizzing for at least min_ms (at least one frame) */
static void lk_bar(uint32_t done, uint32_t total, uint32_t min_ms)
{
    uint32_t t0 = lk_ms(), was = lk_bar_px, ticks;
    lk_bar_px = total ? done * LK_BAR_W / total : 0;
    if (lk_bar_px < was)
        lk_redraw(LK_BAR_X, CS_BAR_Y, LK_BAR_W, CS_BAR_H);
    do {
        for (ticks = 0; lk_ms() - lk_tick >= LK_TICK; ticks++) {
            lk_tick += LK_TICK;
            if (ticks < 4u)                               /* after a long wait (an erase), only a little catching up */
                lk_fizz();
            else
                lk_tick = lk_ms();
        }
        lk_bar_frame(was);
        was = lk_bar_px;
        lk_idle();
    } while (lk_ms() - t0 < min_ms);
}
