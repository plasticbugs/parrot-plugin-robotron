// rbcv.c -- see rbcv.h. The lists, the DAC and the two-colour picker are
// Time Pilot's (Parrot Cart's timeplt/tpcv.c, from Donkey Kong's); the
// picture is new: a bitmap, scaled line by line as the board changes it.
#include "rbcv.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cvscale.h"
#include "rbdac3.h"

// ---- lists (from gbkernel.asm's costs, with controller 2) ---------------------------------
#define FRAME_T    (262 * 228)
#define VBLANK_T   (70 * 228)
#define C_START    660                  // (480, and 179 for controller 2)
#define C_RUN_VB   106
#define C_BYTE_VB  23
#define C_RUN_ACT  106
#define C_BYTE_ACT 31
#define C_PSG      75
#define C_PSG_BYTE 52
#define C_END_VB   54
#define C_END      52
#define VB_LIMIT   (VBLANK_T - 600)
#define LIST_LIMIT (FRAME_T - 1200)

// The console goes from one list straight into the next, so a list's
// length is the sample clock. Between them: 357 T, and 179 for controller 2
// (gbkernel.asm: "179 T more between lists that run back to back").
#define C_FRAME    (357 + 179)
#define PCM_L      RBCV_LIST_T
#define PCM_FIRST  (C_FRAME + C_END_VB)
#define C_SAMPLE   (C_PSG + 3 * C_PSG_BYTE)
#define PCM_STEP   (((PCM_L - PCM_FIRST - C_SAMPLE - C_END) << 8) / (RBCV_PCM_N - 1))

#define PATTERNS 0x0000
#define COLORS   0x2000
#define NAMES    0x1800
#define BLACK    1

typedef struct {
    uint8_t *b;
    size_t n;
    uint32_t t;
    bool vblank;
} list_t;

static void l_run_head(list_t *l, uint16_t addr, int n) {
    l->t += l->vblank ? C_RUN_VB + n * C_BYTE_VB : C_RUN_ACT + n * C_BYTE_ACT;
    l->b[l->n++] = 1;
    l->b[l->n++] = (uint8_t)addr;
    l->b[l->n++] = (uint8_t)((addr >> 8) & 0x3F) | 0x40;
    l->b[l->n++] = (uint8_t)n;
}

static void l_psg(list_t *l, const uint8_t *d, int n) {
    l->t += C_PSG + C_PSG_BYTE * n;
    l->b[l->n++] = 2;
    l->b[l->n++] = (uint8_t)n;
    memcpy(l->b + l->n, d, n);
    l->n += n;
}

static void l_end_vblank(list_t *l) {
    if (!l->vblank) return;
    l->vblank = false;
    l->t += C_END_VB;
    l->b[l->n++] = 0;
}

static int room(const list_t *l) {
    int t = l->vblank ? ((int)VB_LIMIT - (int)l->t - C_END_VB - C_RUN_VB) / C_BYTE_VB
                      : ((int)LIST_LIMIT - (int)l->t - C_END - C_RUN_ACT) / C_BYTE_ACT;
    return t > 255 ? 255 : t;
}

// ---- colours ------------------------------------------------------------------------
const uint32_t rbcv_tms_rgb[16] = {0x000000, 0x000000, 0x21C842, 0x5EDC78, 0x5455ED, 0x7D76FC, 0xD4524D, 0x42EBF5,
                                   0xFC5554, 0xFF7978, 0xD4C154, 0xE6CE80, 0x21B03B, 0xC95BBA, 0xCCCCCC, 0xFFFFFF};

// MAME's palette_init: each gun's resistors (1200, 560, 330 ohms for red
// and green, 560 and 330 for blue), the full swing 255
uint32_t rbcv_pal_rgb(uint8_t v) {
    static const float rg[3] = {1.0f / 1200, 1.0f / 560, 1.0f / 330}, bw[2] = {1.0f / 560, 1.0f / 330};
    const float rs = rg[0] + rg[1] + rg[2], bs = bw[0] + bw[1];
    float r = ((v & 1 ? rg[0] : 0) + (v & 2 ? rg[1] : 0) + (v & 4 ? rg[2] : 0)) / rs;
    float g = ((v & 8 ? rg[0] : 0) + (v & 16 ? rg[1] : 0) + (v & 32 ? rg[2] : 0)) / rs;
    float b = ((v & 64 ? bw[0] : 0) + (v & 128 ? bw[1] : 0)) / bs;
    return (uint32_t)(r * 255 + 0.5f) << 16 | (uint32_t)(g * 255 + 0.5f) << 8 | (uint32_t)(b * 255 + 0.5f);
}

static float tms_lab[16][3];
static uint16_t nearer[16][16];         // [c][a] bit b: c is nearer a than b
#define NEARER(c, a, b) ((nearer[c][a] >> (b)) & 1)
static uint8_t pal_tms[256];            // a palette byte's TMS9918 colour (black: 0)
static bool colors_ready;

static void oklab(uint32_t rgb, float *out) {
    float c[3];
    for (int i = 0; i < 3; i++) {
        float v = (float)((rgb >> (16 - 8 * i)) & 255) / 255.0f;
        c[i] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
    }
    float l = cbrtf(0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2]);
    float m = cbrtf(0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2]);
    float s = cbrtf(0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2]);
    out[0] = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    out[1] = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    out[2] = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
}

