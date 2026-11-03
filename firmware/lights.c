/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Drew Robinson */
/* The lights, with no hardware in it: a light runs once round the FM-1, OCT+, OCT-, along the keys from left to
 * right, then round the buttons (the top row from the right, the bottom row from the left), and PLAY flashes green. The
 * firmware (cleansweep.c) puts the levels on the LEDs; the host test (tests/look_test.c) checks them.
 *
 * Every LED is one colour, set by the hardware. PLAY has two: its own, id 12, which is red (seen on hardware), and a
 * green one outside the key matrix (LT_GREEN: column 8, PA9, as ESTAMPIE's ui_input.c found it). The show ends on
 * the green, a friendly "go", with PLAY's red dark meanwhile so the flash reads as pure green. Levels, as the board's
 * scan can show them (felucca-bsp fm1_input.h): LT_LIT, LT_MID (lit 1 frame in 8), LT_GLOW (~1/30 of lit, which the
 * eye reads as nearly lit) and dark. The light has a short tail; the LEDs it has passed keep the glow.
 *
 * Ids are the board's (fm1_input.h FM1_KEYMAP): buttons 0..13, the keys 14..40 from F3, and LT_GREEN after them. The
 * buttons' places are ESTAMPIE's panel table (panel.c PANEL_DEFAULT; PLAY and REC measured on hardware). */
#include <stdint.h>

#define LT_N 41u                                          /* the LEDs in the key matrix, and the way round */
#define LT_GREEN 41u                                      /* PLAY's green LED, outside the matrix */
#define LT_NLV 42u                                        /* every level a show gives */
enum { LT_DARK, LT_GLOW, LT_MID, LT_LIT };
#define LT_PLAY 12u                                       /* PLAY's own LED: red */
#define LT_STEP_MS 36u                                    /* the light moves on one LED this often */
#define LT_FLASH_MS 160u                                  /* PLAY: lit this long, then the glow as long */
#define LT_FLASHES 3u

/* the way round: OCT+, OCT-, F3..G5, GLO EDIT LFO ENV SEL FX, HOME SAVE ARP SEQ PLAY REC */
static const uint8_t LT_PATH[LT_N] = {
    1, 0,
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    7, 6, 5, 4, 3, 2,
    8, 9, 10, 11, 12, 13,
};
#define LT_RUN_MS ((LT_N + 3u) * LT_STEP_MS)              /* until the tail has gone past REC */
#define LT_SHOW_MS (LT_RUN_MS + 2u * LT_FLASH_MS * LT_FLASHES)

/* PLAY's green flashes from t (ms) on: 1 while lit */
static int lt_flash(uint32_t t)
{
    return t < 2u * LT_FLASH_MS * LT_FLASHES && (t / LT_FLASH_MS & 1u) == 0;
}

/* PLAY green during its flashes, its red dark; after them, as before (the green dark) */
static void lt_play(uint32_t t, uint8_t lv[LT_NLV])
{
    lv[LT_GREEN] = LT_DARK;
    if (t < 2u * LT_FLASH_MS * LT_FLASHES) {
        lv[LT_PLAY] = LT_DARK;
        if (lt_flash(t))
            lv[LT_GREEN] = LT_LIT;
    }
}

/* the show at t ms: each LED's level into lv[id]; 1 while it runs, 0 once it has ended (all in the glow) */
static int lt_show(uint32_t t, uint8_t lv[LT_NLV])
{
    uint32_t i, head = t / LT_STEP_MS;
    for (i = 0; i < LT_N; i++) {
        uint8_t l = LT_DARK;
        if (t >= LT_RUN_MS || head > i + 3u)
            l = LT_GLOW;                                  /* passed: the glow stays */
        else if (head == i)
            l = LT_LIT;
        else if (head == i + 1u)
            l = LT_MID;
        else if (head > i)
            l = LT_GLOW;
        lv[LT_PATH[i]] = l;
    }
    lv[LT_GREEN] = LT_DARK;
    if (t >= LT_RUN_MS)
        lt_play(t - LT_RUN_MS, lv);
    return t < LT_SHOW_MS;
}

/* done: PLAY flashes green again over the glow; 1 while it does */
static int lt_done(uint32_t t, uint8_t lv[LT_NLV])
{
    uint32_t i;
    for (i = 0; i < LT_N; i++)
        lv[i] = LT_GLOW;
    lt_play(t, lv);
    return t < 2u * LT_FLASH_MS * LT_FLASHES;
}
