// cart_host.c -- the cart's side on the Mac: the board run a list at a time
// as the plugin runs it (src/rbrun.c), the picture and the sound through
// src/rbcv.c. For previews, the lists' numbers and the lists themselves.
// (Time Pilot's.)
//
//   cart_host ZIP [-lists N] [-script FILE] [-png DIR -at L,L,...] [-wav FILE]
//             [-out LISTS.bin] [-from L] [-stats FILE] [-poke FRAME:ADDR:VALUE]
//
// -script: rb_host's script ("frame :PORT hex"), each line's inputs set as
// the board finishes that frame, as MAME's frame_done sets them (64 cycles
// sooner: the last visible line's start), so the game is MAME's frame for
// frame. -png: at each list in -at, what the console shows (cv_LIST.png,
// 256 x 192 doubled), the picture before the TMS9918's two colours a place
// (ideal_LIST.png), the board's frame the picture was made from
// (board_LIST.png, 292 x 240 doubled) and the two side by side, the
// console's at the TV's 4:3 (pair_LIST.png). -wav: the DAC's samples, the
// attenuators the lists carry as levels (the measured ladder) at the
// lists' 5,962 Hz. -out: the lists from list -from on, as test/host/host.c
// records them (the frame kernel with controller 2), for
// tools/mame-replay.sh. -stats: a line a list (the list, its length, its
// picture bytes, pictures so far, bytes still to send, the layout, the
// layout the shown frame's border says, that frame, pieces so far, what
// the list spent on the picture). -poke: a byte of the RAM
// set as the board finishes a frame. All of these are made from the ROMs:
// keep them under $TMPDIR.
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rb.h"
#include "rbcv.h"
#include "rbroms.h"
#include "rbrun.h"
#include "rbsnd.h"

int rb_host_load(const char *zipname);     // rb_host.c

// ---- PNG, stored (no compression) ------------------------------------------------------
static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n) {
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    c = ~c;
    while (n--) c = crc_table[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static void chunk(FILE *f, const char *type, const uint8_t *d, size_t n) {
    uint8_t h[8];
    be32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(d, 1, n, f);
    be32(h, crc(crc(0, (const uint8_t *)type, 4), d, n));
    fwrite(h, 1, 4, f);
}

static void png(const char *name, int w, int h, const uint8_t *rgb, int scale) {
    int W = w * scale, H = h * scale;
    size_t row = (size_t)W * 3 + 1, raw_n = row * H;
    uint8_t *raw = malloc(raw_n);
    for (int y = 0; y < H; y++) {
        raw[y * row] = 0;
        for (int x = 0; x < W; x++) memcpy(raw + y * row + 1 + x * 3, rgb + ((size_t)(y / scale) * w + x / scale) * 3, 3);
    }
    size_t blocks = (raw_n + 65534) / 65535, z_n = 2 + raw_n + blocks * 5 + 4;
    uint8_t *z = malloc(z_n), *p = z;
    *p++ = 0x78; *p++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw_n; off += 65535) {
        size_t n = raw_n - off < 65535 ? raw_n - off : 65535;
        *p++ = off + n == raw_n;
        *p++ = (uint8_t)n; *p++ = (uint8_t)(n >> 8); *p++ = (uint8_t)~n; *p++ = (uint8_t)(~n >> 8);
        memcpy(p, raw + off, n);
        p += n;
    }
    be32(p, b << 16 | a);
    p += 4;
    FILE *f = fopen(name, "wb");
    if (!f) { fprintf(stderr, "cart_host: can't write %s\n", name); exit(1); }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)W); be32(ihdr + 4, (uint32_t)H);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (size_t)(p - z));
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

static void cv_png(const char *name, const uint8_t *px) {
    static uint8_t rgb[RBCV_W * RBCV_H * 3];
    for (int i = 0; i < RBCV_W * RBCV_H; i++) {
        uint32_t c = rbcv_tms_rgb[px[i] & 15];
        rgb[i * 3] = (uint8_t)(c >> 16); rgb[i * 3 + 1] = (uint8_t)(c >> 8); rgb[i * 3 + 2] = (uint8_t)c;
    }
    png(name, RBCV_W, RBCV_H, rgb, 2);
}

