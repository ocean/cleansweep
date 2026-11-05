/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Drew Robinson */
/* The reset (firmware/reset_core.c) on a pretend 1 MiB NOR flash: erase sets a sector to 0xFF, programming can only
 * clear bits. Checks that it ends with V15's data and nothing else in its range, never touches a byte outside it,
 * does nothing on a second start, finishes after a power cut at any erase or write (the cut leaving that sector or
 * page half done), and stops with the sector's address when a sector won't take its picture.
 *   tests/run_tests.sh */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usr_data.h"
#include "../firmware/reset_core.c"

#define FLASH 0x100000u
static uint8_t flash[FLASH], before[FLASH];
static uint32_t ops, cut_at, stuck_at, rng = 1;
static jmp_buf power;
static int failures;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void outside(uint32_t off, uint32_t n)
{
    if (off < RS_LO || off + n > RS_HI || n == 0) {
        printf("FAIL: the reset touched 0x%X..0x%X, outside its range\n", off, off + n);
        exit(1);
    }
}

static int rs_read(uint32_t off, uint8_t *p, uint32_t n)
{
    outside(off, n);
    memcpy(p, flash + off, n);
    return 0;
}

static int rs_erase(uint32_t off)
{
    uint32_t i;
    outside(off, RS_SECTOR);
    if (off & (RS_SECTOR - 1u)) {
        printf("FAIL: erase of 0x%X, not a sector's start\n", off);
        exit(1);
    }
    if (++ops == cut_at) {              /* the power goes part way: some bytes erased, the rest as they were */
        for (i = 0; i < RS_SECTOR; i++)
            if (rnd() & 1u)
                flash[off + i] = 0xFFu;
        longjmp(power, 1);
    }
    memset(flash + off, 0xFF, RS_SECTOR);
    if (off == stuck_at)                /* a worn sector: one bit that won't erase */
        flash[off + 0x123u] &= 0xFEu;
    return 0;
}

static int rs_prog(uint32_t off, const uint8_t *p, uint32_t n)
{
    uint32_t i, end = n;
    outside(off, n);
    if ((off & (RS_PAGE - 1u)) + n > RS_PAGE) {
        printf("FAIL: write of 0x%X+%u crosses a page\n", off, n);
        exit(1);
    }
    if (++ops == cut_at)                /* the power goes part way through the page */
        end = rnd() % n;
    for (i = 0; i < end; i++)
        flash[off + i] &= p[i];
    if (end != n)
        longjmp(power, 1);
    return 0;
}

static uint32_t steps;
static void rs_step(uint32_t done, uint32_t total)
{
    if (done > total || total != RS_NSEC)
        printf("FAIL: progress %u of %u\n", done, total), exit(1);
    steps++;
}

static void fill_random(void)
{
    uint32_t i;
    for (i = 0; i < FLASH; i++)
        flash[i] = (uint8_t)rnd();
    for (i = RS_LO + 0x7000u; i < RS_LO + 0x9000u; i++)      /* and some of it already erased, some already right */
        flash[i] = 0xFFu;
    for (i = 0; i < 0x2000u; i++)
        flash[USR_DATA_BASE + i] = usr_data[i];
    memcpy(before, flash, FLASH);
}

/* the range holds V15's data and erased flash; everything else is as it was */
static int pictured(void)
{
    uint32_t i;
    for (i = 0; i < FLASH; i++) {
        uint8_t want = i < RS_LO || i >= RS_HI ? before[i] : rs_want(i);
        if (flash[i] != want) {
            printf("  byte 0x%X is 0x%02X, not 0x%02X\n", i, flash[i], want);
            return 0;
        }
    }
    return 1;
}

static void test_reset(void)
{
    rs_result_t r;
    fill_random();
    cut_at = 0;
    steps = 0;
    rs_run(&r);
    CHECK(r.err == 0, "a reset from random flash ended with %d at 0x%X", r.err, r.bad_at);
    CHECK(r.erased == RS_NSEC - 4u, "erased %u sectors, not %u (four were already right)", r.erased, RS_NSEC - 4u);
    CHECK(steps == RS_NSEC + 1u, "%u progress calls", steps);
    CHECK(pictured(), "the flash isn't V15's data and erased flash after a reset");
    CHECK(memcmp(flash + USR_DATA_BASE, usr_data, USR_DATA_LEN) == 0, "V15's data isn't at 0x%X", USR_DATA_BASE);
    rs_run(&r);
    CHECK(r.err == 0 && r.erased == 0 && r.written == 0, "a second start erased %u and wrote %u", r.erased, r.written);
    CHECK(pictured(), "the flash changed on a second start");
}

/* a power cut at every erase and every write a reset makes, then the next start */
static void test_power_cuts(void)
{
    rs_result_t r;
    uint32_t total;
    volatile uint32_t k, worst = 0;   /* (across the longjmp of a power cut: gcc's -Wclobbered) */
    fill_random();
    ops = 0;
    cut_at = 0;
    rs_run(&r);
    total = ops;
    CHECK(total > RS_NSEC, "only %u flash operations", total);
    for (k = 1; k <= total; k++) {
        memcpy(flash, before, FLASH);
        ops = 0;
        cut_at = k;
        if (!setjmp(power)) {
            rs_run(&r);
            CHECK(0, "the power cut at operation %u never came", k);
        }
        ops = 0;
        cut_at = 0;
        rs_run(&r);
        if (r.err || !pictured()) {
            CHECK(0, "after a power cut at operation %u of %u the next start ended with %d", k, total, r.err);
            return;
        }
        if (r.erased > worst)
            worst = r.erased;
    }
    printf("  %u power cuts, each finished on the next start (which erased at most %u sectors)\n", total, worst);
}

/* power cuts on the restart too: three cuts in a row, then a start that finishes */
static void test_repeated_cuts(void)
{
    rs_result_t r;
    volatile uint32_t round, n;       /* (across the longjmp of a power cut: gcc's -Wclobbered) */
    for (round = 0; round < 200u; round++) {
        fill_random();
        for (n = 0; n < 3u; n++) {
            ops = 0;
            cut_at = 1u + rnd() % 300u;
            if (!setjmp(power))
                rs_run(&r);
        }
        ops = 0;
        cut_at = 0;
        rs_run(&r);
        if (r.err || !pictured()) {
            CHECK(0, "round %u: three power cuts, then the start ended with %d", round, r.err);
            return;
        }
    }
}

/* a sector that won't erase: the reset stops there and says where, after trying RS_TRIES times */
static void test_stuck_sector(void)
{
    rs_result_t r;
    fill_random();
    cut_at = 0;
    stuck_at = 0xA0000u;
    rs_run(&r);
    stuck_at = 0;
    CHECK(r.err == -4 && r.bad_at == 0xA0000u, "a stuck sector ended with %d at 0x%X", r.err, r.bad_at);
    CHECK(r.erased == (0xA0000u - RS_LO) / RS_SECTOR - 2u + RS_TRIES, "erased %u sectors", r.erased);
}

int main(void)
{
    test_reset();
    test_power_cuts();
    test_repeated_cuts();
    test_stuck_sector();
    printf(failures ? "reset_test: %d failed\n" : "reset_test: ok (%d failures)\n", failures);
    return failures != 0;
}
