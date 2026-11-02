/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN OS on a desktop: the platform API over a frame buffer, a scripted panel, a project file and a WAV, then a
 * session that plays the firmware the way a hand would: screenshots of every screen, the audio, and checks.
 *   cc -O2 -Ibuild -Isdk/src -Iengines -Ios os/host/sim.c -o build/amen_sim && build/amen_sim build/sim
 * Writes OUT/<name>.ppm (240 x 240 + an LED row), OUT/session.wav; exit 1 on a failed check. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define AMEN_HOSTED 1
#include "fe_tables.h"
#include "amen.h"

static uint16_t fb[240 * 240];
static uint32_t sim_ms, bands_sent;
static plat_in_t sim_in;
static uint8_t led[41];
static uint8_t store[65536];
static uint32_t store_n;
static uint8_t midiq[64][3];
static uint32_t mq_r, mq_w;

static uint32_t plat_ms(void) { return sim_ms; }
static void plat_input(plat_in_t *in)
{
    *in = sim_in;
    memset(sim_in.enc, 0, sizeof sim_in.enc);
}
static void plat_led(uint32_t id, int on) { if (id < 41u) led[id] = (uint8_t)(on != 0); }
static void plat_lcd_band(uint32_t y, uint32_t h, const uint16_t *px)
{
    memcpy(&fb[y * 240u], px, h * 240u * 2u);
    bands_sent++;
}
static int plat_store_load(void *p, uint32_t n)
{
    if (store_n != n) return -1;
    memcpy(p, store, n);
    return 0;
}
static int plat_store_save(const void *p, uint32_t n)
{
    if (n > sizeof store) return -1;
    memcpy(store, p, n);
    store_n = n;
    return 0;
}
static uint32_t plat_cpu(void) { return 0; }
static int plat_midi_get(uint8_t m[3])
{
    if (mq_r == mq_w) return 0;
    memcpy(m, midiq[mq_r++ % 64u], 3);
    return 1;
}

#include "amen.c"

/* ------------------------------------------------------------------------------------------ the session --- */
static const char *out_dir;
static FILE *wav;
static uint32_t wav_frames;
static int32_t peak;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } else { printf("ok    " __VA_ARGS__); printf("\n"); } } while (0)