// the board's frame as RGB (292 x 240)
static void board_rgb(const rb_frame_t *f, uint8_t *rgb) {
    for (int y = 0; y < RB_VIS_H; y++)
        for (int x = 0; x < RB_VIS_W; x++) {
            uint8_t b = f->pix[y][x / 2], p = x & 1 ? b & 15 : b >> 4;
            uint32_t c = rbcv_pal_rgb(f->pal[y][p]);
            uint8_t *o = rgb + (y * RB_VIS_W + x) * 3;
            o[0] = (uint8_t)(c >> 16); o[1] = (uint8_t)(c >> 8); o[2] = (uint8_t)c;
        }
}

// the board's frame (292 x 240, doubled: 584 x 480) and the console's
// (256 x 192 at the TV's 4:3: 640 x 480), on grey
static void pair_png(const char *name, const uint8_t *m, const uint8_t *px) {
    enum { MW = 584, MH = 480, CW = 640, PW = MW + 16 + CW, PH = MH };
    static uint8_t out[PW * PH * 3];
    memset(out, 0x30, sizeof out);
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) memcpy(out + (y * PW + x) * 3, m + ((y / 2) * RB_VIS_W + x / 2) * 3, 3);
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < CW; x++) {
            uint32_t c = rbcv_tms_rgb[px[(y * RBCV_H / MH) * RBCV_W + x * RBCV_W / CW] & 15];
            uint8_t *o = out + (y * PW + MW + 16 + x) * 3;
            o[0] = (uint8_t)(c >> 16); o[1] = (uint8_t)(c >> 8); o[2] = (uint8_t)c;
        }
    png(name, PW, PH, out, 1);
}

// ---- the inputs, as the board finishes each frame --------------------------------------
static struct { int frame, port, v; } fev[65536];
static int nfev;
static struct { int frame; uint16_t addr; uint8_t v; } pokes[16];
static int npokes;

static rb_frame_t shown;
static int shown_raw;                   // the layout the frame's own border says (before the hold)
static void on_picture(void) { shown = rb_frame; shown_raw = rbcv_layout_of(&rb_frame); }

static void on_frame(void) {
    int n = (int)rb.frames;
    for (int i = 0; i < nfev; i++) {
        if (fev[i].frame != n) continue;
        if (fev[i].port == 0) rb_in.in0 = (uint8_t)fev[i].v;
        else if (fev[i].port == 1) rb_in.in1 = (uint8_t)fev[i].v;
        else rb_in.in2 = (uint8_t)fev[i].v;
    }
    for (int i = 0; i < npokes; i++)
        if (pokes[i].frame == n && pokes[i].addr < 0xC000) rb.ram[pokes[i].addr] = pokes[i].v;
    rbrun_frame();
}

// ---- the lists' sound (the DAC's levels) ---------------------------------------------------
static int a0 = 15, a1 = 15, a2 = 15;
static int list_samples(const uint8_t *l, size_t n, int16_t *out) {
    int k = 0;
    size_t i = 0;
    bool vblank = true;
    while (i < n) {
        uint8_t c = l[i];
        if (c == 1) i += 4 + l[i + 3];
        else if (c == 2) {
            int m = l[i + 1];
            bool got = false;
            for (int j = 0; j < m; j++) {
                uint8_t b = l[i + 2 + j];
                if ((b & 0xF0) == 0x90) { a0 = b & 15; got = true; }
                if ((b & 0xF0) == 0xB0) { a1 = b & 15; got = true; }
                if ((b & 0xF0) == 0xD0) { a2 = b & 15; got = true; }
            }
            if (got && k < RBCV_PCM_N) out[k++] = (int16_t)((rbcv_dac_level(a0, a1, a2) - 1.20f) * 12500.0f);
            i += 2 + (size_t)m;
        } else if (c == 3 && vblank) i += 3;
        else if (c == 0 && vblank) { vblank = false; i++; }
        else break;
    }
    return k;
}

