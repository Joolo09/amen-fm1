/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN OS on the M-VAVE FM-1: the platform API over felucca-bsp (MPL-2.0, fw/firmware/bsp), from its hwtest example:
 * the boot guard, the key scan and USB on TIMER5, the audio ISR, the screen in bands, the LEDs, the MASTER pot, the
 * project in the flash (two copies, the header last), and the update path (the installer over USB-MIDI; OCT- and
 * OCT+ held 5 s, or the SysEx soft key: UBOOT), so AMEN OS can always be replaced. */
#include <stdint.h>
#define FELUCCA_CDC 1
#define FELUCCA_OTA 1
#ifndef AMEN_ID
#define AMEN_ID "FM-1_702"               /* the package identity the installer checks (tools/build_os.sh --product) */
#endif
#define FELUCCA_ID AMEN_ID
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_timer.h"
#include "fm1_input.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "fm1_audio.h"
#include "fm1_flash.h"
#include "libc.c"
#include "usb.c"
#include "ota.c"
#include "fe_tables.h"
#include "amen.h"

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _rt_start[], _rt_end[], _rt_load[];

/* ------------------------------------------------------------------------------------------- the panel --- */
/* matrix ids of the printed buttons and knobs (Felucca's panel.c, measured on hardware) */
static const uint8_t BTN_ID[NBTN] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1};
static const uint8_t ENC_ID[NENC] = {0, 1, 6, 2, 3, 4, 5};
static volatile uint16_t pot = 700;

static uint32_t plat_ms(void) { return fm1_ms; }
static void plat_input(plat_in_t *in)
{
    uint32_t b, raw = fm1_in.buttons;
    in->buttons = 0;
    for (b = 0; b < NBTN; b++)
        if ((raw >> BTN_ID[b]) & 1u)
            in->buttons |= 1u << b;
    in->keys = fm1_in.notes & ((1u << NKEYS) - 1u);
    for (b = 0; b < NENC; b++)
        in->enc[b] = (int16_t)fm1_enc_take(ENC_ID[b]);
    in->pot = pot;
}
static void plat_led(uint32_t id, int on) { fm1_led_key(id < NBTN ? BTN_ID[id] : id, on); }

/* ------------------------------------------------------------------------------------------- the screen --- */
static uint8_t lcd_arg[4];
static uint16_t lcd_tx[240 * 16];
static void lcd_cmd(uint8_t c, const uint8_t *d, uint32_t n)
{
    uint32_t i;
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
    if (!n)
        return;
    for (i = 0; i < n; i++)
        lcd_arg[i] = d[i];
    fm1_lcd_send_data(lcd_arg, n);
    fm1_lcd_wait();
    fm1_lcd_deselect();
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
    lcd_cmd(0x36, &madctl, 1);
    lcd_cmd(0x21, 0, 0);                        /* INVON: IPS */
    lcd_cmd(0x13, 0, 0);                        /* NORON */
    lcd_cmd(0x29, 0, 0);                        /* DISPON */
    fm1_lcd_baud(4);
}
static void plat_lcd_band(uint32_t y, uint32_t h, const uint16_t *px)
{
    uint32_t i, n = 240u * h;
    for (i = 0; i < n; i++)                     /* the panel takes the high byte first */
        lcd_tx[i] = (uint16_t)((px[i] >> 8) | (px[i] << 8));
    lcd_window(0, y, 240, h);
    fm1_lcd_send_data(lcd_tx, n * 2u);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}
static void lcd_fill(uint32_t y, uint32_t h, uint16_t c)   /* (the update's bar, UBOOT) */
{
    uint32_t i;
    for (i = 0; i < 240u * 16u; i++)
        lcd_tx[i] = (uint16_t)((c >> 8) | (c << 8));
    while (h) {
        uint32_t k = h > 16u ? 16u : h;
        lcd_window(0, y, 240, k);
        fm1_lcd_send_data(lcd_tx, 240u * k * 2u);
        fm1_lcd_wait();
        fm1_lcd_deselect();
        y += k;
        h -= k;
    }
}

