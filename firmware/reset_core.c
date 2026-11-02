/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Drew Robinson */
/* The reset itself, with no hardware in it: the firmware (switch_reset.c) and the host test (tests/reset_test.c)
 * give it the flash.
 *
 * It brings [RS_LO, RS_HI) to one picture: V15's factory USR data (usr_data.h) at USR_DATA_BASE, every other byte
 * erased (0xFF). Sector by sector it reads what is there, and only a sector that differs is erased, programmed
 * and read back. Nothing records that the reset was done: every start compares again, so a reset cut short by a
 * power cut (or a sector that a cut left half erased or half written) is finished on the next start, and one that
 * is done costs only the reading.
 *
 * Hooks:
 *   rs_read(off, p, n)    read flash into RAM, 0 = ok
 *   rs_erase(off)         erase the 4 KiB sector at off, 0 = ok
 *   rs_prog(off, p, n)    program up to 256 bytes inside one page from RAM, 0 = ok
 *   rs_step(done, total)  progress, once a sector (the watchdog, the screen)
 */
#include <stdint.h>

#define RS_LO 0x93000u                  /* the saved data of every firmware seen so far, from the app area's end ... */
#define RS_HI 0xFC000u                  /* ... to Felucca's store, which stays (Melodee's and LOOM's Bluetooth copy) */
#define RS_SECTOR 0x1000u
#define RS_PAGE 256u
#define RS_NSEC ((RS_HI - RS_LO) / RS_SECTOR)
#define RS_TRIES 3u                     /* erase and write a sector at most this often before giving up */

#if USR_DATA_BASE < RS_LO || USR_DATA_BASE + USR_DATA_LEN > RS_HI
#error "V15's USR data must lie inside the range the reset clears"
#endif

static int rs_read(uint32_t off, uint8_t *p, uint32_t n);
static int rs_erase(uint32_t off);
static int rs_prog(uint32_t off, const uint8_t *p, uint32_t n);
static void rs_step(uint32_t done, uint32_t total);

typedef struct {
    uint32_t erased;                    /* sectors erased this start (0: everything was already as it should be) */
    uint32_t written;                   /* pages of V15's data programmed */
    uint32_t bad_at;                    /* the sector that would not take its picture (with an error) */
    int err;                            /* 0, or -1 read, -2 erase, -3 program, -4 still wrong after RS_TRIES */
} rs_result_t;

static uint8_t rs_buf[RS_PAGE];         /* in RAM: the flash driver programs only from RAM */

/* what a byte of the range should hold */
static uint8_t rs_want(uint32_t off)
{
    return off - USR_DATA_BASE < USR_DATA_LEN ? usr_data[off - USR_DATA_BASE] : 0xFFu;
}

/* the picture of the page at off into rs_buf; 1 when it has something to program (not all erased) */
static int rs_page(uint32_t off)
{
    uint32_t i, any = 0;
    for (i = 0; i < RS_PAGE; i++) {
        rs_buf[i] = rs_want(off + i);
        any |= rs_buf[i] != 0xFFu;
    }
    return (int)any;
}

/* 1 when the sector at off holds its picture, 0 when not, < 0 when it can't be read */
static int rs_same(uint32_t off)
{
    static uint8_t have[RS_PAGE];
    uint32_t p, i;
    for (p = off; p < off + RS_SECTOR; p += RS_PAGE) {
        if (rs_read(p, have, RS_PAGE))
            return -1;
        rs_page(p);
        for (i = 0; i < RS_PAGE; i++)
            if (have[i] != rs_buf[i])
                return 0;
    }
    return 1;
}

static void rs_run(rs_result_t *r)
{
    uint32_t s, off, p, tries;
    int same;
    r->erased = r->written = r->bad_at = 0;
    r->err = 0;
    for (s = 0; s < RS_NSEC; s++) {
        off = RS_LO + s * RS_SECTOR;
        rs_step(s, RS_NSEC);
        for (tries = 0;; tries++) {
            same = rs_same(off);
            if (same == 1)
                break;
            if (same < 0 || tries == RS_TRIES) {
                r->err = same < 0 ? -1 : -4;
                r->bad_at = off;
                return;
            }
            if (rs_erase(off)) {
                r->err = -2;
                r->bad_at = off;
                return;
            }
            r->erased++;
            for (p = off; p < off + RS_SECTOR; p += RS_PAGE) {
                if (!rs_page(p))
                    continue;
                if (rs_prog(p, rs_buf, RS_PAGE)) {
                    r->err = -3;
                    r->bad_at = off;
                    return;
                }
                r->written++;
            }
        }
    }
    rs_step(RS_NSEC, RS_NSEC);
}