static float d2(const float *a, const float *b) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z;
}

// The nearest TMS9918 colour to each palette byte, in OKLab; a grey (little
// chroma) only to black, grey or white. A colour that isn't black never
// becomes black (the dark blues and reds of the men's details would vanish):
// it takes its nearest other colour.
static void colors_init(void) {
    for (int i = 1; i < 16; i++) oklab(rbcv_tms_rgb[i], tms_lab[i]);
    for (int c = 1; c < 16; c++)
        for (int a = 1; a < 16; a++)
            for (int b = 1; b < 16; b++)
                if (d2(tms_lab[c], tms_lab[a]) <= d2(tms_lab[c], tms_lab[b])) nearer[c][a] |= (uint16_t)(1u << b);
    for (int v = 0; v < 256; v++) {
        if (!v) { pal_tms[v] = 0; continue; }
        float lab[3];
        oklab(rbcv_pal_rgb((uint8_t)v), lab);
        bool grey = lab[1] * lab[1] + lab[2] * lab[2] < 0.03f * 0.03f;
        uint8_t best = 2;
        float bd2 = 1e9f;
        for (int t = 2; t < 16; t++) {
            if (grey && t != 14 && t != 15) continue;
            float d = d2(lab, tms_lab[t]);
            if (d < bd2) { bd2 = d; best = (uint8_t)t; }
        }
        // a very dark one (the darkest blues, greys and reds) black after all
        pal_tms[v] = lab[0] < 0.25f ? 0 : best;
    }
    colors_ready = true;
}

// ---- the layouts ----------------------------------------------------------------------
// A column map: TMS9918 column c is the board's (visible) column scol[c],
// with the one after it if pair[c]. A row map: TMS9918 line r is the
// board's line srow[r] (with the one after it if rpair[r]), its columns by
// map rcmap[r]; lines a layout doesn't show have no map (NONE).
enum { CM_ONE, CM_PLAY, CM_FULL, CMAPS, NONE = 0xff };
#define ONE_SHIFT 18                    // the scores' and the wave's columns, at the arcade's size
static uint16_t scol[CMAPS][RBCV_W];
static uint8_t cpair[CMAPS][RBCV_W];
static uint8_t srow[2][RBCV_H], rpair[2][RBCV_H], rcmap[2][RBCV_H];
static uint8_t line_cmap[2][RB_VIS_H];  // the column map each of the board's lines is scaled by (NONE: not shown)
// the places (a bit a column of 8) each word (4 bytes, 8 pixels) of a
// board line reaches, by a column map: its pixels, and as a neighbour
static uint32_t word_cells[CMAPS][RB_VIS_WORDS];
static uint16_t pen_bits[256];          // a VRAM byte's two pens, a bit each
static bool maps_ready;

// n of the board's columns (or lines) from s on into m: (n - m) pairs, spread evenly
static void spread(int s, int n, int m, uint16_t *pos, uint8_t *pair) {
    int p = n - m, src = s, j = 0;
    for (int k = 0; k < m; k++) {
        bool is = j < p && k == (int)(((2 * j + 1) * m) / (2 * p));
        pos[k] = (uint16_t)src;
        pair[k] = is;
        src += is ? 2 : 1;
        if (is) j++;
    }
}

static void maps_init(void) {
    for (int c = 0; c < RBCV_W; c++) scol[CM_ONE][c] = (uint16_t)(c + ONE_SHIFT), cpair[CM_ONE][c] = 0;
    spread(6, 278, RBCV_W, scol[CM_PLAY], cpair[CM_PLAY]);
    spread(0, RB_VIS_W, RBCV_W, scol[CM_FULL], cpair[CM_FULL]);
    uint16_t pos[RBCV_H];
    uint8_t pr[RBCV_H];
    // the play layout: the scores, the play field, the wave
    memset(srow, 0, sizeof srow);
    memset(rpair, 0, sizeof rpair);
    for (int r = 0; r < 8; r++) srow[RBCV_PLAY][r] = (uint8_t)(7 + r), rcmap[RBCV_PLAY][r] = CM_ONE;
    spread(15, 215, 179, pos, pr);
    for (int r = 0; r < 179; r++)
        srow[RBCV_PLAY][8 + r] = (uint8_t)pos[r], rpair[RBCV_PLAY][8 + r] = pr[r], rcmap[RBCV_PLAY][8 + r] = CM_PLAY;
    for (int r = 0; r < 5; r++) srow[RBCV_PLAY][187 + r] = (uint8_t)(231 + r), rcmap[RBCV_PLAY][187 + r] = CM_ONE;
    // the whole of it
    spread(0, RB_VIS_H, RBCV_H, pos, pr);
    for (int r = 0; r < RBCV_H; r++)
        srow[RBCV_FULL][r] = (uint8_t)pos[r], rpair[RBCV_FULL][r] = pr[r], rcmap[RBCV_FULL][r] = CM_FULL;
    for (int l = 0; l < 2; l++) {
        memset(line_cmap[l], NONE, RB_VIS_H);
        for (int r = 0; r < RBCV_H; r++) {
            int y = srow[l][r];
            line_cmap[l][y] = rcmap[l][r];
            if (rpair[l][r]) line_cmap[l][y - 1] = line_cmap[l][y + 1] = line_cmap[l][y + 2] = rcmap[l][r];
        }
    }
    // each place's columns of the board: its own 8 TMS9918 columns' and their
    // neighbours' (a pair of lines looks along the line), a pair's both and
    // those either side
    memset(word_cells, 0, sizeof word_cells);
    for (int m = 0; m < CMAPS; m++)
        for (int cx = 0; cx < 32; cx++) {
            int c0 = cx * 8 - 1 < 0 ? 0 : cx * 8 - 1, c1 = cx * 8 + 8 > RBCV_W - 1 ? RBCV_W - 1 : cx * 8 + 8;
            int lo = scol[m][c0] - (cpair[m][c0] ? 1 : 0), hi = scol[m][c1] + (cpair[m][c1] ? 2 : 0);
            if (lo < 0) lo = 0;
            if (hi > RB_VIS_W - 1) hi = RB_VIS_W - 1;
            for (int w = lo / 8; w <= hi / 8; w++) word_cells[m][w] |= 1u << cx;
        }
    for (int b = 0; b < 256; b++) pen_bits[b] = (uint16_t)(1u << (b >> 4) | 1u << (b & 15));
    maps_ready = true;
}

