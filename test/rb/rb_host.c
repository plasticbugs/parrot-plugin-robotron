// rb_host: the board (src/rb.c) on the Mac, for the tests.
//
//   rb_host ZIP [-frames N] [-script FILE] [-dump FILE] [-trace FILE]
//           [-tracefrom N] [-strace FILE] [-ppm DIR -every N] [-cmos FILE]
//           [-wav FILE] [-sdump FILE]
//
// ZIP is MAME's robotron.zip. Frame n ends at n * 16,640 cycles, a VBLANK's
// start, where MAME's frame_done runs; there frame n's script lines
// ("frame :PORT hex", the port's whole value, MAME's active high) are
// applied and the board is dumped. -dump writes a record a frame, as
// test/rb/rbdump.lua does in MAME: 'RBFR', the frame (u32), the RAM
// ($0000-$BFFF), the palette (16), the CMOS (1K).
// -trace writes the main CPU's registers before each instruction (MAME's
// tracelog: "@ PC A B X Y U S DP CC cycles"; -tracefrom N: from the end of
// frame N on), -strace the sound CPU's ("@ PC A B X S CC cycles", from the
// end of frame -tracefrom too).
// -ppm writes every Nth frame as the beam drew it (292 x 240). -cmos
// starts from that CMOS (1K) instead of all zero. -wav writes the DAC as
// MAME hears it (its level held between writes, -1 to 127/128 at 0.25 of
// full scale), at 48 kHz, for test/rb/soundcmp.sh. -sdump writes the
// sound board a frame: the 6808's registers, its cycles, its RAM and the
// DAC's writes with their times (test/rb/loopcheck.sh).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rb.h"
#include "rbroms.h"
#include "swzip.h"

static FILE *trace, *strace, *sdump;

static void on_trace(const m6809_t *c) {
    fprintf(trace, "@ %04X %02X %02X %04X %04X %04X %04X %02X %02X %llu\n", c->pc, c->a, c->b, c->x, c->y, c->u, c->s,
            c->dp, c->cc, (unsigned long long)m6809_now(c));
}
static void on_strace(void) {
    fprintf(strace, "@ %04X %02X %02X %04X %04X %02X %llu\n", rb.snd.pc, rb.snd.a, rb.snd.b, rb.snd.x, rb.snd.s, rb.snd.cc,
            (unsigned long long)rb.snd_now);
}

static uint8_t *slurp(const char *path, uint32_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)sz);
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f);
    *n = (uint32_t)sz;
    return b;
}

// the ROMs from MAME's zip, each by its size and CRC-32
int rb_host_load(const char *zipname) {
    uint32_t n;
    uint8_t *zip = slurp(zipname, &n);
    if (!zip) { fprintf(stderr, "rb_host: can't read %s\n", zipname); return -1; }
    uint32_t found = 0;
    swzip_entry_t e;
    static uint8_t buf[0x10000];
    for (int i = 0; swzip_entry(zip, n, i, &e); i++) {
        int r = rbroms_wanted(e.usize, e.crc, found);
        if (r < 0 || e.usize > sizeof buf || !swzip_read(&e, buf)) continue;
        rbroms_put(r, buf);
        found |= 1u << r;
    }
    free(zip);
    if (found != RBROMS_ALL) { fprintf(stderr, "rb_host: %s lacks some of the ROMs (%x)\n", zipname, found); return -1; }
    return 0;
}

#ifndef RB_HOST_NO_MAIN
static uint32_t pal_rgb(uint8_t v) {
    // MAME's palette_init: resistor weights 1200/560/330 (R, G), 560/330 (B)
    static const double rg[3] = {1.0 / 1200, 1.0 / 560, 1.0 / 330}, bw[2] = {1.0 / 560, 1.0 / 330};
    double rs = rg[0] + rg[1] + rg[2], bs = bw[0] + bw[1];
    double r = ((v & 1 ? rg[0] : 0) + (v & 2 ? rg[1] : 0) + (v & 4 ? rg[2] : 0)) / rs;
    double g = ((v & 8 ? rg[0] : 0) + (v & 16 ? rg[1] : 0) + (v & 32 ? rg[2] : 0)) / rs;
    double b = ((v & 64 ? bw[0] : 0) + (v & 128 ? bw[1] : 0)) / bs;
    return (uint32_t)(r * 255 + 0.5) << 16 | (uint32_t)(g * 255 + 0.5) << 8 | (uint32_t)(b * 255 + 0.5);
}