/* -------------------------------------------------------------------------------------------- the audio --- */
#define HALF_FRAMES 128u
#define HALF_WORDS (HALF_FRAMES * 2u)
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
static volatile uint32_t audio_max_us;
void fm1_alnk0_irq(void)
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), i, t0 = fm1_ticks(), us;
        int32_t *o = &abuf[half * HALF_WORDS];
        app_audio(o, HALF_FRAMES);
        for (i = 0; i < HALF_WORDS; i++)
            o[i] *= 1 << 7;                     /* Q15 -> 24 bit, -6 dBFS ceiling (as Felucca) */
        fm1_audio_ack_half();
        us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
        if (us > audio_max_us)
            audio_max_us = us;
    }
}
extern void isr_alnk0(void);
static uint32_t plat_cpu(void)                  /* the worst half lately, % of its 2.9 ms; it decays */
{
    uint32_t us = audio_max_us;
    audio_max_us = us - us / 8u;
    return us * 100u / (HALF_FRAMES * 1000000u / 44100u);
}

/* ----------------------------------------------------------------------------------- input, USB, the ms --- */
void fm1_timer5_irq(void)                       /* 10 kHz: the key scan, ms, USB at 2 kHz */
{
    static uint32_t sub, last, acc;
    uint32_t t = fm1_ticks();
    fm1_timer5_ack();
    fm1_input_tick();
    acc += t - last;
    last = t;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        fm1_ms++;
    }
    if (++sub == 5u) {
        sub = 0;
        usb_poll();
    }
}
extern void isr_timer5(void);

static int plat_midi_get(uint8_t m[3])
{
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ], st = (pkt >> 8) & 0xFFu;
        mi_r++;
        if (st < 0x80u || st >= 0xF0u)
            continue;
        m[0] = (uint8_t)st;
        m[1] = (uint8_t)((pkt >> 16) & 0x7Fu);
        m[2] = (uint8_t)((pkt >> 24) & 0x7Fu);
        return 1;
    }
    midi_in_overflow = 0;
    return 0;
}

/* ---------------------------------------------------------------------------------------------- the store --- */
/* two copies of the project, two sectors each, in Felucca's project area (fm1_flash.h FL_DATA): a header at the copy's
 * start (magic, seq, length, CRC), the project from +256; the header is written last, so a cut leaves the other copy */
#define ST_A 0x00097000u
#define ST_B 0x00099000u
#define ST_MAGIC 0x414D5331u                    /* "AMS1" */
static uint8_t flash_ok;
typedef struct { uint32_t magic, seq, len, crc; } st_hdr_t;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc) return rc;
        off += k; d += k; n -= k;
    }
    return 0;
}
static uint32_t st_crc(uint32_t off, uint32_t n)
{
    static uint8_t b[256];
    uint32_t c = 0xFFFFFFFFu, k, i;
    while (n) {
        uint32_t m = n > 256u ? 256u : n;
        if (st_read(off, b, m)) return 0;
        for (i = 0; i < m; i++) {
            c ^= b[i];
            for (k = 0; k < 8u; k++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        }
        off += m; n -= m;
    }
    return ~c;
}
static int st_hdr_ok(uint32_t off, st_hdr_t *h)
{
    return !st_read(off, h, sizeof *h) && h->magic == ST_MAGIC && h->len <= 0x2000u - 256u &&
           st_crc(off + 256u, h->len) == h->crc;
}
static uint32_t st_newest(st_hdr_t *h, uint32_t *seq)
{
    st_hdr_t a, b;
    int va = st_hdr_ok(ST_A, &a), vb = st_hdr_ok(ST_B, &b);
    if (va && (!vb || (int32_t)(a.seq - b.seq) > 0)) { *h = a; *seq = a.seq; return ST_A; }
    if (vb) { *h = b; *seq = b.seq; return ST_B; }
    *seq = 0;
    return 0;
}
static int plat_store_load(void *p, uint32_t n)
{
    st_hdr_t h;
    uint32_t seq, at;
    if (!flash_ok) return -1;
    at = st_newest(&h, &seq);
    if (!at || h.len != n) return -1;
    return st_read(at + 256u, p, n);
}
static int plat_store_save(const void *p, uint32_t n)
{
    st_hdr_t h;
    uint32_t seq, cur, to, off, i, took, f;
    int rc = 0;
    if (!flash_ok || n > 0x2000u - 256u) return -1;
    cur = st_newest(&h, &seq);
    to = cur == ST_A ? ST_B : ST_A;
    for (off = 0; off < 0x2000u && !rc; off += 0x1000u) {
        f = irq_save();
        for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)   /* IRQs off: the DMA would loop stale audio */
            abuf[i] = 0;
        rc = FL_FAR(fl_erase4k_ram)(to + off, &took);
        irq_restore(f);
    }
    if (!rc) rc = fl_write(to + 256u, (const uint8_t *)p, n);
    h.magic = ST_MAGIC;
    h.seq = seq + 1u;
    h.len = n;
    h.crc = st_crc(to + 256u, n);
    if (!rc) rc = fl_write(to, (const uint8_t *)&h, sizeof h);
    return rc ? -1 : 0;
}