// ---- the picture's state ---------------------------------------------------------------
// Each of the board's lines' colours as TMS9918 colours (lut, black 0),
// and each line scaled along itself by its column map (hs: 256 TMS9918
// colours, a nibble each, a word a place). The board's lines themselves
// are its frame buffer's (rb_frame), which says what changed. In the
// plugin's room before the game.
typedef struct {
    uint8_t lut[RB_VIS_H][16];
    uint32_t hs[RB_VIS_H][32];
} pic_t;
static union {
    pic_t p;
    uint8_t room[sizeof(pic_t) > RBCV_ROOM ? sizeof(pic_t) : RBCV_ROOM];
    uint64_t align;
} pic_u;
#define P (pic_u.p)
static bool pic_clear;
static uint32_t line_cells[RB_VIS_H];   // the places each line changed (this picture)

uint8_t *rbcv_scratch(size_t *n) {
    *n = sizeof pic_u.room;
    pic_clear = false;
    return pic_u.room;
}

// a pixel of a line of the board's (row: its VRAM bytes), as its TMS9918 colour; 0 off the edges
#define PX(row, lut, x) ((unsigned)(x) < RB_VIS_W ? (lut)[((x) & 1) ? (row)[(x) >> 1] & 15 : (row)[(x) >> 1] >> 4] : 0)

// line y's places in cells scaled along the line again, by column map m
RBCV_HOT static void hscale(const uint8_t *row, int y, uint32_t cells, int m) {
    const uint8_t *lut = P.lut[y];
    uint32_t *o = P.hs[y];
    const uint16_t *sc = scol[m];
    const uint8_t *cp = cpair[m];
    for (; cells; cells &= cells - 1) {
        int cx = __builtin_ctz(cells);
        uint32_t w = 0;
        for (int i = 0, c = cx * 8; i < 8; i++, c++) {
            int x = sc[c];
            uint32_t v = !cp[c] ? PX(row, lut, x)
                                : cvscale_pick(PX(row, lut, x), PX(row, lut, x + 1), PX(row, lut, x - 1), PX(row, lut, x + 2));
            w |= v << (4 * i);
        }
        o[cx] = w;
    }
}

// ---- the two colours of 8 pixels -------------------------------------------------------
// 8 pixels (TMS9918 colours) as a pattern byte and a colour byte, the
// colour byte kept as the console has it where it will do (Time Pilot's)
static inline __attribute__((always_inline)) void span(rbcv_t *e, unsigned a, const uint8_t *c) {
    uint8_t vc = e->vram_col[a], vp = e->vram_pat[a];
    uint8_t c0 = c[0];
    if (c[1] == c0 && c[2] == c0 && c[3] == c0 && c[4] == c0 && c[5] == c0 && c[6] == c0 && c[7] == c0) {
        uint8_t vf = vc >> 4, vb = vc & 15;
        if (vf == c0 || vb == c0) {
            e->want_col[a] = vc;
            e->want_pat[a] = vf == vb ? vp : vf == c0 ? 0xFF : 0x00;
        } else {
            e->want_col[a] = (uint8_t)(c0 << 4 | c0);
            e->want_pat[a] = vp;
        }
        return;
    }
    {                                   // two colours
        uint8_t d = 0;
        unsigned m0 = 0;
        int i = 0;
        for (; i < 8; i++) {
            uint8_t x = c[i];
            if (x == c0) m0 |= 0x80u >> i;
            else if (d && x != d) break;
            else d = x;
        }
        if (i == 8) {
            static const uint8_t pop4[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
            int n0 = pop4[m0 & 15] + pop4[m0 >> 4];
            uint8_t a1 = n0 > 4 ? c0 : n0 < 4 ? d : (c0 < d ? c0 : d), a2 = a1 == c0 ? d : c0;
            uint8_t vf = vc >> 4, vb = vc & 15, fg, bg;
            if (vf == a1 && vb == a2) { fg = a1; bg = a2; }
            else { fg = a2; bg = a1; }
            e->want_pat[a] = (uint8_t)(fg == c0 ? m0 : ~m0 & 0xFF);
            e->want_col[a] = (uint8_t)(fg << 4 | bg);
            return;
        }
    }
    uint8_t cnt[16] = {0};
    for (int i = 0; i < 8; i++) cnt[c[i]]++;
    // the two most used (ties: the lower colour), black first among equals:
    // a man's colours go on black
    uint8_t a1 = 0, a2 = 0;
    for (int k = 1; k < 16; k++)
        if (cnt[k] > cnt[a1] || (!a1 && cnt[k])) a1 = (uint8_t)k;
    for (int k = 1; k < 16; k++)
        if (k != a1 && cnt[k] && (!a2 || cnt[k] > cnt[a2])) a2 = (uint8_t)k;
    uint8_t fg, bg, vf = vc >> 4, vb = vc & 15;
    if (vf == a1 && vb == a2) { fg = a1; bg = a2; }
    else if (vf == a2 && vb == a1) { fg = a2; bg = a1; }
    else { fg = a2; bg = a1; }
    uint8_t pat = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t x = c[i];
        bool on = x == fg || (x != bg && NEARER(x, fg, bg));
        pat = (uint8_t)(pat << 1 | on);
    }
    e->want_pat[a] = pat;
    e->want_col[a] = (uint8_t)(fg << 4 | bg);
}

