/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Drew Robinson */
/* The look and the lights (firmware/look.c, firmware/lights.c) on a pretend screen and LEDs, with a pretend clock:
 * a start as the firmware runs it (the name zipping in and the light show, the bar fizzing through a reset with
 * erases in it, the done screen and PLAY's flashes), then the other screens. Checks that every drawing stays on the
 * screen and fills its window exactly, that an animation leaves nothing behind (the screen after it is the screen
 * drawn afresh), that the bar fills, that the background is violet, and that the light goes round in its order.
 *   tests/run_tests.sh
 *   look_test --frames DIR    also the start as frames (tools/preview.py makes them a picture) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "screens.h"
#include "../firmware/look.c"
#include "../firmware/lights.c"

#define RS_NSEC_TEST 105u                                /* reset_core.c's RS_NSEC: the sectors a reset goes through */

static uint16_t fb[LK_H][LK_W], fresh[LK_H][LK_W];
static uint32_t win_x, win_y, win_w, win_h, win_row, clock_ms, next_frame, frame_no, windows;
static uint8_t leds[LT_NLV];
static uint32_t lt_t0;
static int (*lt_now)(uint32_t, uint8_t *);
static const char *frames_dir;
static int failures;

#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void lk_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (win_row != win_h || !w || !h || x + w > LK_W || y + h > LK_H) {
        printf("FAIL: window %u,%u %ux%u (the last one %u of %u rows)\n", x, y, w, h, win_row, win_h);
        exit(1);
    }
    win_x = x; win_y = y; win_w = w; win_h = h; win_row = 0;
    windows++;
}

static void lk_row(uint16_t *px, uint32_t w)
{
    if (w != win_w || win_row >= win_h) {
        printf("FAIL: a row of %u in a window of %ux%u, row %u\n", w, win_w, win_h, win_row);
        exit(1);
    }
    memcpy(&fb[win_y + win_row][win_x], px, w * 2u);
    win_row++;
}

static uint32_t lk_ms(void) { return clock_ms; }

static void dump(const char *name)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", frames_dir, name);
    if (!(f = fopen(path, "wb"))) {
        printf("FAIL: can't write %s\n", path);
        exit(1);
    }
    fwrite(fb, sizeof fb, 1, f);                         /* the screen, RGB565, then the LEDs' levels */
    fwrite(leds, sizeof leds, 1, f);
    fclose(f);
}

/* the clock on by n ms, taking a frame every 40 ms when asked (the lights moving as the firmware moves them) */
static void advance(uint32_t n)
{
    while (n--) {
        clock_ms++;
        if (lt_now && !lt_now(clock_ms - lt_t0, leds))
            lt_now = 0;
        if (frames_dir && clock_ms >= next_frame) {
            char name[32];
            snprintf(name, sizeof name, "f%04u.raw", frame_no++);
            dump(name);
            next_frame += 40u;
        }
    }
}

static void lk_idle(void) { advance(1); }

static void lights_start(int (*show)(uint32_t, uint8_t *))
{
    lt_t0 = clock_ms;
    lt_now = show;
}

/* the screen as it is, against the scene drawn afresh: anything an animation left behind shows up here */
static void same_as_fresh(const char *when)
{
    uint32_t x, y, diff = 0;
    memcpy(fresh, fb, sizeof fb);
    lk_redraw(0, 0, LK_W, LK_H);
    for (y = 0; y < LK_H; y++)
        for (x = 0; x < LK_W; x++)
            diff += fresh[y][x] != fb[y][x];
    CHECK(diff == 0, "%s: %u pixels differ from the screen drawn afresh", when, diff);
}

static void rgb(uint16_t c, uint32_t *r, uint32_t *g, uint32_t *b)
{
    *r = (c >> 11) * 255u / 31u;
    *g = (c >> 5 & 63u) * 255u / 63u;
    *b = (c & 31u) * 255u / 31u;
}

