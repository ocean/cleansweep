/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Drew Robinson
 * After felucca-bsp's examples/hwtest (Leo Kuroshita, Hügelton Instruments, MPL-2.0). */
/* CLEANSWEEP: clears what earlier firmwares left in the FM-1's flash, then waits for the next firmware.
 *
 * At every start, before USB is up: CLEAN and SWEEP zip onto the screen while a light runs round the keys and
 * buttons (look.c, lights.c), then reset_core.c brings [0x93000, 0xFC000) to V15's factory USR data at 0xEA000 and
 * erased flash everywhere else, a fizzing bar showing how far it is, and the screen says when it's done. The flash driver itself
 * refuses anything outside that range (FL_RANGE_OK below): the boot area, this firmware's own image, Felucca's store
 * at 0xFC000 (where Melodee and LOOM keep Bluetooth) and 0xFF000 can't be touched. No sound: the audio path is never
 * started and the codec is held off.
 *
 * Then USB comes up with the update entry (felucca-bsp ota.c, the fixed check: V15, V16, m-bootloader and Felucca's
 * loader are all taken), so the installer can put the next firmware on, and a serial console (h p r u).
 *
 * Two starts in a row that never get to the end of the reset drop into UBOOT, as hwtest does, so a broken build
 * can always be replaced. */
#include <stdint.h>
#define FELUCCA_CDC 1
#define FELUCCA_OTA 1
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_257000"      /* the identity the installer sees, == the package's (tools/build.py) */
#endif
/* V15's USB identity (kata xbkc): its ids and bcdDevice, the product "FM-1", no manufacturer, and the
 * flash's unique ID as the serial number, in hex, as V15 and download mode give it. */
#define FELUCCA_USB_VID 0x4C4A
#define FELUCCA_USB_PID 0xC755
#define FELUCCA_USB_BCD 0x0100
#define FELUCCA_USB_NO_MANUFACTURER 1
#define FELUCCA_USB_PRODUCT "FM-1"
#define FELUCCA_USB_SERIAL cs_serial
static char cs_serial[17];
#define FL_UID
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_timer.h"
#include "fm1_input.h"
#include "fm1_audio.h"
#include "fm1_lcd_hw.h"
/* the only flash this firmware may erase or program: the reset's range, which holds ota.c's staging area too */
#define FL_RANGE_OK(off, n) FL_IN(off, n, 0x93000u, 0xFC000u)
#include "fm1_flash.h"
#include "libc.c"
#include "usb.c"
#include "ota.c"
#include "usr_data.h"                  /* tools/usr_data.py: V15's factory USR data */
#include "screens.h"                   /* tools/gen_screens.py: the words */
#include "../firmware/reset_core.c"
#include "../firmware/look.c"
#include "../firmware/lights.c"

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _rt_start[], _rt_end[], _rt_load[];   /* code that runs from RAM: the flash driver (fm1_flash.h) */

static volatile uint32_t ms;
static volatile struct { uint32_t magic, pending, failed; } guard __attribute__((section(".noinit")));   /* (hwtest) */
static rs_result_t result;
void fm1_alnk0_irq(void) {}                     /* isr_alnk0 is linked but no audio runs */

void fm1_timer5_irq(void)                       /* 10 kHz: key scan and lights, ms, USB at 2 kHz */
{
    static uint32_t sub, last, acc;
    uint32_t t = fm1_ticks();
    fm1_timer5_ack();
    fm1_input_tick();
    acc += t - last;
    last = t;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        ms++;
    }
    if (++sub == 5u) {
        sub = 0;
        usb_poll();                             /* nothing until usb_start (usb.up) */
    }
}
extern void isr_timer5(void);

static void wait_ms(uint32_t n)
{
    uint32_t t0 = ms;
    while (ms - t0 < n)
        fm1_wdt_feed();
}

/* ---- screen: ST7789 240x240, RGB565 sent high byte first ---- */
static void lcd_cmd(uint8_t c, const uint8_t *d, uint32_t n)
{
    static uint8_t buf[4];
    uint32_t i;
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
    if (!n)
        return;
    for (i = 0; i < n; i++)
        buf[i] = d[i];
    fm1_lcd_send_data(buf, n);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void lcd_init(void)
{
    static const uint8_t colmod = 0x55, madctl = 0x00;
    fm1_lcd_hw_init();
    fm1_delay_ms(50);
    lcd_cmd(0x01, 0, 0);                        /* SWRESET */
    fm1_delay_ms(150);
    lcd_cmd(0x11, 0, 0);                        /* SLPOUT */
    fm1_delay_ms(120);
    lcd_cmd(0x3A, &colmod, 1);                  /* RGB565 */
    lcd_cmd(0x36, &madctl, 1);                  /* top-left origin, RGB */
    lcd_cmd(0x21, 0, 0);                        /* INVON: this panel is IPS */
    lcd_cmd(0x13, 0, 0);                        /* NORON */
    lcd_cmd(0x29, 0, 0);                        /* DISPON */
    fm1_lcd_baud(4);
}

/* look.c's hooks */
static void lk_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint8_t a[4];
    a[0] = 0; a[1] = (uint8_t)x; a[2] = 0; a[3] = (uint8_t)(x + w - 1u);
    lcd_cmd(0x2A, a, 4);
    a[1] = (uint8_t)y; a[3] = (uint8_t)(y + h - 1u);
    lcd_cmd(0x2B, a, 4);
    lcd_cmd(0x2C, 0, 0);
}