// ---- making the places -------------------------------------------------------------------
// TMS9918 line r's place cx (8 pixels), as TMS9918 colours (black 0), into c
static inline void unpack(uint32_t w, uint8_t *c) {
    for (int i = 0; i < 8; i++, w >>= 4) c[i] = (uint8_t)(w & 15);
}

RBCV_HOT static void cell_colors(int layout, int r, int cx, uint8_t *c) {
    int y = srow[layout][r];
    if (!rpair[layout][r]) {
        unpack(P.hs[y][cx], c);
        return;
    }
    // the pair's two lines with a column either side (0 off the edges), and those above and below
    uint8_t u[10], v[10], b[8], a[8];
    const uint32_t *hu = P.hs[y], *hv = P.hs[y + 1];
    unpack(hu[cx], u + 1);
    unpack(hv[cx], v + 1);
    unpack(P.hs[y - 1][cx], b);
    unpack(P.hs[y + 2][cx], a);
    u[0] = cx ? (uint8_t)(hu[cx - 1] >> 28) : 0;
    v[0] = cx ? (uint8_t)(hv[cx - 1] >> 28) : 0;
    u[9] = cx < 31 ? (uint8_t)(hu[cx + 1] & 15) : 0;
    v[9] = cx < 31 ? (uint8_t)(hv[cx + 1] & 15) : 0;
    for (int i = 0; i < 8; i++) {
        uint8_t m = cvscale_pick(u[i + 1], v[i + 1], b[i], a[i]);
        if (cvscale_mid_asks(m, u[i + 1], v[i + 1], b[i], a[i])) m = cvscale_mid_then(u[i + 1], v[i + 1], u[i], u[i + 2], v[i], v[i + 2]);
        c[i] = m;
    }
}

RBCV_HOT static void make_cell(rbcv_t *e, int r, int cx) {
    uint8_t c[8];
    cell_colors(e->layout, r, cx, c);
    for (int i = 0; i < 8; i++)
        if (!c[i]) c[i] = BLACK;
    span(e, (unsigned)((r >> 3) * 256 + cx * 8 + (r & 7)), c);
    e->bands |= 1u << (r >> 3);
}

// ---- the picture -----------------------------------------------------------------------
// The play layout when the play field's border is there: lines 15 and 229
// (the border's outer and inner lines are two each) lit in one colour at
// columns 8 to 280, and its sides at 6 and 283
int rbcv_layout_of(const rb_frame_t *f) {
    static const uint8_t rows[] = {15, 16, 228, 229};
    for (int i = 0; i < 4; i++) {
        const uint8_t *p = f->pix[rows[i]];
        uint8_t v = p[4];
        if (!v || (v >> 4) != (v & 15)) return RBCV_FULL;
        for (int x = 8; x < 140; x += 11)
            if (p[x] != v) return RBCV_FULL;
    }
    for (int y = 30; y < 220; y += 40) {
        const uint8_t *p = f->pix[y];
        if (!(p[3] >> 4) || !(p[141] & 15)) return RBCV_FULL;
    }
    return RBCV_PLAY;
}

// A picture is made a piece at a time, as the budget allows: the board's
// lines from the top (each scaled along itself where it changed), and each
// TMS9918 line once its board lines (and those either side of a pair) are
// done. What isn't done by the end of the budget is done at the next frame,
// from that frame's lines: a picture that changes everywhere (the
// rectangles between waves) comes a few frames late and torn, but no list
// takes longer than its time. The cost is in places (8 x 1 pixels) made,
// each about 450 cycles on the cart: a board line's place scaled along the
// line costs 1, a TMS9918 place made from them 1, a line whose pens changed
// colour 1 more (its words are looked at for them); lines and places that
// didn't change cost next to nothing, and nothing here.
static bool row_ready(const rbcv_t *e, int r) {
    int y = srow[e->layout][r];
    return (rpair[e->layout][r] ? y + 2 : y) < e->pass_y;
}

