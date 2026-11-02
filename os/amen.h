/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN OS: the firmware's own core, platform-free. One translation unit: a platform (fm1/main.c on the FM-1,
 * host/sim.c on a desktop) includes os/amen.c after providing the functions of the platform API below. */
#ifndef AMEN_H
#define AMEN_H
#include <stdint.h>
#ifdef AMEN_HOSTED
#include <string.h>
#else                                            /* the FM-1: felucca-bsp's libc.c has these three */
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
#endif
static uint32_t am_strlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static char *am_strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++) != 0) { } return r; }
static char *am_strcat(char *d, const char *s) { am_strcpy(d + am_strlen(d), s); return d; }
static int am_strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (uint8_t)*a - (uint8_t)*b; }

#define AMEN_NAME "AMEN OS"
#ifndef AMEN_VERSION
#define AMEN_VERSION "0.2"
#endif

/* ------------------------------------------------------------------------------------------- the platform --- */
/* logical buttons, as printed on the FM-1's panel */
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP, NBTN };
/* logical encoders */
enum { E_SELECT, E_ALGO, E_PRESET, E_K1, E_K2, E_K3, E_K4, NENC };
#define NKEYS 27u                 /* F3 .. G5 */

typedef struct {
    uint32_t buttons;             /* bit B_*: held */
    uint32_t keys;                /* bit k: key k (0 = F3) held */
    int16_t enc[NENC];            /* detents since the last read, + = clockwise */
    uint16_t pot;                 /* MASTER, 0..1023 */
} plat_in_t;

/* provided by the platform */
static uint32_t plat_ms(void);
static void plat_input(plat_in_t *in);
static void plat_led(uint32_t id, int on);           /* 0..13: B_*, 14 + k: key k */
static void plat_lcd_band(uint32_t y, uint32_t h, const uint16_t *px);   /* 240 x h, RGB565 */
static int plat_store_load(void *p, uint32_t n);     /* 0 = loaded */
static int plat_store_save(const void *p, uint32_t n);
static int plat_midi_get(uint8_t m[3]);              /* 1 = a short message */
static uint32_t plat_cpu(void);                      /* the audio's load, % of its time (the worst lately) */

/* the app (os/amen.c) */
static void app_init(void);
static void app_audio(int32_t *out, uint32_t n);     /* audio ISR: n stereo frames (a multiple of CTL), Q15 */
static void app_frame(void);                         /* main loop: input, the screen, the LEDs */
#endif
