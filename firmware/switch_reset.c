/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Drew Robinson
 * After felucca-bsp's examples/hwtest (Leo Kuroshita, Hügelton Instruments, MPL-2.0). */
/* SWITCH_RESET: clears what earlier firmwares left in the FM-1's flash, then waits for the next firmware.
 *
 * At every start, before USB is up: the power-on light sweep, then reset_core.c brings [0x93000, 0xFC000) to V15's
 * factory USR data at 0xEA000 and erased flash everywhere else, and says so on the screen. The flash driver itself
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
#define FELUCCA_ID "FM-1_379000"      /* the identity the installer sees, == the package's (tools/build.py) */
#endif
#define FELUCCA_USB_MANUFACTURER "M-VAVE"
#define FELUCCA_USB_PRODUCT "FM-1 SWITCH RESET"
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
#define C(x) ((uint16_t)((((x) >> 8) | ((x) << 8)) & 0xFFFFu))
static uint16_t row_px[240];

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

static void lcd_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint8_t a[4];
    a[0] = 0; a[1] = (uint8_t)x; a[2] = 0; a[3] = (uint8_t)(x + w - 1u);
    lcd_cmd(0x2A, a, 4);
    a[1] = (uint8_t)y; a[3] = (uint8_t)(y + h - 1u);
    lcd_cmd(0x2B, a, 4);
    lcd_cmd(0x2C, 0, 0);
}

static void lcd_row(uint32_t w)
{
    fm1_lcd_send_data(row_px, w * 2u);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void lcd_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t rgb)
{
    uint32_t i;
    for (i = 0; i < w; i++)
        row_px[i] = C(rgb);
    lcd_window(x, y, w, h);
    for (i = 0; i < h; i++)
        lcd_row(w);
}

static uint16_t grey(uint32_t level)            /* 0..15 -> RGB565, pre-swapped */
{
    uint32_t v = level * 17u;
    return C((uint16_t)((v >> 3) << 11 | (v >> 2) << 5 | (v >> 3)));
}

/* columns [from, from + w) of a text at x: its runs, row by row */
static void text_part(const sr_text_t *t, uint32_t x, uint32_t from, uint32_t w)
{
    uint32_t i = 0, y, col, n;
    lcd_window(x, t->y, w, t->h);
    for (y = 0; y < t->h; y++) {
        for (col = 0; col < t->w;) {
            uint8_t b = t->rle[i++];
            for (n = (b & 15u) + 1u; n--; col++)
                if (col - from < w)
                    row_px[col - from] = grey(b >> 4);
        }
        lcd_row(w);
    }
}

static void text(const sr_text_t *t)
{
    text_part(t, t->x, 0, t->w);
}

/* a screen: the title and up to three texts; the rest of it black */
static void screen(const sr_text_t *a, const sr_text_t *b, const sr_text_t *c)
{
    lcd_rect(0, 0, 240, 240, 0);
    text(&TX_TITLE);
    if (a)
        text(a);
    if (b)
        text(b);
    if (c)
        text(c);
}

static uint32_t bar_shown;
static void bar(uint32_t done, uint32_t total)  /* white over dark grey, drawn as it grows */
{
    uint32_t w = done * 208u / total;
    if (!done || w < bar_shown) {
        lcd_rect(16, SR_BAR_Y, 208, SR_BAR_H, 0x2104);
        bar_shown = 0;
    }
    if (w > bar_shown) {
        lcd_rect(16u + bar_shown, SR_BAR_Y, w - bar_shown, SR_BAR_H, 0xFFFF);
        bar_shown = w;
    }
}

/* a number in hex at x on the digits' line (the error's sector) */
static uint32_t hex_at(uint32_t x, uint32_t v, uint32_t digits)
{
    while (digits--) {
        text_part(&TX_DIGITS, x, ((v >> (4u * digits)) & 15u) * SR_DIGIT_W, SR_DIGIT_W);
        x += SR_DIGIT_W;
    }
    return x;
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
    bar(done, total);
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
        screen(&TX_INSTALLING, 0, 0);
        bar_shown = 0;
    }
    if (step < 9u)
        bar(step, 5);
    else if (code != 1)
        screen(&TX_WRONG, &TX_STOPPED, 0);
}
static void ota_commit(const uint8_t *parm)
{
    guard.pending = 0;                          /* an intentional reset, not a failed start */
    guard.failed = 0;
    usb_detach();
    fm1_delay_ms(30);
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
        puts_(FELUCCA_ID " SWITCH RESET: h help, p the reset's result, r restart, u UBOOT\r\n");
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
        flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the 1 MiB part every FM-1 has */
        irq_restore(f);
    }
    lcd_init();
    screen(&TX_CLEARING, &TX_LEAVE_ON, 0);
    fm1_input_init();
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_lcd_backlight(1);
    fm1_led_anim_start(0);                      /* hello: the light runs over the keys, the buttons swell */
    while (fm1_led_anim_on())
        fm1_wdt_feed();
    if (flash_ok)
        rs_run(&result);
    else
        result.err = -5;                        /* not the flash part an FM-1 has: touch nothing */
    if (result.err) {
        screen(&TX_WRONG, &TX_AGAIN, 0);
        hex_at(hex_at(88, (uint32_t)-result.err, 1) + SR_DIGIT_W, result.bad_at, 5);
    } else {
        screen(&TX_DONE, &TX_NEXT, &TX_BLUETOOTH);
        fm1_led_anim_start(1);                  /* and again, the keys left softly lit */
    }
    guard.pending = 0;                          /* it got through: the next start is not a loop */
    guard.failed = 0;
    usb_start();
    for (;;) {
        uint32_t now = ms;
        fm1_wdt_feed();
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
    if (guard.magic != 0x53525354u || guard.pending > 1u || guard.failed > 1u) {   /* they only ever hold 0 or 1 */
        guard.magic = 0x53525354u;
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