RBCV_HOT bool rbcv_picture(rbcv_t *e, rb_frame_t *f, int32_t *budget) {
    if (!colors_ready) colors_init();
    if (!maps_ready) maps_init();
    if (!pic_clear) {                   // (the plugin's room before the game)
        memset(&P, 0, sizeof P);
        pic_clear = true;
        e->pic_valid = false;
        e->in_pass = false;
    }
    if (!e->in_pass) {
        // the play layout until the border has been gone RBCV_HOLD frames (the
        // rectangles between waves, a man lost): no layout back and forth
        int layout = rbcv_layout_of(f);
        if (layout == RBCV_PLAY) e->play_frame = f->frame;
        else if (e->layout == RBCV_PLAY && f->frame - e->play_frame < RBCV_HOLD) layout = RBCV_PLAY;
        if (layout != e->layout) {
            e->layout = (uint8_t)layout;
            e->pic_valid = false;
            e->stats.layouts++;
        }
        e->in_pass = true;
        e->pass_all = !e->pic_valid;
        e->pass_y = e->pass_r = 0;
    }
    const int layout = e->layout;
    const bool all = e->pass_all;
    const uint8_t *cmap = line_cmap[layout];
    int32_t left = *budget;
    while (left > 0) {
        // the TMS9918's lines whose board lines are all done: the places any
        // of those changed
        if (e->pass_r < RBCV_H && (e->pass_y >= RB_VIS_H || row_ready(e, e->pass_r))) {
            int r = e->pass_r++;
            int y = srow[layout][r];
            uint32_t cells = rpair[layout][r] ? line_cells[y - 1] | line_cells[y] | line_cells[y + 1] | line_cells[y + 2]
                                              : line_cells[y];
            for (; cells; cells &= cells - 1) make_cell(e, r, __builtin_ctz(cells)), e->stats.cells++, left--;
            continue;
        }
        if (e->pass_y >= RB_VIS_H) break;
        // the next board line: the words the frame buffer says changed, and
        // those with a pen whose TMS9918 colour changed; scaled along itself
        // again where it changed
        int y = e->pass_y++;
        line_cells[y] = 0;
        uint64_t ch = f->changed[y];
        uint32_t pens = f->pens[y];
        f->changed[y] = 0;
        f->pens[y] = 0;
        int m = cmap[y];
        if (m == NONE) continue;
        const uint8_t *row = f->pix[y];
        uint32_t cells = 0;
        if (all) {
            for (int i = 0; i < 16; i++) P.lut[y][i] = pal_tms[f->pal[y][i]];
            cells = ~0u;
        } else {
            uint32_t tp = 0;
            for (; pens; pens &= pens - 1) {
                int i = __builtin_ctz(pens);
                uint8_t c = pal_tms[f->pal[y][i]];
                if (c != P.lut[y][i]) P.lut[y][i] = c, tp |= 1u << i;
            }
            if (tp) {                   // (the game cycles a few pens' colours every frame)
                left -= 1;
                for (int w = 0; w < RB_VIS_WORDS; w++) {
                    const uint8_t *q = row + 4 * w;
                    if ((pen_bits[q[0]] | pen_bits[q[1]] | (w < RB_VIS_WORDS - 1 ? pen_bits[q[2]] | pen_bits[q[3]] : 0)) & tp)
                        ch |= 1ull << w;
                }
            }
            const uint32_t *wc = word_cells[m];
            for (; ch; ch &= ch - 1) cells |= wc[__builtin_ctzll(ch)];
        }
        if (cells) {
            hscale(row, y, cells, m);
            line_cells[y] = cells;
            left -= __builtin_popcount(cells);
            e->stats.lines++;
        }
    }
    *budget = left;
    e->dirty = true;
    if (e->pass_r < RBCV_H) return false;
    e->in_pass = false;
    e->pic_valid = true;
    e->stats.pictures++;
    return true;
}

void rbcv_canvas(rbcv_t *e, const uint8_t *px) {
    if (!colors_ready) colors_init();
    e->pic_valid = false;
    e->in_pass = false;
    uint8_t c[8];
    for (int y = 0; y < RBCV_H; y++)
        for (int cx = 0; cx < 32; cx++) {
            for (int i = 0; i < 8; i++) {
                uint8_t v = px[y * RBCV_W + cx * 8 + i];
                c[i] = v ? v : BLACK;
            }
            span(e, (unsigned)((y >> 3) * 256 + cx * 8 + (y & 7)), c);
        }
    e->bands = ~0u;
    e->stats.pictures++;
    e->dirty = true;
}

static void screen_of(const uint8_t *pat, const uint8_t *colt, uint8_t *px) {
    for (int y = 0; y < RBCV_H; y++)
        for (int x = 0; x < RBCV_W; x++) {
            unsigned a = (unsigned)(((y >> 3) * 32 + (x >> 3)) * 8 + (y & 7));
            uint8_t c = colt[a];
            c = (pat[a] & (0x80 >> (x & 7))) ? c >> 4 : c & 15;
            px[y * RBCV_W + x] = c ? c : BLACK;
        }
}

void rbcv_screen(const rbcv_t *e, uint8_t *px) { screen_of(e->vram_pat, e->vram_col, px); }
void rbcv_wanted(const rbcv_t *e, uint8_t *px) { screen_of(e->want_pat, e->want_col, px); }

void rbcv_ideal(const rbcv_t *e, uint8_t *px) {
    for (int r = 0; r < RBCV_H; r++)
        for (int cx = 0; cx < 32; cx++) {
            uint8_t c[8];
            cell_colors(e->layout, r, cx, c);
            for (int i = 0; i < 8; i++) px[r * RBCV_W + cx * 8 + i] = c[i] ? c[i] : BLACK;
        }
}