static void test_start(void)
{
    uint32_t s, i, r, g, b, first[LT_N], play_on = 0, was = 0;
    clock_ms = 0;
    lk_clear();
    lk_redraw(0, 0, LK_W, LK_H);
    rgb(fb[LK_H - 1][0], &r, &g, &b);
    CHECK(b > r && r > g && b < 90u, "the bottom corner isn't deep violet: %u %u %u", r, g, b);
    rgb(fb[LK_GLOW_Y][LK_GLOW_X], &r, &g, &b);
    CHECK(b > 120u && r > 60u, "no glow behind the name: %u %u %u", r, g, b);

    lights_start(lt_show);
    lk_intro();
    same_as_fresh("after CLEAN and SWEEP zip in");
    CHECK(lk_items[0].x == TX_CLEAN.x && lk_items[1].x == TX_SWEEP.x, "the name isn't in its place");

    lk_screen(&TX_CLEARING, &TX_LEAVE_ON, 0);
    lk_bar_start();
    while (lt_now)
        lk_idle();
    for (s = 0; s < RS_NSEC_TEST; s++) {                /* a reset: the sectors in the middle erased (~45 ms each) */
        lk_bar(s, RS_NSEC_TEST, 12);
        if (s >= 20u && s < 70u)
            advance(45);
    }
    lk_bar(RS_NSEC_TEST, RS_NSEC_TEST, 400);
    rgb(fb[CS_BAR_Y + CS_BAR_H / 2][LK_BAR_X + LK_BAR_W - 3], &r, &g, &b);
    CHECK(r > 240u && g > 240u && b > 240u, "the bar isn't full and white at its end: %u %u %u", r, g, b);
    for (i = 0; i < LK_SPARKS; i++)
        CHECK(!lk_spark[i].life, "a spark still alive after the bar settled");
    same_as_fresh("after the bar's fizz");

    lk_screen(&TX_DONE, &TX_NEXT, &TX_BLUETOOTH);
    lights_start(lt_done);
    for (i = 0; i < 1500u; i++) {
        lk_idle();
        play_on += leds[LT_GREEN] == LT_LIT && !was;
        was = leds[LT_GREEN] == LT_LIT;
        CHECK(!was || leds[LT_PLAY] == LT_DARK, "PLAY's red LED on with its green at %u ms", i);
    }
    CHECK(play_on == LT_FLASHES, "PLAY flashed green %u times when done, not %u", play_on, LT_FLASHES);
    next_frame = ~0u;                                    /* the frames are the start; the rest are pictures */
    if (frames_dir)
        dump("done.raw");                                /* what tools/hw_check.py expects the real package to show */

    /* the light show on its own: each LED lit first in the order of the way round, and all left in the glow */
    for (i = 0; i < LT_N; i++)
        first[i] = ~0u;
    for (s = 0; s < LT_SHOW_MS + 100u; s++) {
        uint8_t lv[LT_NLV];
        int on = lt_show(s, lv);
        CHECK(on == (s < LT_SHOW_MS), "the show says it's running at %u ms: %d", s, on);
        for (i = 0; i < LT_N; i++)
            if (lv[i] == LT_LIT && first[i] == ~0u)
                first[i] = s;
        if (!on)
            for (i = 0; i < LT_N; i++)
                CHECK(lv[i] == LT_GLOW, "LED %u isn't left in the glow", i);
    }
    for (i = 1; i < LT_N; i++)
        CHECK(first[LT_PATH[i]] > first[LT_PATH[i - 1]], "LED %u lit before LED %u", LT_PATH[i], LT_PATH[i - 1]);
    for (i = 0; i < LT_N; i++)
        CHECK(first[i] != ~0u, "LED %u never lit", i);
}

static void test_screens(void)
{
    lk_screen(&TX_WRONG, &TX_AGAIN, 0);
    lk_hex(lk_hex(LK_W / 2 - 4 * (int32_t)CS_DIGIT_W, 4, 1) + (int32_t)CS_DIGIT_W, 0xA0000u, 5);
    same_as_fresh("the error's digits");
    if (frames_dir)
        dump("wrong.raw");
    lk_screen(&TX_INSTALLING, 0, 0);
    lk_bar_start();
    lk_bar(1, 5, 0);
    lk_bar(3, 5, 200);
    if (frames_dir)
        dump("installing.raw");
    lk_bar(0, 5, 0);                                     /* the bar going back (a new install) */
    lk_bar(5, 5, 400);
    same_as_fresh("the install's bar");
    lk_screen(&TX_WRONG, &TX_STOPPED, 0);
    if (frames_dir)
        dump("stopped.raw");
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--frames"))
        frames_dir = argv[2];
    test_start();
    test_screens();
    if (failures) {
        printf("look_test: %d FAILED\n", failures);
        return 1;
    }
    printf("look_test: ok (%u windows drawn, %u frames)\n", windows, frame_no);
    return 0;
}