static void lk_row(uint16_t *px, uint32_t w)
{
    uint32_t i;
    for (i = 0; i < w; i++)                     /* high byte first */
        px[i] = (uint16_t)(px[i] >> 8 | px[i] << 8);
    fm1_lcd_send_data(px, w * 2u);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static uint32_t lk_ms(void) { return ms; }

/* The panel takes a moment to show the violet after the update loader's screen: with no wait the show was half over
 * before it was visibly ours, and 500 ms left the violet still coming on (Drew, on hardware). */
#define START_WAIT_MS 580u

/* ---- the lights (lights.c): the show at power-on, PLAY's flashes when it's done ---- */
static uint32_t lt_t0;
static int (*lt_now)(uint32_t, uint8_t *);      /* the show running, or 0 */
#define LT_GREEN_COL 8u                         /* PLAY's green LED: column 8, PA9 (bit 1), no key there */
#define LT_GREEN_BIT 1u

static void lights_start(int (*show)(uint32_t, uint8_t *))
{
    lt_t0 = ms;
    lt_now = show;
}

static void lights(void)                        /* the levels on the LEDs, a column's bits at a time */
{
    uint8_t lv[LT_NLV], lit[FM1_NCOL] = {0}, mid[FM1_NCOL] = {0}, dim[FM1_NCOL] = {0};
    uint32_t c, r;
    int8_t id;
    if (!lt_now)
        return;
    if (!lt_now(ms - lt_t0, lv))
        lt_now = 0;                             /* (its last picture stays) */
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if ((id = FM1_KEYMAP[r][c]) >= 0) {
                lit[c] |= (uint8_t)((lv[id] == LT_LIT) << r);
                mid[c] |= (uint8_t)((lv[id] == LT_MID) << r);
                dim[c] |= (uint8_t)((lv[id] != LT_DARK) << r);
            }
    lit[LT_GREEN_COL] |= (uint8_t)((lv[LT_GREEN] == LT_LIT) << LT_GREEN_BIT);   /* only ever lit or dark */
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = lit[c];
        fm1_led_mid[c] = mid[c];
        fm1_led_dim[c] = dim[c];
    }
}

static void lk_idle(void)
{
    fm1_wdt_feed();
    lights();
}

/* ---- the reset's hooks (reset_core.c) ---- */
static uint8_t flash_ok;                        /* the expected flash part */
static int rs_read(uint32_t off, uint8_t *p, uint32_t n)
{
    uint32_t f = irq_save();
    int rc = FL_FAR(fl_read_ram)(off, p, n);
    irq_restore(f);
    return rc;
}
static int rs_erase(uint32_t off)
{
    uint32_t took;
    fm1_wdt_feed();
    return fl_erase4k(off, &took);
}
static int rs_prog(uint32_t off, const uint8_t *p, uint32_t n) { return fl_write(off, p, n); }
static void rs_step(uint32_t done, uint32_t total)
{
    fm1_wdt_feed();
    lk_bar(done, total, done == total ? 400u : 12u);   /* a little time for the sparks, and to let them settle */
}