// ---- what the console needs ----------------------------------------------------------
static inline bool need_pat(const rbcv_t *e, unsigned j) { return e->want_pat[j] != e->vram_pat[j]; }
static inline bool need_col(const rbcv_t *e, unsigned j) { return e->want_col[j] != e->vram_col[j]; }

// The next bytes to send, from the cursor on (Time Pilot's next_run)
RBCV_HOT static bool next_run(rbcv_t *e, bool *color, unsigned *s, int *m, unsigned most) {
    if (!e->dirty) return false;
    unsigned i = e->cursor;
    int band = -1;
    for (unsigned k = 0; k < RBCV_BYTES;) {
        if (!(i & 255)) {
            if (band >= 0) e->bands &= ~(1u << band);
            band = -1;
            if (k + 256 <= RBCV_BYTES && !(e->bands >> (i >> 8) & 1)) {
                k += 256;
                i = i + 256 == RBCV_BYTES ? 0 : i + 256;
                continue;
            }
            band = (int)(i >> 8);
        }
        if (!(i & 3) && k + 4 <= RBCV_BYTES) {
            uint32_t w, v, wc, vc;
            memcpy(&w, e->want_pat + i, 4);
            memcpy(&v, e->vram_pat + i, 4);
            memcpy(&wc, e->want_col + i, 4);
            memcpy(&vc, e->vram_col + i, 4);
            if (w == v && wc == vc) {
                k += 4;
                i = i + 4 == RBCV_BYTES ? 0 : i + 4;
                continue;
            }
        }
        bool nc = need_col(e, i);
        if (!nc && !need_pat(e, i)) {
            k++;
            i = i + 1 == RBCV_BYTES ? 0 : i + 1;
            continue;
        }
        e->cursor = (uint16_t)i;
        *color = nc;
        *s = i;
        unsigned end = i + 1, gap = 0;
        for (unsigned j = i + 1; j < RBCV_BYTES && j - i < most; j++) {
            if (nc ? need_col(e, j) : need_pat(e, j)) { end = j + 1; gap = 0; }
            else if (++gap > 3) break;
        }
        *m = (int)(end - i);
        return true;
    }
    e->dirty = false;
    return false;
}

static void put_run(rbcv_t *e, list_t *l, bool color, unsigned s, int n) {
    l_run_head(l, (uint16_t)((color ? COLORS : PATTERNS) + s), n);
    uint8_t *d = l->b + l->n;
    if (color) {
        memcpy(d, e->want_col + s, n);
        memcpy(e->vram_col + s, d, n);
        e->stats.col_bytes += (unsigned)n;
    } else {
        memcpy(d, e->want_pat + s, n);
        memcpy(e->vram_pat + s, d, n);
        e->stats.pat_bytes += (unsigned)n;
    }
    l->n += n;
    e->stats.runs++;
}

// what's on screen written again as it is (a byte the console misread is
// put right within seconds)
static void refresh_run(rbcv_t *e, list_t *l, int n) {
    unsigned at = e->refresh_at;
    bool color = at >= RBCV_BYTES;
    unsigned s = color ? at - RBCV_BYTES : at;
    if (s + n > RBCV_BYTES) n = (int)(RBCV_BYTES - s);
    l_run_head(l, (uint16_t)((color ? COLORS : PATTERNS) + s), n);
    memcpy(l->b + l->n, (color ? e->vram_col : e->vram_pat) + s, n);
    l->n += n;
    e->refresh_at = (uint16_t)((at + n) % (2 * RBCV_BYTES));
    e->stats.refreshed += (unsigned)n;
}

uint32_t rbcv_backlog(const rbcv_t *e) {
    uint32_t n = 0;
    for (unsigned j = 0; j < RBCV_BYTES; j++) n += need_pat(e, j) + need_col(e, j);
    return n;
}

// the name table: name i at place i in each third (it changes never)
static void names_run(rbcv_t *e, list_t *l, int n) {
    int left = 768 - e->names_at;
    if (n > left) n = left;
    l_run_head(l, (uint16_t)(NAMES + e->names_at), n);
    for (int k = 0; k < n; k++) l->b[l->n++] = (uint8_t)(e->names_at + k);
    e->names_at = (uint16_t)(e->names_at + n);
    if (e->names_at == 768) e->names_done = true;
}

// ---- a list, the console waiting for the vertical blank (messages) --------------------
static void plain_list(rbcv_t *e, list_t *l) {
    if (e->resync) {
        l_end_vblank(l);
        if (e->tone_n) l_psg(l, e->tone_bytes, e->tone_n);
        if (e->resync-- == 2) return;
    } else if (e->tone_n) {
        l_psg(l, e->tone_bytes, e->tone_n);
    }
    while (!e->names_done) {
        int n = room(l);
        if (n < 16 && l->vblank) { l_end_vblank(l); continue; }
        if (n < 1) break;
        names_run(e, l, n);
    }
    bool color;
    unsigned s;
    int m;
    while (e->names_done && next_run(e, &color, &s, &m, 255)) {
        int n = room(l);
        if (n < m && l->vblank) {
            if (n >= 8) put_run(e, l, color, s, n);
            l_end_vblank(l);
            continue;
        }
        if (n < 1) break;
        put_run(e, l, color, s, n < m ? n : m);
    }
    for (int k = 0; k < 2 && e->names_done; k++) {
        int n = room(l);
        if (n < 32) break;
        refresh_run(e, l, n);
    }
    l_end_vblank(l);
}