static void wav_header(FILE *f, uint32_t rate, uint32_t n) {
    uint32_t v;
    uint16_t h;
    fwrite("RIFF", 1, 4, f); v = 36 + n * 2; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    h = 1; fwrite(&h, 2, 1, f); fwrite(&h, 2, 1, f);
    fwrite(&rate, 4, 1, f); v = rate * 2; fwrite(&v, 4, 1, f);
    h = 2; fwrite(&h, 2, 1, f); h = 16; fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f); v = n * 2; fwrite(&v, 4, 1, f);
}

static bool in_list(const char *at, int l) {
    char key[16];
    snprintf(key, sizeof key, "%d", l);
    const char *s = at;
    while (*s) {
        const char *e = strchr(s, ',');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len == strlen(key) && !strncmp(s, key, len)) return true;
        s = e ? e + 1 : s + len;
    }
    return false;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: cart_host ZIP [options]\n"); return 2; }
    const char *script = NULL, *pngdir = NULL, *at = "", *wavname = NULL, *statsname = NULL, *outname = NULL;
    int lists = 2400, from = 1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-lists")) lists = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-script")) script = argv[++i];
        else if (!strcmp(argv[i], "-png")) pngdir = argv[++i];
        else if (!strcmp(argv[i], "-at")) at = argv[++i];
        else if (!strcmp(argv[i], "-wav")) wavname = argv[++i];
        else if (!strcmp(argv[i], "-stats")) statsname = argv[++i];
        else if (!strcmp(argv[i], "-out")) outname = argv[++i];
        else if (!strcmp(argv[i], "-from")) from = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-poke") && npokes < 16) {
            unsigned f, a, v;
            if (sscanf(argv[++i], "%u:%x:%x", &f, &a, &v) != 3) { fprintf(stderr, "cart_host: -poke FRAME:ADDR:VALUE\n"); return 2; }
            pokes[npokes].frame = (int)f; pokes[npokes].addr = (uint16_t)a; pokes[npokes].v = (uint8_t)v; npokes++;
        } else { fprintf(stderr, "cart_host: what is %s?\n", argv[i]); return 2; }
    }
    if (rb_host_load(argv[1])) return 1;
    if (script) {
        FILE *f = fopen(script, "r");
        if (!f) { fprintf(stderr, "cart_host: can't read %s\n", script); return 1; }
        char line[128], port[16];
        while (fgets(line, sizeof line, f) && nfev < 65536)
            if (sscanf(line, "%d %15s %x", &fev[nfev].frame, port, &fev[nfev].v) == 3)
                fev[nfev].port = strstr(port, "IN0") ? 0 : strstr(port, "IN1") ? 1 : 2, nfev++;
        fclose(f);
    }
    static rbcv_t cv;
    static rbrun_t run;
    rb_roms_t roms;
    rbroms_roms(&roms);
    rbcv_init(&cv);
    rb_init(&roms);
    rbrun_start(&run, &cv);
    rb_on_frame = on_frame;
    run.on_picture = on_picture;

    FILE *wav = wavname ? fopen(wavname, "wb") : NULL;
    const uint32_t rate = (uint32_t)((uint64_t)RBCV_PCM_N * RBCV_CONSOLE_HZ / RBCV_LIST_T);
    if (wav) wav_header(wav, rate, 0);
    uint32_t nsamples = 0;
    FILE *stats = statsname ? fopen(statsname, "w") : NULL;
    FILE *out = outname ? fopen(outname, "wb") : NULL;
    static uint8_t list[RBCV_LIST_MAX], px[RBCV_W * RBCV_H];
    static uint8_t brgb[RB_VIS_W * RB_VIS_H * 3];
    uint64_t bytes_sum = 0;
    uint32_t len_max = 0, pics0 = 0, sec_pics = 0, worst_sec = 1000, best_sec = 0, secs = 0, backlog_max = 0;
    for (int l = 1; l <= lists; l++) {
        uint32_t b0 = cv.stats.pat_bytes + cv.stats.col_bytes;
        size_t n = rbrun_list(&run, list);
        uint32_t b1 = cv.stats.pat_bytes + cv.stats.col_bytes;
        bytes_sum += b1 - b0;
        if (n > len_max) len_max = (uint32_t)n;
        if (wav) {
            int16_t s[RBCV_PCM_N];
            int k = list_samples(list, n, s);
            fwrite(s, 2, (size_t)k, wav);
            nsamples += (uint32_t)k;
        }
        if (out && l >= from) {
            const uint8_t h[9] = {'F', 1, 0, (uint8_t)n, (uint8_t)(n >> 8), 0, 0, 0, 0};
            fwrite(h, 1, 9, out);
            fwrite(list, 1, n, out);
        }
        if (stats) fprintf(stats, "%d %zu %u %u %u %u %d %u %u %d\n", l, n, b1 - b0, run.pictures, cv.dirty ? 1 : 0, cv.layout, shown_raw, shown.frame, run.pieces, RBRUN_BUDGET - run.budget);
        if (l % 60 == 0) {              // pictures a second: each 60 lists (about a second)
            uint32_t p = run.pictures - pics0;
            pics0 = run.pictures;
            if (l > 120) {
                sec_pics += p;
                secs++;
                if (p < worst_sec) worst_sec = p;
                if (p > best_sec) best_sec = p;
            }
        }
        if (pngdir && *at && in_list(at, l)) {
            char name[1024];
            rbcv_screen(&cv, px);
            snprintf(name, sizeof name, "%s/cv_%d.png", pngdir, l);
            cv_png(name, px);
            rbcv_ideal(&cv, px);
            snprintf(name, sizeof name, "%s/ideal_%d.png", pngdir, l);
            cv_png(name, px);
            board_rgb(&shown, brgb);
            snprintf(name, sizeof name, "%s/board_%d.png", pngdir, l);
            png(name, RB_VIS_W, RB_VIS_H, brgb, 2);
            rbcv_screen(&cv, px);
            snprintf(name, sizeof name, "%s/pair_%d.png", pngdir, l);
            pair_png(name, brgb, px);
            uint32_t bl = rbcv_backlog(&cv);
            if (bl > backlog_max) backlog_max = bl;
            printf("list %d: frame %u, layout %s, backlog %u\n", l, shown.frame, cv.layout == RBCV_PLAY ? "play" : "full", bl);
        }
    }
    if (wav) { fseek(wav, 0, SEEK_SET); wav_header(wav, rate, nsamples); fclose(wav); }
    if (stats) fclose(stats);
    if (out) fclose(out);
    const rbcv_stats_t *st = &cv.stats;
    printf("cart_host: %d lists, %u pictures (%.1f a second; seconds %u to %u), waited %u, pieces %u\n", lists, run.pictures,
           secs ? (double)sec_pics / secs * 59.62 / 60.0 : 0.0, worst_sec, best_sec, run.waited, run.pieces);
    printf("  bytes %llu (pattern %u, colour %u, refreshed %u), %.0f a list, %.0f a picture; runs %u; longest list %u bytes, %u T\n",
           (unsigned long long)bytes_sum, st->pat_bytes, st->col_bytes, st->refreshed, (double)bytes_sum / lists,
           run.pictures ? (double)bytes_sum / run.pictures : 0.0, st->runs, len_max, st->t_max);
    printf("  settled %u lists, lines %u, cells %u, layouts %u, clipped samples %u, DAC writes waiting %u at most (ring %u), lost %u\n",
           st->settled, st->lines, st->cells, st->layouts, st->clipped, rbsnd_waiting_max, RB_DAC_RING, rbsnd_lost);
    return 0;
}