static void write_ppm(const char *dir, uint32_t n, const rb_frame_t *f) {
    char path[512];
    snprintf(path, sizeof path, "%s/f%06u.ppm", dir, n);
    FILE *o = fopen(path, "wb");
    if (!o) return;
    fprintf(o, "P6\n%d %d\n255\n", RB_VIS_W, RB_VIS_H);
    for (int y = 0; y < RB_VIS_H; y++)
        for (int x = 0; x < RB_VIS_W; x++) {
            uint8_t b = f->pix[y][x / 2], p = x & 1 ? b & 15 : b >> 4;
            uint32_t c = pal_rgb(f->pal[y][p]);
            uint8_t px[3] = {(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c};
            fwrite(px, 1, 3, o);
        }
    fclose(o);
}

static void wav_header(FILE *f, uint32_t n) {
    uint32_t v, rate = 48000;
    uint16_t h;
    fwrite("RIFF", 1, 4, f); v = 36 + n * 2; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    h = 1; fwrite(&h, 2, 1, f); fwrite(&h, 2, 1, f);
    fwrite(&rate, 4, 1, f); v = rate * 2; fwrite(&v, 4, 1, f);
    h = 2; fwrite(&h, 2, 1, f); h = 16; fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f); v = n * 2; fwrite(&v, 4, 1, f);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: rb_host ZIP [-frames N] [-script F] [-dump F] [-trace F] [-strace F] [-ppm DIR -every N] [-cmos F] [-wav F] [-sdump F]\n"); return 2; }
    const char *script = NULL, *dumpname = NULL, *ppm = NULL, *cmosname = NULL, *wavname = NULL;
    int frames = 600, every = 0, trace_from = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-frames")) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-script")) script = argv[++i];
        else if (!strcmp(argv[i], "-dump")) dumpname = argv[++i];
        else if (!strcmp(argv[i], "-trace")) trace = fopen(argv[++i], "w");
        else if (!strcmp(argv[i], "-tracefrom")) trace_from = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-strace")) strace = fopen(argv[++i], "w");
        else if (!strcmp(argv[i], "-ppm")) ppm = argv[++i];
        else if (!strcmp(argv[i], "-every")) every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-cmos")) cmosname = argv[++i];
        else if (!strcmp(argv[i], "-wav")) wavname = argv[++i];
        else if (!strcmp(argv[i], "-sdump")) sdump = fopen(argv[++i], "w");
        else { fprintf(stderr, "rb_host: what is %s?\n", argv[i]); return 2; }
    }
    if (rb_host_load(argv[1])) return 1;
    static struct { int frame, port, v; } ev[65536];
    int nev = 0;
    if (script) {
        FILE *f = fopen(script, "r");
        if (!f) { fprintf(stderr, "rb_host: can't read %s\n", script); return 1; }
        char line[128], port[16];
        while (fgets(line, sizeof line, f) && nev < 65536)
            if (sscanf(line, "%d %15s %x", &ev[nev].frame, port, &ev[nev].v) == 3)
                ev[nev].port = strstr(port, "IN0") ? 0 : strstr(port, "IN1") ? 1 : 2, nev++;
        fclose(f);
    }
    if (cmosname) {
        uint32_t n;
        uint8_t *c = slurp(cmosname, &n);
        if (c && n >= 0x400) memcpy(rb.cmos, c, 0x400);
        free(c);
    }
    rb_roms_t r;
    rbroms_roms(&r);
    rb_init(&r);
    if (trace && !trace_from) rb_trace = on_trace;
    if (strace && !trace_from) rb_strace = on_strace;
    FILE *dump = dumpname ? fopen(dumpname, "wb") : NULL;
    FILE *wav = wavname ? fopen(wavname, "wb") : NULL;
    if (wav) wav_header(wav, 0);
    uint32_t wav_n = 0, rd = 0, sd_rd = 0;
    uint8_t level = rb.dac;
    for (int n = 1; n <= frames; n++) {
        rb_run_to((uint64_t)n * RB_FRAME);
        if (wav) {                      // the sound CPU's time to 48 kHz
            uint64_t end = rb.snd_now * 48000 / RB_SND_HZ;
            static int16_t buf[2048];
            int k = 0;
            for (; wav_n < end; wav_n++) {
                uint64_t ts = ((uint64_t)wav_n * RB_SND_HZ + 24000) / 48000;
                while (rd != rb_dac_head && (((uint32_t)ts - (rb_dac[rd & (RB_DAC_RING - 1)] >> 8)) & 0xFFFFFF) < 0x800000)
                    level = (uint8_t)rb_dac[rd++ & (RB_DAC_RING - 1)];
                buf[k++] = (int16_t)((level - 128) * 64);     // MAME's: (level - 128) / 128 at 0.25
                if (k == 2048) fwrite(buf, 2, (size_t)k, wav), k = 0;
            }
            fwrite(buf, 2, (size_t)k, wav);
        }
        if (trace && n == trace_from) rb_trace = on_trace;
        if (strace && n == trace_from) rb_strace = on_strace;
        for (int i = 0; i < nev; i++) {
            if (ev[i].frame != n) continue;
            if (ev[i].port == 0) rb_in.in0 = (uint8_t)ev[i].v;
            else if (ev[i].port == 1) rb_in.in1 = (uint8_t)ev[i].v;
            else rb_in.in2 = (uint8_t)ev[i].v;
        }
        if (sdump) {
            fprintf(sdump, "%d %04X %02X %02X %04X %04X %02X %d %d %llu\n", n, rb.snd.pc, rb.snd.a, rb.snd.b, rb.snd.x,
                    rb.snd.s, rb.snd.cc, rb.snd.irq, rb.snd.wai, (unsigned long long)rb.snd_now);
            for (int i = 0; i < 0x100; i++) fprintf(sdump, "%02X%s", rb.snd_ram[i], (i & 31) == 31 ? "\n" : "");
            for (; sd_rd != rb_dac_head; sd_rd++) fprintf(sdump, "w %08X\n", rb_dac[sd_rd & (RB_DAC_RING - 1)]);
        }
        if (dump) {
            uint32_t fn = (uint32_t)n;
            fwrite("RBFR", 1, 4, dump);
            fwrite(&fn, 4, 1, dump);
            fwrite(rb.ram, 1, sizeof rb.ram, dump);
            fwrite(rb.pal, 1, 16, dump);
            fwrite(rb.cmos, 1, 0x400, dump);
        }
        if (ppm && every > 0 && n % every == 0) write_ppm(ppm, (uint32_t)n, &rb_frame);
    }
    if (dump) fclose(dump);
    if (sdump) fclose(sdump);
    if (wav) { fseek(wav, 0, SEEK_SET); wav_header(wav, wav_n); fclose(wav); }
    if (trace) fclose(trace);
    if (strace) fclose(strace);
    fprintf(stderr, "rb_host: %d frames, %llu main cycles, %llu sound cycles, %u DAC writes\n", frames,
            (unsigned long long)rb_time(), (unsigned long long)rb.snd_now, rb_dac_head);
    return 0;
}
#endif