// ---- a list with samples in it (Time Pilot's pcm_list) ---------------------------------
RBCV_HOT static void fill(rbcv_t *e, list_t *l, uint32_t until) {
    int avail = (int)until - (int)l->t;
    if (avail < C_RUN_ACT + C_BYTE_ACT) return;
    int n = (avail - C_RUN_ACT) / C_BYTE_ACT;
    if (n > 255) n = 255;
    if (!e->names_done) { names_run(e, l, n); return; }
    bool color;
    unsigned s;
    int m;
    if (!next_run(e, &color, &s, &m, 1)) {
        if (e->refresh_at % RBCV_BYTES + (unsigned)n > RBCV_BYTES) e->refresh_at = (uint16_t)(e->refresh_at / RBCV_BYTES * RBCV_BYTES);
        refresh_run(e, l, n);
        return;
    }
    if (s + (unsigned)n > RBCV_BYTES) s = RBCV_BYTES - (unsigned)n;
    put_run(e, l, color, s, n);
}

RBCV_HOT static void pcm_list(rbcv_t *e, list_t *l) {
    l->t = C_FRAME;
    l_end_vblank(l);
    unsigned tone_at = 0;
    for (int k = 0; k < RBCV_PCM_N; k++) {
        uint8_t b[10];
        int n = 0;
        for (int j = 0; j < 3; j++) b[n++] = e->pcm_bytes[k][j];
        while (tone_at < e->tone_n && n < 5) {
            unsigned g = 1;
            while (tone_at + g < e->tone_n && !(e->tone_bytes[tone_at + g] & 0x80)) g++;
            if (n + (int)g > 6) break;
            memcpy(b + n, e->tone_bytes + tone_at, g);
            n += (int)g;
            tone_at += g;
        }
        if (k + 1 == RBCV_PCM_N) {
            l_psg(l, b, n);
            break;
        }
        uint32_t until = PCM_FIRST + (((uint32_t)(k + 1) * PCM_STEP) >> 8);
        int avail = (int)until - (int)l->t - (C_PSG + C_PSG_BYTE * n);
        // a short run of the picture's as one of two
        bool color;
        unsigned s;
        int m = 0;
        bool two = e->names_done && next_run(e, &color, &s, &m, 7) && m <= 2 &&
                   avail >= 2 * C_RUN_ACT + C_BYTE_ACT * (m + 2);
        int need = two ? m + 1 : 1;
        int base = avail - (two ? 2 : 1) * C_RUN_ACT, best = 0, best_left = 99;
        for (int r = 0; r < 3 && base - C_PSG_BYTE * r >= C_BYTE_ACT * need; r++) {
            int left = (base - C_PSG_BYTE * r) % C_BYTE_ACT;
            if (left < best_left) { best_left = left; best = r; }
        }
        while (best--) b[n++] = e->pcm_bytes[k][2];     // (the last attenuator again: no change)
        l_psg(l, b, n);
        if (two) put_run(e, l, color, s, m);
        fill(e, l, until);
    }
    e->stats.pcm_frames++;
    e->resync = 2;
}

size_t rbcv_tv_frame(rbcv_t *e, uint8_t *out) {
    list_t l = {out, 0, C_START, true};
    if (e->pcm) pcm_list(e, &l);
    else plain_list(e, &l);
    l.t += C_END;
    l.b[l.n++] = 0;
    e->tone_n = 0;
    e->stats.tv_frames++;
    if (l.t > e->stats.t_max) e->stats.t_max = l.t;
    if (!e->dirty) e->stats.settled++;
    return l.n;
}

// ---- the sound: samples through the DAC ------------------------------------------------
// The DAC: the three tones at a period of 1, their attenuators as measured
// on a console (tools/ladder.json), summed. rbdac3.h's 512 levels go from 0
// to 2.40, each a triple of attenuators chosen so a small change makes
// small partial sums between a sample's three writes (tools/mkdac3.py). The
// sound rests at the middle (DAC_MID). rbsnd.c hands over samples already
// scaled to the DAC's levels either side of the middle (+-32767 for
// +-DAC_HALF); a soft knee from 3/4 of the way rounds the loudest off,
// never clipping.
#define DAC_LEVELS RBDAC3_LEVELS
#define DAC_MID    255
#define DAC_HALF   255.0f
#define DAC_KNEE   191.0f
static const float ladder[3][16] = {
    {1.0f, 0.7914f, 0.6320f, 0.5084f, 0.4019f, 0.3161f, 0.2459f, 0.1912f, 0.1396f, 0.1080f, 0.0844f, 0.0672f, 0.0524f, 0.0421f, 0.0310f, 0.0f},
    {1.0f, 0.7816f, 0.6125f, 0.4850f, 0.3925f, 0.3109f, 0.2416f, 0.1872f, 0.1373f, 0.1058f, 0.0828f, 0.0653f, 0.0533f, 0.0412f, 0.0306f, 0.0f},
    {1.0f, 0.7979f, 0.6229f, 0.4921f, 0.3942f, 0.3004f, 0.2348f, 0.1874f, 0.1419f, 0.1106f, 0.0846f, 0.0677f, 0.0534f, 0.0420f, 0.0328f, 0.0f}};
static uint8_t limit[512];                  // |level| (0-511) to 0-255: straight to DAC_KNEE, then soft
static int16_t lq[3][16];                   // ladder in 1/16384ths, for put_sample's integer sums
static bool dac_ready;