/* --------------------------------------------------------------------------------------- the update path --- */
static struct { uint32_t magic, pending, failed; } guard __attribute__((section(".noinit")));
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu)) return -8;
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n)) return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    lcd_fill(200, 24, step < 9u ? (step & 1u ? 0x07FF : 0xFFE0) : code == 1 ? 0x07E0 : 0xF800);   /* (blinks while it runs) */
}
static void ota_commit(const uint8_t *parm)
{
    guard.pending = 0;
    guard.failed = 0;
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
}
static void uboot(void)
{
    fm1_audio_stop();
    lcd_fill(0, 240, 0xF810);
    guard.pending = 0;
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_uboot();
}
static void fm1_fault(const fm1_crash_t *c)
{
    (void)c;
    fm1_audio_stop();
    lcd_fill(0, 240, 0xF800);
    fm1_delay_ms(1500);
    fm1_reboot();
}

#include "amen.c"

/* --------------------------------------------------------------------------------------------- the loop --- */
static void app_main(void)
{
    uint32_t last_pot = 0, last_frame = 0, both_t0 = 0;
    {
        uint32_t f = irq_save();
        flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the 1 MiB part every FM-1 has */
        irq_restore(f);
        if (flash_ok)
            ota_boot_cleanup();
    }
    lcd_init();
    fm1_input_init();
    fm1_adc_init();
    (void)fm1_adc_read(FM1_ADC_MASTER);
    {
        int32_t a = fm1_adc_read(FM1_ADC_MASTER);
        if (a >= 0) pot = (uint16_t)a;
    }
    app_init();                                  /* the splash; the project from the flash */
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
    usb_start();
    fm1_timer5_start(isr_timer5, 4);             /* above the audio: the scan nests into the render */
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_lcd_backlight(1);
    for (;;) {
        uint32_t now = fm1_ms;
        fm1_wdt_feed();
        usb_retry(now);
        if (usb.uboot_req)                       /* the SysEx soft key */
            uboot();
        ota_service();                           /* the installer's handshake, then its update */
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (flash_ok) {
                fm1_audio_stop();
                ota_session();                   /* returns only if nothing was committed */
                fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
            }
        }
        if (guard.pending && now > 3000u) {      /* it runs: the next boot is not a loop */
            guard.pending = 0;
            guard.failed = 0;
        }
        {   /* OCT- and OCT+ held 5 s: UBOOT (an update, a way back) */
            uint32_t both = (1u << BTN_ID[B_OCTDN]) | (1u << BTN_ID[B_OCTUP]);
            if ((fm1_in.buttons & both) != both) both_t0 = now;
            else if (now - both_t0 > 5000u) uboot();
        }
        while (ci_r != ci_w)                      /* (the USB serial: nothing to say yet) */
            ci_r++;
        if (now - last_pot >= 40u) {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            last_pot = now;
            if (a >= 0) pot = (uint16_t)((pot * 3u + (uint32_t)a) / 4u);
        }
        if (now - last_frame >= 20u) {           /* ~50 frames a second at most */
            last_frame = now;
            app_frame();
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_time_init();
    fm1_wdt_arm(0x0D);
    if (guard.magic != 0x414D4E47u) {
        guard.magic = 0x414D4E47u;
        guard.failed = 0;
        guard.pending = 0;
    }
    if (guard.pending)
        guard.failed++;
    guard.pending = 1;
    if (guard.failed >= 2u) {                    /* two boots that never got going: UBOOT, so it can be replaced */
        guard.failed = 0;
        guard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    app_main();
    for (;;)
        ;
}