static void run(uint32_t ms)                       /* audio in halves of 128 frames, a frame every 16 ms */
{
    static uint32_t frac, next_frame;
    int32_t buf[256];
    int16_t pcm[256];
    uint32_t i, end = sim_ms + ms;
    while (sim_ms < end) {
        app_audio(buf, 128);
        for (i = 0; i < 256u; i++) {
            int32_t v = buf[i], a = v < 0 ? -v : v;
            if (a > peak) peak = a;
            pcm[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        if (wav) { fwrite(pcm, 2, 256, wav); wav_frames += 128u; }
        frac += 128000u;
        sim_ms += frac / 44100u;
        frac %= 44100u;
        if (sim_ms >= next_frame) {
            app_frame();
            next_frame = sim_ms + 16u;
        }
    }
}
static void press(uint32_t b) { sim_in.buttons |= 1u << b; run(40); }
static void release(uint32_t b) { sim_in.buttons &= ~(1u << b); run(40); }
static void tap(uint32_t b) { press(b); release(b); }
static void turn(uint32_t e, int32_t d) { sim_in.enc[e] = (int16_t)d; run(40); }
static void key(uint32_t k, int down) { if (down) sim_in.keys |= 1u << k; else sim_in.keys &= ~(1u << k); run(30); }
static void white(uint32_t i) { key(WHITE[i], 1); key(WHITE[i], 0); }

static void shot(const char *name)
{
    char path[256];
    FILE *f;
    uint32_t i, x, y;
    run(20);
    snprintf(path, sizeof path, "%s/%s.ppm", out_dir, name);
    f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fprintf(f, "P6\n240 256\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        uint8_t px[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63u) << 2), (uint8_t)((c & 31u) << 3)};
        fwrite(px, 1, 3, f);
    }
    for (y = 0; y < 16u; y++)                       /* the LEDs: 14 buttons, then the 27 keys */
        for (x = 0; x < 240u; x++) {
            uint32_t id = x < 70u ? x / 5u : 14u + (x - 70u) * 27u / 170u, edge = x < 70u ? x % 5u == 4u : 0u;
            uint8_t on = y > 3u && y < 12u && !edge && id < 41u && led[id], px[3];
            px[0] = on ? (id < 14u ? 255 : 204) : 30;
            px[1] = on ? (id < 14u ? 46 : 255) : 30;
            px[2] = on ? (id < 14u ? 136 : 26) : 30;
            fwrite(px, 1, 3, f);
        }
    fclose(f);
}

static void wav_open(void)
{
    char path[256];
    snprintf(path, sizeof path, "%s/session.wav", out_dir);
    wav = fopen(path, "wb");
    fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\1\0\2\0\x44\xac\0\0\x10\xb1\2\0\4\0\x10\0data\0\0\0\0", 1, 44, wav);
}
static void wav_close(void)
{
    uint32_t bytes = wav_frames * 4u, riff = 36u + bytes;
    fseek(wav, 4, SEEK_SET); fwrite(&riff, 4, 1, wav);
    fseek(wav, 40, SEEK_SET); fwrite(&bytes, 4, 1, wav);
    fclose(wav);
}

int main(int argc, char **argv)
{
    uint32_t i, steps_before, n_on;
    out_dir = argc > 1 ? argv[1] : "build/sim";
    sim_in.pot = 900;
    wav_open();
    app_init();
    shot("00_splash");
    run(400);
    CHECK(prj.bpm == 170u && prj.scene == 0u, "power-on: the demo project, 170 BPM, scene A");

    tap(B_PLAY);
    run(2000);
    CHECK(sq.playing && part_sounding(T_BRKA), "PLAY: the break plays");
    shot("01_main");

    tap(B_SEQ);
    shot("02_step");
    white(5); white(5);                               /* step 6: selected, then off */
    CHECK(!prj.pat[0][T_BRKA][5].on, "STEP: a white key twice selects then clears its step");
    white(5);
    turn(E_K1, 7);                                    /* its slice */
    turn(E_K2, 2);                                    /* a roll x3 */
    turn(E_K4, 1);                                    /* REV */
    CHECK(prj.pat[0][T_BRKA][5].on && prj.pat[0][T_BRKA][5].roll == 3u && prj.pat[0][T_BRKA][5].fx == FX_REV,
          "STEP: K1 slice, K2 roll x3, K4 REV on the selected step");
    shot("03_step_edit");

    tap(B_EDIT);
    shot("04_sound");
    tap(B_EDIT);
    shot("05_sound_p2");

    tap(B_HOME);
    steps_before = 0;
    for (i = 0; i < 16u; i++) steps_before += prj.pat[0][T_BRKA][i].val * (i + 1u) + prj.pat[0][T_BRKA][i].roll;
    tap(B_ENV);
    n_on = 0;
    for (i = 0; i < 16u; i++) n_on += prj.pat[0][T_BRKA][i].val * (i + 1u) + prj.pat[0][T_BRKA][i].roll;
    CHECK(n_on != steps_before, "ENV: MANGLE makes a new edit");
    run(1500);
    shot("06_main_mangled");

    turn(E_SELECT, 1); turn(E_SELECT, 1);             /* BASS */
    CHECK(ui.part == T_BASS, "SELECT: the tracks round to BASS");
    tap(B_SEQ);
    shot("07_step_bass");
    tap(B_EDIT);
    shot("08_sound_bass");
    turn(E_PRESET, 3);
    shot("09_preset");
    tap(B_FX);
    turn(E_K4, -12);
    shot("10_fx");
    turn(E_K4, 12);
    tap(B_GLO);
    shot("11_set");

    /* performance: REPEAT, TAPE STOP, mutes, scenes */
    turn(E_SELECT, 1);                                /* BRK A */
    tap(B_HOME);
    press(B_ARP);
    run(600);
    CHECK(sq.repeat == 3u, "ARP held: REPEAT 1/16");
    key(WHITE[3], 1); key(WHITE[3], 0);
    CHECK(sq.repeat == 4u, "ARP held + the 4th white key: 1/32");
    shot("12_repeat");
    release(B_ARP);
    CHECK(sq.repeat == 0u, "ARP let go: REPEAT off");
    press(B_LFO);
    run(1400);
    CHECK(amen_speed < 200u, "LFO held: TAPE STOP slows to a stop (speed %u)", amen_speed);
    release(B_LFO);
    run(500);
    CHECK(amen_speed == 4096u, "LFO let go: back to speed");
    tap(B_SCL);
    CHECK(prj.snd[T_BRKA].mute == 1u, "SCL: the track muted");
    shot("13_mute");
    tap(B_SCL);
    while (sq.step != 2u) run(10);                    /* (early in the bar: the switch waits for its end) */
    turn(E_ALGO, 1);
    CHECK(sq.queued == 1u && prj.scene == 0u, "ALGORITHM while playing: scene B queued");
    run(6000);
    CHECK(prj.scene == 1u, "the queued scene starts at the bar's end");
    shot("14_scene_b");

    /* live keys and REC */
    tap(B_REC);
    for (i = 0; i < 6u; i++) { key(WHITE[i * 2u], 1); run(90); key(WHITE[i * 2u], 0); }
    tap(B_REC);
    CHECK(!sq.rec, "REC off");

    /* MIDI in */
    midiq[mq_w % 64u][0] = 0x92; midiq[mq_w % 64u][1] = 40; midiq[mq_w % 64u][2] = 100; mq_w++;
    run(100);
    CHECK(trk[T_BASS].v[0].active && trk[T_BASS].v[0].note == 40u, "MIDI channel 3 plays the bass");

    /* save, then a power cycle */
    tap(B_SAVE);
    CHECK(store_n == sizeof prj, "SAVE: the project in the flash (%u bytes)", (unsigned)sizeof prj);
    {
        uint8_t keep = prj.bpm;
        prj.bpm = 99;
        app_init();
        CHECK(prj.bpm == keep && prj.scene == 1u, "power-on: the saved project back");
    }
    tap(B_PLAY);
    run(1000);
    tap(B_PLAY);
    run(1500);
    CHECK(!sq.playing, "PLAY again: stopped");
    CHECK(peak <= 32767 && peak > 8000, "the audio: loud, never over full scale (peak %d)", peak);
    CHECK(bands_sent > 0u, "the screen: %u bands sent", bands_sent);
    wav_close();
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