float rbcv_dac_level(int a0, int a1, int a2) { return ladder[0][a0 & 15] + ladder[1][a1 & 15] + ladder[2][a2 & 15]; }

static void dac_init(void) {
    for (int i = 0; i < 512; i++) {
        float v = (float)i;
        if (v > DAC_KNEE) v = DAC_KNEE + (DAC_HALF - DAC_KNEE) * (1.0f - expf(-(v - DAC_KNEE) / (DAC_HALF - DAC_KNEE)));
        limit[i] = (uint8_t)(v + 0.5f);
    }
    for (int c = 0; c < 3; c++)
        for (int a = 0; a < 16; a++) lq[c][a] = (int16_t)lrintf(ladder[c][a] * 16384.0f);
    dac_ready = true;
}

// a sample's three writes, in the order whose two partial sums (the chip
// between the writes, 52 T each) are nearest the new level, in integers
static const uint8_t order[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
static inline int32_t iabs(int32_t x) { return x < 0 ? -x : x; }
static inline void put_sample(uint8_t *out, int *a0, int *a1, int *a2, uint16_t t) {
    const int w0 = t >> 8, w1 = t >> 4 & 15, w2 = t & 15;
    const int32_t d0 = lq[0][w0] - lq[0][*a0], d1 = lq[1][w1] - lq[1][*a1], d2 = lq[2][w2] - lq[2][*a2];
    const int32_t sum = d0 + d1 + d2;
    const int32_t f0 = iabs(sum - d0), f1 = iabs(sum - d1), f2 = iabs(sum - d2);
    const int32_t l0 = iabs(d0), l1 = iabs(d1), l2 = iabs(d2);
    int32_t best = f0 + l2, v;
    int pick = 0;
    if ((v = f0 + l1) < best) { best = v; pick = 1; }
    if ((v = f1 + l2) < best) { best = v; pick = 2; }
    if ((v = f1 + l0) < best) { best = v; pick = 3; }
    if ((v = f2 + l1) < best) { best = v; pick = 4; }
    if ((v = f2 + l0) < best) pick = 5;
    const uint32_t b = (uint32_t)(0x90 | w0) | (uint32_t)(0xB0 | w1) << 8 | (uint32_t)(0xD0 | w2) << 16;
    out[0] = (uint8_t)(b >> 8 * order[pick][0]);
    out[1] = (uint8_t)(b >> 8 * order[pick][1]);
    out[2] = (uint8_t)(b >> 8 * order[pick][2]);
    *a0 = w0; *a1 = w1; *a2 = w2;
}

RBCV_HOT void rbcv_sound_pcm(rbcv_t *e, const int16_t *s, int n) {
    if (!dac_ready) dac_init();
    e->tone_n = 0;
    if (n <= 0) {                       // silence: the whole chip, every list (a misread byte lasts a list)
        static const uint8_t off[] = {0x80, 0x00, 0x9F, 0xA0, 0x00, 0xBF, 0xC0, 0x00, 0xDF, 0xFF};
        memcpy(e->tone_bytes, off, sizeof off);
        e->tone_n = sizeof off;
        e->pcm = false;
        return;
    }
    if (!e->pcm) {
        e->pcm = true;
        e->pcm_gain = 0;
        e->dac_att[0] = e->dac_att[1] = e->dac_att[2] = 15;     // (as the lists before left them)
    }
    // Every list: the three tones parked at a period of 1, the noise silent,
    // so every register is written each list (a byte the console misreads
    // lasts a list at most)
    static const uint8_t park[] = {0x81, 0x00, 0xA1, 0x00, 0xC1, 0x00, 0xE4, 0xFF};
    memcpy(e->tone_bytes, park, sizeof park);
    e->tone_n = sizeof park;
    int32_t gain = e->pcm_gain;
    int a0 = e->dac_att[0], a1 = e->dac_att[1], a2 = e->dac_att[2];
    uint32_t clipped = 0;
    for (int k = 0; k < RBCV_PCM_N; k++) {
        int32_t v = s[k < n ? k : n - 1] >> 6;          // +-32767 to +-511 levels
        int32_t a = v < 0 ? -v : v;
        if (a > (int32_t)DAC_KNEE) clipped++;
        a = limit[a > 511 ? 511 : a];
        int32_t lvl = DAC_MID + (v < 0 ? -a : a);
        if (gain < 32 && !(k & 7)) gain++;              // (up to its rest over 43 ms)
        lvl = lvl * gain / 32;
        put_sample(e->pcm_bytes[k], &a0, &a1, &a2, rb_dac3[lvl < 0 ? 0 : lvl > DAC_LEVELS - 1 ? DAC_LEVELS - 1 : lvl]);
    }
    e->pcm_gain = (uint8_t)gain;
    e->dac_att[0] = (uint8_t)a0;
    e->dac_att[1] = (uint8_t)a1;
    e->dac_att[2] = (uint8_t)a2;
    e->stats.clipped += clipped;
}

void rbcv_init(rbcv_t *e) {
    memset(e, 0, sizeof *e);
    e->dirty = true;
    e->bands = (1u << (RBCV_BYTES / 256)) - 1;
    e->layout = RBCV_FULL;
    if (!colors_ready) colors_init();
    if (!dac_ready) dac_init();
    if (!maps_ready) maps_init();
    pic_clear = false;
}