/* ---- the update (ota.c): the installer's M-UPGRADE session over USB-MIDI ---- */
static uint32_t ota_now_ms(void) { return ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n)
{
    uint8_t *d = p;
    while (n) {                                 /* 256-byte windows with interrupts off */
        uint32_t k = n > 256u ? 256u : n;
        int rc = rs_read(off, d, k);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static void ota_show(uint32_t step, int32_t code)
{
    if (step == 1u) {
        lk_screen(&TX_INSTALLING, &TX_DARK, 0);
        lk_bar_start();
    }
    if (step < 9u)
        lk_bar(step, 5, 0);
    else if (code != 1)
        lk_screen(&TX_WRONG, &TX_STOPPED, 0);
}
/* The reset ends the screen: PA2, the backlight, comes back undriven and M-VAVE's loader has no display code, so
 * the FM-1 stays dark until the next firmware starts (kata h3k6). Staging takes about a second, too short to read
 * that, so the words stay up a little longer. Installers look for the loader for 30 s or more. */
#define OTA_HOLD_MS 3000u

static void ota_commit(const uint8_t *parm)
{
    guard.pending = 0;                          /* an intentional reset, not a failed start */
    guard.failed = 0;
    usb_detach();                               /* off USB first: to an installer nothing but the wait changes */
    lk_bar(5, 5, OTA_HOLD_MS);
    fm1_enter_update(parm);
}

static void fm1_fault(const fm1_crash_t *c)     /* a fault: reset; two in a row end in UBOOT (fm1_cstart) */
{
    (void)c;
    fm1_reboot();
}

/* ---- serial console ---- */
static void put(char ch)
{
    if (!cdc.dtr || co_w - co_r >= CO_N)
        return;
    cdc_out[co_w % CO_N] = (uint8_t)ch;
    RING_PUBLISH();
    co_w++;
}
static void puts_(const char *s) { while (*s) put(*s++); }
static void putd(const char *k, uint32_t v)
{
    char b[10];
    uint32_t n = 0;
    puts_(k);
    do { b[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n)
        put(b[--n]);
    put(' ');
}
static void puthex(const char *k, uint32_t v)
{
    uint32_t i;
    puts_(k);
    for (i = 8; i--;)
        put("0123456789ABCDEF"[(v >> (4u * i)) & 15u]);
    put(' ');
}

static void command(char ch)
{
    switch (ch) {
    case 'h':
        puts_(FELUCCA_ID " CLEANSWEEP: h help, p the reset's result, r restart, u UBOOT\r\n");
        break;
    case 'p':
        puthex("range=", RS_LO); puthex("..", RS_HI);
        putd("flash_ok=", flash_ok); putd("err=", (uint32_t)-result.err); puthex("bad_at=", result.bad_at);
        putd("erased=", result.erased); putd("pages_written=", result.written);
        puts_("\r\n");
        break;
    case 'r':
        puts_("restart\r\n");
        fm1_delay_ms(30);
        usb_detach();
        fm1_reboot();
        break;
    case 'u':
        puts_("UBOOT\r\n");
        fm1_delay_ms(30);
        usb_detach();
        fm1_enter_uboot();
        break;
    }
}

static void app_main(void)
{
    fm1__pc_out(6, 0);                          /* the codec held off (fm1_audio.h): no sound at all */
    {
        uint32_t f = irq_save();
        uint32_t id = FL_FAR(fl_jedec_ram)();
        flash_ok = id == 0x856014u || id == 0xB36014u;  /* the FM-1's 1 MiB parts: P25Q80, UC25HQ80 since autumn 2026 */
        irq_restore(f);
    }
    lcd_init();
    lk_clear();
    lk_redraw(0, 0, LK_W, LK_H);                /* the violet, before the backlight */
    fm1_input_init();
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_lcd_backlight(1);
    {                                           /* the panel and the LEDs visibly up before anything moves */
        uint32_t t0 = ms;
        while (ms - t0 < START_WAIT_MS)
            lk_idle();
    }
    lights_start(lt_show);                      /* hello: the light runs round, PLAY flashes */
    lk_intro();                                 /* CLEAN and SWEEP zip in */
    lk_screen(&TX_CLEARING, &TX_LEAVE_ON, 0);
    lk_bar_start();
    while (lt_now)
        lk_idle();
    if (flash_ok)
        rs_run(&result);
    else
        result.err = -5;                        /* not the flash part an FM-1 has: touch nothing */
    if (result.err) {
        lk_screen(&TX_WRONG, &TX_AGAIN, 0);
        lk_hex(lk_hex(LK_W / 2 - 4 * (int32_t)CS_DIGIT_W, (uint32_t)-result.err, 1) + (int32_t)CS_DIGIT_W,
               result.bad_at, 5);              /* the code and the sector: 7 digits, about the middle */
    } else {
        lk_screen(&TX_DONE, &TX_NEXT, &TX_BLUETOOTH);
        lights_start(lt_done);                  /* PLAY flashes: good to go */
    }
    guard.pending = 0;                          /* it got through: the next start is not a loop */
    guard.failed = 0;
    {                                           /* the serial number: the flash's first eight unique-ID bytes */
        static const char hex[] = "0123456789ABCDEF";
        uint8_t uid[16];
        uint32_t i, f = irq_save();             /* the timer's interrupt runs from the flash, off while it's read */
        int rc = FL_FAR(fl_uid_ram)(uid);
        irq_restore(f);
        if (!rc)
            for (i = 0; i < 8u; i++) {
                cs_serial[2 * i] = hex[uid[i] >> 4];
                cs_serial[2 * i + 1] = hex[uid[i] & 15u];
            }
    }
    usb_start();
    for (;;) {
        uint32_t now = ms;
        lk_idle();
        usb_retry(now);
        if (usb.uboot_req)                      /* the SysEx soft key */
            command('u');
        ota_service();                          /* the installer's handshake */
        if (usb.ota_req) {                      /* and its update: commits and resets, or comes back */
            usb.ota_req = 0;
            if (flash_ok)                       /* even after a failed reset: the next firmware may do */
                ota_session();
        }
        while (ci_r != ci_w) {
            char ch = (char)cdc_in[ci_r % CI_N];
            ci_r++;
            command(ch);
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_time_init();
    fm1_wdt_arm(0x0D);
    if (guard.magic != 0x434C5357u || guard.pending > 1u || guard.failed > 1u) {   /* they only ever hold 0 or 1 */
        guard.magic = 0x434C5357u;
        guard.failed = 0;
        guard.pending = 0;
    }
    if (guard.pending)
        guard.failed++;
    guard.pending = 1;
    if (guard.failed >= 2u) {
        guard.failed = 0;
        guard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)    /* the flash driver, run from RAM */
        *d = *s;
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    app_main();
    for (;;)
        ;
}
