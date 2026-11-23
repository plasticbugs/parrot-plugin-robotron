// rb.c -- see rb.h. Written for this plugin from MAME 0.288's williams.cpp,
// williams_m.cpp, williams_v.cpp, williamsblitter.cpp and 6821pia.cpp
// (BSD-3-Clause; Aaron Giles and the MAME team): the memory map, the
// blitter's every rule and its stall, the PIAs as MAME has them, the
// interrupts from the scanline counter, the video counter at $CB00. The
// sound CPU's instructions, and its clock, follow MAME's m6800
// (BSD-3-Clause; Aaron Giles). test/rb/trace.sh compares each CPU with
// MAME's instruction by instruction.
#include "rb.h"

#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define RB_HOT __attribute__((section(".time_critical.rb")))
#else
#define RB_HOT
#endif

rb_t rb;
rb_in_t rb_in;
rb_frame_t rb_frame;
uint32_t rb_dac[RB_DAC_RING];
uint32_t rb_dac_head;
void (*rb_on_frame)(void);
void (*rb_trace)(const m6809_t *c);
void (*rb_strace)(void);

static rb_roms_t roms;

const uint8_t m6809_nz[256] = {
    CC_Z, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
#define N8 CC_N, CC_N, CC_N, CC_N, CC_N, CC_N, CC_N, CC_N
    N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8,
#undef N8
};

uint64_t rb_time(void) { return m6809_now(&rb.cpu); }

// ---- the 6821s (MAME's pia6821_device, the parts this board wires up) -------------------
// PIA 0: port A IN0, port B IN1. PIA 1: port A IN2, port B the sound
// command, CA1 the count of 240, CB1 the scanline's bit 5, both IRQs the
// main CPU's. PIA 2 (the sound board): port A the DAC, port B the command,
// CB1 "a command", both IRQs the sound CPU's.
static void sound_cmd(uint8_t v);
static void dac_write(uint8_t v);

static void pia_irqs(int n) {
    rb_pia_t *p = &rb.pia[n];
    p->irq_a = p->irq_a1 && (p->ctl_a & 1);
    p->irq_b = p->irq_b1 && (p->ctl_b & 1);
    uint8_t line = p->irq_a | p->irq_b;
    if (n == 1) {
        if (line && !rb.cpu.irq) m6809_stop(&rb.cpu);      // (seen at the next instruction)
        rb.cpu.irq = line;
    } else if (n == 2) {
        rb.snd.irq = line;
    }
}

static void pia_ca1(int n, uint8_t s) {
    rb_pia_t *p = &rb.pia[n];
    if (p->ca1 != s && (s ? (p->ctl_a & 2) : !(p->ctl_a & 2))) {
        p->irq_a1 = 1;
        pia_irqs(n);
    }
    p->ca1 = s;
}

static void pia_cb1(int n, uint8_t s) {
    rb_pia_t *p = &rb.pia[n];
    if (p->cb1 != s && (s ? (p->ctl_b & 2) : !(p->ctl_b & 2))) {
        p->irq_b1 = 1;
        pia_irqs(n);
    }
    p->cb1 = s;
}

static uint8_t pia_in_a(int n) { return n == 0 ? rb_in.in0 : n == 1 ? rb_in.in2 : 0xFF; }

static uint8_t pia_read(int n, unsigned r) {
    rb_pia_t *p = &rb.pia[n];
    uint8_t v;
    switch (r & 3) {
    case 0:
        if (!(p->ctl_a & 4)) return p->ddr_a;
        v = (uint8_t)((~p->ddr_a & pia_in_a(n)) | (p->ddr_a & p->out_a));
        p->irq_a1 = 0;
        pia_irqs(n);
        return v;
    case 1:
        return (uint8_t)(p->ctl_a | (p->irq_a1 ? 0x80 : 0));
    case 2:
        if (!(p->ctl_b & 4)) return p->ddr_b;
        if (p->ddr_b == 0xFF) v = p->out_b;
        else {
            uint8_t in = n == 0 ? rb_in.in1 : n == 2 ? p->in_b : 0;
            v = (uint8_t)((p->out_b & p->ddr_b) | (in & ~p->ddr_b));
        }
        p->irq_b1 = 0;
        pia_irqs(n);
        return v;
    default:
        return (uint8_t)(p->ctl_b | (p->irq_b1 ? 0x80 : 0));
    }
}

static void pia_out_a(int n) {
    rb_pia_t *p = &rb.pia[n];
    if (n != 2) return;
    uint8_t v = p->ddr_a == 0xFF ? p->out_a : (uint8_t)((p->out_a & p->ddr_a) | (0xFF & ~p->ddr_a));
    dac_write(v);
}

static void pia_out_b(int n) {
    rb_pia_t *p = &rb.pia[n];
    if (n == 1) sound_cmd((uint8_t)(p->out_b & p->ddr_b));
}

static void pia_write(int n, unsigned r, uint8_t v) {
    rb_pia_t *p = &rb.pia[n];
    switch (r & 3) {
    case 0:
        if (p->ctl_a & 4) { p->out_a = v; pia_out_a(n); }
        else if (p->ddr_a != v) { p->ddr_a = v; pia_out_a(n); }
        break;
    case 1:
        p->ctl_a = v & 0x3F;
        pia_irqs(n);
        break;
    case 2:
        if (p->ctl_b & 4) { p->out_b = v; pia_out_b(n); }
        else if (p->ddr_b != v) { p->ddr_b = v; pia_out_b(n); }
        break;
    default:
        p->ctl_b = v & 0x3F;
        pia_irqs(n);
        break;
    }
}

static void pia_reset(int n) {
    rb_pia_t *p = &rb.pia[n];
    uint8_t ca1 = n == 2 ? 1 : p->ca1, cb1 = p->cb1, in_b = p->in_b;
    memset(p, 0, sizeof *p);
    p->ca1 = ca1;
    p->cb1 = cb1;
    p->in_b = in_b;
    pia_irqs(n);
}

// ---- the main CPU's memory -------------------------------------------------------------
// Each 256-byte page read through rd_base (base + address = the byte; 0: the
// I/O, $C000-$CBFF). $0000-$8FFF read the ROMs while the bank is selected;
// writes always go to the RAM.
static uintptr_t rd_base[256];
static uint8_t zeros[0x110];

static void map_bank(void) {
    uintptr_t b = rb.bank ? (uintptr_t)roms.lo : (uintptr_t)rb.ram;
    for (int p = 0; p < 0x90; p++) rd_base[p] = b;
}

static void map_pages(void) {
    map_bank();
    for (int p = 0x90; p < 0xC0; p++) rd_base[p] = (uintptr_t)rb.ram;
    for (int p = 0xC0; p < 0xCC; p++) rd_base[p] = 0;
    for (int p = 0xCC; p < 0xD0; p++) rd_base[p] = (uintptr_t)rb.cmos - 0xCC00;
    for (int p = 0xD0; p < 0x100; p++) rd_base[p] = (uintptr_t)roms.hi - 0xD000;
}

// the beam: the scanline boundaries up to (not at) t
static void catch_up(uint64_t t);
static void blit(uint8_t ctl);

static uint8_t RB_HOT io_rd(uint16_t a) {
    switch (a >> 8) {
    case 0xC8:
        if ((a & 0x0C) == 0x04) return pia_read(0, a);
        if ((a & 0x0C) == 0x0C) {
            catch_up(m6809_now(&rb.cpu) + 5);
            return pia_read(1, a);
        }
        return 0;
    case 0xCB: {
        // the video counter: the line at the read (the instruction's 5th
        // cycle: the game reads it with LDA, LDB and SUBB extended)
        uint64_t t = m6809_now(&rb.cpu) + 5;
        unsigned v = (unsigned)((RB_LINES - 13 + t / RB_LINE) % RB_LINES);
        return v < 0x100 ? (uint8_t)(v & 0xFC) : 0xFC;
    }
    default:
        return 0;
    }
}

static void RB_HOT io_wr(uint16_t a, uint8_t v) {
    switch (a >> 8) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3:
        rb.pal[a & 15] = v;
        break;
    case 0xC8:
        if ((a & 0x0C) == 0x04) pia_write(0, a, v);
        else if ((a & 0x0C) == 0x0C) {
            catch_up(m6809_now(&rb.cpu) + 5);
            pia_write(1, a, v);
        }
        break;
    case 0xC9:
        rb.cocktail = (v >> 1) & 1;
        if ((v & 1) != rb.bank) {
            rb.bank = v & 1;
            map_bank();
        }
        break;
    case 0xCA:
        switch (a & 7) {
        case 0: blit(v); break;
        case 1: rb.bl_solid = v; break;
        case 2: rb.bl_src = (uint16_t)((rb.bl_src & 0x00FF) | v << 8); break;
        case 3: rb.bl_src = (uint16_t)((rb.bl_src & 0xFF00) | v); break;
        case 4: rb.bl_dst = (uint16_t)((rb.bl_dst & 0x00FF) | v << 8); break;
        case 5: rb.bl_dst = (uint16_t)((rb.bl_dst & 0xFF00) | v); break;
        case 6: rb.bl_w = v; break;
        default: rb.bl_h = v; break;
        }
        break;
    case 0xCC: case 0xCD: case 0xCE: case 0xCF:
        rb.cmos[a & 0x3FF] = v | 0xF0;
        break;
    default:                            // ($CBFF: the watchdog; the ROMs)
        break;
    }
}

static inline __attribute__((always_inline)) uint8_t main_rd(uint16_t a) {
    uintptr_t p = rd_base[a >> 8];
    return p ? *(const uint8_t *)(p + a) : io_rd(a);
}
static inline __attribute__((always_inline)) void main_wr(uint16_t a, uint8_t v) {
    if (a < 0xC000) rb.ram[a] = v;
    else io_wr(a, v);
}

// ---- the blitter (MAME's williams_blitter_sc1_device) ----------------------------------
// The whole blit at the write to $CA00, then the CPU held for its time:
// (4 + 2 * (accesses + 3)) / 4 cycles, slow blits twice that. The sizes are
// XORed with 4 (the SC1's bug). Reads go through the CPU's map (the bank),
// writes to the RAM.
static void RB_HOT blit(uint8_t ctl) {
    catch_up(m6809_now(&rb.cpu) + 5);   // (the beam has drawn the lines before the write)
    const bool no_even = ctl & 0x80, no_odd = ctl & 0x40, shift = ctl & 0x20;
    const bool solid = ctl & 0x10, fg_only = ctl & 0x08;
    const bool dst256 = ctl & 2, src256 = ctl & 1;
    int w = rb.bl_w ^ 4, h = rb.bl_h ^ 4;
    if (!w) w = 1;
    if (!h) h = 1;
    const int sxadv = src256 ? 0x100 : 1, syadv = src256 ? 1 : w;
    const int dxadv = dst256 ? 0x100 : 1, dyadv = dst256 ? 1 : w;
    // the parts of the destination byte kept, for each source byte's two pixels
    // being 0 (bit 1: the even one, bit 0: the odd one) when fg_only
    uint8_t keep[4];
    for (int z = 0; z < 4; z++) {
        uint8_t k = 0xFF;
        bool ze = fg_only && (z & 2), zo = fg_only && (z & 1);
        if (ze) { if (no_even) k &= 0x0F; }
        else if (!no_even) k &= 0x0F;
        if (zo) { if (no_odd) k &= 0xF0; }
        else if (!no_odd) k &= 0xF0;
        keep[z] = k;
    }
    uint16_t sstart = rb.bl_src, dstart = rb.bl_dst;
    unsigned pixdata = 0;
    for (int y = 0; y < h; y++) {
        uint16_t s = sstart, d = dstart;
        for (int x = 0; x < w; x++) {
            uint8_t raw = main_rd(s), src;
            if (shift) {
                pixdata = pixdata << 8 | raw;
                src = (uint8_t)(pixdata >> 4);
            } else {
                src = raw;
            }
            uint8_t k = keep[(src & 0xF0 ? 0 : 2) | (src & 0x0F ? 0 : 1)];
            uint8_t cur = d < 0xC000 ? rb.ram[d] : main_rd(d);
            cur = (uint8_t)((cur & k) | ((solid ? rb.bl_solid : src) & ~k));
            main_wr(d, cur);
            s = (uint16_t)(s + sxadv);
            d = (uint16_t)(d + dxadv);
        }
        if (dst256) dstart = (uint16_t)((dstart & 0xFF00) | ((dstart + dyadv) & 0xFF));
        else dstart = (uint16_t)(dstart + dyadv);
        if (src256) sstart = (uint16_t)((sstart & 0xFF00) | ((sstart + syadv) & 0xFF));
        else sstart = (uint16_t)(sstart + syadv);
    }
    int accesses = 2 * w * h;
    int est = 4 + ((ctl & 4) ? 4 * (accesses + 2) : 2 * (accesses + 3));
    rb.cpu.icount -= (est + 3) / 4;
}

// ---- the main CPU ----------------------------------------------------------------------
// An instruction's bytes come through its first byte's page; one that could
// run on into a page of other memory is read a byte at a time into
// fetch_tmp first (Star Wars' way).
static uint8_t fetch_tmp[8];
static uintptr_t __attribute__((noinline)) main_fetch_edge(uint16_t pc) {
    for (unsigned i = 0; i < sizeof fetch_tmp; i++) fetch_tmp[i] = main_rd((uint16_t)(pc + i));
    return (uintptr_t)fetch_tmp - pc;
}
static inline __attribute__((always_inline)) uintptr_t main_fetch_base(uint16_t pc) {
    uintptr_t p = rd_base[pc >> 8];
    if (__builtin_expect((pc & 0xFF) > 0xFA || !p, 0)) {
        if (pc < 0xFFF0 && rd_base[(uint16_t)(pc + 5) >> 8] != p) return main_fetch_edge(pc);
        if (!p) return (uintptr_t)zeros - (pc & 0xFF00);
    }
    return p;
}

#ifdef RB_TRACE
#define M6809_TRACE(c) do { if (rb_trace) rb_trace(c); } while (0)
#endif
#define M6809_HOT RB_HOT
#define M6809_NAME  maincpu
#define M6809_FETCH_BASE(pc) main_fetch_base(pc)
#define M6809_FETCH(a) (*(const uint8_t *)(fbase + (uint16_t)(a)))
#define M6809_RD(a) main_rd(a)
#define M6809_WR(a, v) main_wr(a, v)
#include "m6809_core.h"

// ---- the sound board: a 6808, its RAM, the PIA, the ROM ----------------------------------
static inline uint8_t snd_rd(uint16_t a) {
    if (a < 0x100) return rb.snd_ram[a];
    if (a >= 0xF000) return roms.snd[a - 0xF000];
    if ((a & 0x7FFC) == 0x0400) return pia_read(2, a);
    return 0;
}
static inline void snd_wr(uint16_t a, uint8_t v) {
    if (a < 0x100) rb.snd_ram[a] = v;
    else if ((a & 0x7FFC) == 0x0400) pia_write(2, a, v);
}

static void dac_write(uint8_t v) {
    rb.dac = v;
    rb_dac[rb_dac_head++ & (RB_DAC_RING - 1)] = (uint32_t)rb.snd_now << 8 | v;
}

// MAME's cycles_6800 (an undefined opcode: 4)
static const uint8_t snd_cycles[256] = {
    4, 2, 4, 4, 4, 4, 2, 2, 4, 4, 2, 2, 2, 2, 2, 2,  2, 2, 4, 4, 4, 4, 2, 2, 4, 2, 4, 2, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 4, 10, 4, 4, 9, 12,
    2, 4, 4, 2, 2, 4, 2, 2, 2, 2, 2, 4, 2, 2, 4, 2,  2, 4, 4, 2, 2, 4, 2, 2, 2, 2, 2, 4, 2, 2, 4, 2,
    7, 4, 4, 7, 7, 4, 7, 7, 7, 7, 7, 4, 7, 7, 4, 7,  6, 4, 4, 6, 6, 4, 6, 6, 6, 6, 6, 4, 6, 6, 3, 6,
    2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 2, 3, 8, 3, 4,  3, 3, 3, 4, 3, 3, 3, 4, 3, 3, 3, 3, 4, 6, 4, 5,
    5, 5, 5, 4, 5, 5, 5, 6, 5, 5, 5, 5, 6, 8, 6, 7,  4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 5, 9, 5, 6,
    2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 2, 4, 4, 3, 4,  3, 3, 3, 4, 3, 3, 3, 4, 3, 3, 3, 3, 4, 4, 4, 5,
    5, 5, 5, 4, 5, 5, 5, 6, 5, 5, 5, 5, 4, 4, 6, 7,  4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 5, 6};

enum { S_C = 1, S_V = 2, S_Z = 4, S_N = 8, S_I = 0x10, S_H = 0x20 };
#define SCC rb.snd.cc
static inline void s_nz8(uint8_t r) { SCC = (uint8_t)((SCC & ~(S_N | S_Z)) | (r & 0x80 ? S_N : 0) | (r ? 0 : S_Z)); }
static inline void s_nz16(uint16_t r) { SCC = (uint8_t)((SCC & ~(S_N | S_Z)) | (r & 0x8000 ? S_N : 0) | (r ? 0 : S_Z)); }
// MAME's SET_FLAGS8: N Z from r, V from a ^ b ^ r ^ (r >> 1), C from bit 8
static inline void s_flags8(unsigned a, unsigned b, unsigned r) {
    SCC = (uint8_t)((SCC & ~(S_N | S_Z | S_V | S_C)) | (r & 0x80 ? S_N : 0) | (r & 0xFF ? 0 : S_Z) |
                    (((a ^ b ^ r ^ (r >> 1)) & 0x80) >> 6) | ((r >> 8) & 1));
}
static inline uint8_t s_sub(uint8_t a, uint8_t b, unsigned c) {
    unsigned r = (unsigned)(a - b - c) & 0xFFFF;
    s_flags8(a, b, r);
    return (uint8_t)r;
}
static inline uint8_t s_add(uint8_t a, uint8_t b, unsigned c) {
    unsigned r = a + b + c;
    s_flags8(a, b, r);
    SCC = (uint8_t)((SCC & ~S_H) | (((a ^ b ^ r) & 0x10) << 1));
    return (uint8_t)r;
}
static inline void s_logic(uint8_t r) { SCC &= (uint8_t)~S_V; s_nz8(r); }
static inline void s_push(uint8_t v) { snd_wr(rb.snd.s--, v); }
static inline uint8_t s_pull(void) { return snd_rd(++rb.snd.s); }
static inline void s_push16(uint16_t v) { s_push((uint8_t)v); s_push((uint8_t)(v >> 8)); }
static inline uint16_t s_pull16(void) { uint16_t h = s_pull(); return (uint16_t)(h << 8 | s_pull()); }
static inline uint16_t s_rd16(uint16_t a) { return (uint16_t)(snd_rd(a) << 8 | snd_rd((uint16_t)(a + 1))); }

// the read-modify-write group ($40-$7F) by its low nibble; -1: not one
static inline int s_rmw(unsigned op, uint8_t t) {
    unsigned r;
    switch (op & 15) {
    case 0x0: r = (0u - t) & 0xFFFF; s_flags8(0, t, r); return (uint8_t)r;                    // NEG
    case 0x3: r = (uint8_t)~t; s_logic((uint8_t)r); SCC |= S_C; return (int)r;                 // COM
    case 0x4: SCC = (uint8_t)((SCC & ~(S_N | S_Z | S_V | S_C)) | (t & 1)); r = t >> 1;      // LSR
        if (!r) SCC |= S_Z;
        if (((SCC >> 3) ^ SCC) & 1) SCC |= S_V;
        return (int)r;
    case 0x6: r = (unsigned)(SCC & S_C) << 7 | t >> 1;                                       // ROR
        SCC = (uint8_t)((SCC & ~(S_N | S_Z | S_V | S_C)) | (t & 1)); s_nz8((uint8_t)r);
        if (((SCC >> 3) ^ SCC) & 1) SCC |= S_V;
        return (int)r;
    case 0x7: r = (unsigned)(t >> 1) | (t & 0x80);                                          // ASR
        SCC = (uint8_t)((SCC & ~(S_N | S_Z | S_V | S_C)) | (t & 1)); s_nz8((uint8_t)r);
        if (((SCC >> 3) ^ SCC) & 1) SCC |= S_V;
        return (int)r;
    case 0x8: r = (unsigned)t << 1; s_flags8(t, t, r); return (uint8_t)r;                     // ASL
    case 0x9: r = (unsigned)t << 1 | (SCC & S_C); s_flags8(t, t, r); return (uint8_t)r;       // ROL
    case 0xA: r = (uint8_t)(t - 1); SCC &= (uint8_t)~S_V; s_nz8((uint8_t)r);                  // DEC
        if (r == 0x7F) SCC |= S_V;
        return (int)r;
    case 0xC: r = (uint8_t)(t + 1); SCC &= (uint8_t)~S_V; s_nz8((uint8_t)r);                  // INC
        if (r == 0x80) SCC |= S_V;
        return (int)r;
    case 0xD: SCC &= (uint8_t)~(S_V | S_C); s_nz8(t); return -2;                              // TST
    case 0xF: SCC = (uint8_t)((SCC & ~(S_N | S_V | S_C)) | S_Z); return 0;                     // CLR
    default: return -1;
    }
}

static int snd_insn(void);

// one instruction (or an interrupt's entry, or a WAI's wait); its cycles
static int RB_HOT snd_step(void) {
    if (rb.snd.irq && !(SCC & S_I)) {
        int n;
        if (rb.snd.wai) { rb.snd.wai = 0; n = 4; }
        else {
            s_push16(rb.snd.pc); s_push16(rb.snd.x); s_push(rb.snd.a); s_push(rb.snd.b); s_push(SCC);
            n = 12;
        }
        SCC |= S_I;
        rb.snd.pc = s_rd16(0xFFF8);
        return n;
    }
    if (rb.snd.wai) return 1;
    return snd_insn();
}

// One instruction; its cycles. TAP, and CLI with the I flag set, run the
// next instruction too before an interrupt can come, as MAME's do: inside
// their own, at their own start time (their cycles are counted after it).
static int RB_HOT snd_insn(void) {
#ifdef RB_TRACE
    if (rb_strace) rb_strace();
#endif
    bool nest = false;
    uint16_t pc = rb.snd.pc;
    unsigned op = snd_rd(pc++);
    int cyc = snd_cycles[op];
    uint8_t *acc = op & 0x40 ? &rb.snd.b : &rb.snd.a;
    uint16_t ea = 0, w;
    uint8_t t;
    if (op >= 0x80) {
        // the ALU group: A ($80-$BF) or B ($C0-$FF), four addressing modes
        unsigned mode = op >> 4 & 3, lo = op & 15;
        switch (mode) {
        case 0: ea = pc; pc = (uint16_t)(pc + ((lo >= 0xC && lo != 0xD) || op == 0xCD ? 2 : 1)); break;   // (immediate)
        case 1: ea = snd_rd(pc++); break;
        case 2: ea = (uint16_t)(rb.snd.x + snd_rd(pc++)); break;
        default: ea = (uint16_t)(snd_rd(pc) << 8 | snd_rd((uint16_t)(pc + 1))); pc += 2; break;
        }
        switch (lo) {
        case 0x0: *acc = s_sub(*acc, snd_rd(ea), 0); break;                         // SUB
        case 0x1: (void)s_sub(*acc, snd_rd(ea), 0); break;                          // CMP
        case 0x2: *acc = s_sub(*acc, snd_rd(ea), SCC & S_C); break;                 // SBC
        case 0x4: *acc &= snd_rd(ea); s_logic(*acc); break;                         // AND
        case 0x5: s_logic(*acc & snd_rd(ea)); break;                                // BIT
        case 0x6: *acc = snd_rd(ea); s_logic(*acc); break;                          // LDA
        case 0x7: s_logic(*acc); snd_wr(ea, *acc); break;                           // STA (immediate: the next byte)
        case 0x8: *acc ^= snd_rd(ea); s_logic(*acc); break;                         // EOR
        case 0x9: *acc = s_add(*acc, snd_rd(ea), SCC & S_C); break;                 // ADC
        case 0xA: *acc |= snd_rd(ea); s_logic(*acc); break;                         // ORA
        case 0xB: *acc = s_add(*acc, snd_rd(ea), 0); break;                         // ADD
        case 0xC:
            if (op & 0x40) break;                                                   // (undefined)
            {                                                                       // CPX (the 6800's flags)
                w = s_rd16(ea);
                unsigned hd = ((unsigned)(rb.snd.x >> 8) - (w >> 8)) & 0xFFFF;
                SCC = (uint8_t)(SCC & ~(S_N | S_Z | S_V));
                SCC |= (uint8_t)((hd & 0x80) >> 4);
                SCC |= (uint8_t)((((rb.snd.x >> 8) ^ (w >> 8) ^ hd ^ (hd >> 1)) & 0x80) >> 6);
                if ((uint16_t)(rb.snd.x - w) == 0) SCC |= S_Z;
            }
            break;
        case 0xD:
            if (op & 0x40) break;
            if (mode == 0) {                                                        // BSR
                int8_t o = (int8_t)snd_rd(ea);
                s_push16(pc);
                pc = (uint16_t)(pc + o);
            } else {                                                                // JSR
                s_push16(pc);
                pc = ea;
            }
            break;
        case 0xE:                                                                   // LDS / LDX
            w = s_rd16(ea);
            if (op & 0x40) rb.snd.x = w; else rb.snd.s = w;
            SCC &= (uint8_t)~S_V;
            s_nz16(w);
            break;
        case 0xF:                                                                   // STS / STX
            w = op & 0x40 ? rb.snd.x : rb.snd.s;
            SCC &= (uint8_t)~S_V;
            s_nz16(w);
            snd_wr(ea, (uint8_t)(w >> 8));
            snd_wr((uint16_t)(ea + 1), (uint8_t)w);
            break;
        default: break;                                                             // ($x3: undefined)
        }
        rb.snd.pc = pc;
        return cyc;
    }
    if (op >= 0x40) {
        int r;
        if (op < 0x60) {                                                            // on A ($4x) or B ($5x)
            acc = op & 0x10 ? &rb.snd.b : &rb.snd.a;
            r = s_rmw(op, *acc);
            if (r >= 0) *acc = (uint8_t)r;
        } else {
            if (op < 0x70) ea = (uint16_t)(rb.snd.x + snd_rd(pc++));
            else { ea = (uint16_t)(snd_rd(pc) << 8 | snd_rd((uint16_t)(pc + 1))); pc += 2; }
            if ((op & 15) == 0xE) pc = ea;                                         // JMP
            else if ((op & 15) == 0xF) { (void)snd_rd(ea); snd_wr(ea, 0); (void)s_rmw(op, 0); }   // CLR
            else {
                t = snd_rd(ea);
                r = s_rmw(op, t);
                if (r >= 0) snd_wr(ea, (uint8_t)r);
            }
        }
        rb.snd.pc = pc;
        return cyc;
    }
    if (op >= 0x20 && op < 0x30) {                                                  // branches
        int8_t o = (int8_t)snd_rd(pc++);
        uint8_t cc = SCC;
        bool n = cc & S_N, v = cc & S_V, z = cc & S_Z, c = cc & S_C, take;
        switch (op & 15) {
        case 0x0: take = true; break;
        case 0x1: take = false; break;
        case 0x2: take = !(c || z); break;
        case 0x3: take = c || z; break;
        case 0x4: take = !c; break;
        case 0x5: take = c; break;
        case 0x6: take = !z; break;
        case 0x7: take = z; break;
        case 0x8: take = !v; break;
        case 0x9: take = v; break;
        case 0xA: take = !n; break;
        case 0xB: take = n; break;
        case 0xC: take = n == v; break;
        case 0xD: take = n != v; break;
        case 0xE: take = !(n != v || z); break;
        default: take = n != v || z; break;
        }
        if (take) pc = (uint16_t)(pc + o);
        rb.snd.pc = pc;
        return cyc;
    }
    unsigned r;
    switch (op) {
    case 0x06: SCC = rb.snd.a; nest = true; break;                                  // TAP
    case 0x07: rb.snd.a = SCC; break;                                               // TPA
    case 0x08: rb.snd.x++; SCC = (uint8_t)((SCC & ~S_Z) | (rb.snd.x ? 0 : S_Z)); break;    // INX
    case 0x09: rb.snd.x--; SCC = (uint8_t)((SCC & ~S_Z) | (rb.snd.x ? 0 : S_Z)); break;    // DEX
    case 0x0A: SCC &= (uint8_t)~S_V; break;
    case 0x0B: SCC |= S_V; break;
    case 0x0C: SCC &= (uint8_t)~S_C; break;
    case 0x0D: SCC |= S_C; break;
    case 0x0E: nest = SCC & S_I; SCC &= (uint8_t)~S_I; break;                      // CLI
    case 0x0F: SCC |= S_I; break;
    case 0x10: rb.snd.a = s_sub(rb.snd.a, rb.snd.b, 0); break;                      // SBA
    case 0x11: (void)s_sub(rb.snd.a, rb.snd.b, 0); break;                           // CBA
    case 0x16: rb.snd.b = rb.snd.a; s_logic(rb.snd.b); break;                       // TAB
    case 0x17: rb.snd.a = rb.snd.b; s_logic(rb.snd.a); break;                       // TBA
    case 0x19: {                                                                    // DAA (MAME's)
        uint8_t a = rb.snd.a, msn = a & 0xF0, lsn = a & 0x0F;
        unsigned cf = 0;
        if (lsn > 9 || (SCC & S_H)) cf |= 0x06;
        if (msn > 0x80 && lsn > 9) cf |= 0x60;
        if (msn > 0x90 || (SCC & S_C)) cf |= 0x60;
        r = cf + a;
        SCC &= (uint8_t)~(S_N | S_Z | S_V);
        s_nz8((uint8_t)r);
        if (r & 0x100) SCC |= S_C;
        rb.snd.a = (uint8_t)r;
        break;
    }
    case 0x1B: rb.snd.a = s_add(rb.snd.a, rb.snd.b, 0); break;                      // ABA
    case 0x30: rb.snd.x = (uint16_t)(rb.snd.s + 1); break;                          // TSX
    case 0x31: rb.snd.s++; break;
    case 0x32: rb.snd.a = s_pull(); break;
    case 0x33: rb.snd.b = s_pull(); break;
    case 0x34: rb.snd.s--; break;
    case 0x35: rb.snd.s = (uint16_t)(rb.snd.x - 1); break;                          // TXS
    case 0x36: s_push(rb.snd.a); break;
    case 0x37: s_push(rb.snd.b); break;
    case 0x39: pc = s_pull16(); break;                                              // RTS
    case 0x3B:                                                                      // RTI
        SCC = s_pull(); rb.snd.b = s_pull(); rb.snd.a = s_pull();
        rb.snd.x = s_pull16(); pc = s_pull16();
        break;
    case 0x3E:                                                                      // WAI
        s_push16(pc); s_push16(rb.snd.x); s_push(rb.snd.a); s_push(rb.snd.b); s_push(SCC);
        rb.snd.wai = 1;
        break;
    case 0x3F:                                                                      // SWI
        s_push16(pc); s_push16(rb.snd.x); s_push(rb.snd.a); s_push(rb.snd.b); s_push(SCC);
        SCC |= S_I;
        pc = s_rd16(0xFFFA);
        break;
    default: break;                                                                 // NOP, undefined
    }
    rb.snd.pc = pc;
    if (nest) cyc += snd_insn();
    return cyc;
}

static uint64_t snd_target(uint64_t main_t) { return main_t * RB_SND_NUM / RB_SND_DEN; }

// A delay loop, DECA, DECB or DEX then BNE back to it, all but the last of
// its turns before s in one go, as one instruction at a time would leave
// it (the flags are the next DEC's to set, and nothing reads them between).
// Robotron's sound ROM spends a third of its instructions in them. (Not in
// the traces' build, which compares every instruction with MAME's.)
static inline void snd_loop(uint64_t s) {
    uint16_t pc = rb.snd.pc;
    if (pc < 0xF000 || pc > 0xFFFD) return;
    const uint8_t *m = &roms.snd[pc - 0xF000];
    if (m[1] != 0x26 || m[2] != 0xFD) return;
    uint32_t dec, left;                             // the DEC's cycles; turns until its count is 0
    switch (m[0]) {
    case 0x4A: dec = 2; left = rb.snd.a ? rb.snd.a : 256u; break;
    case 0x5A: dec = 2; left = rb.snd.b ? rb.snd.b : 256u; break;
    case 0x09: dec = 4; left = rb.snd.x ? rb.snd.x : 65536u; break;
    default: return;
    }
    uint64_t room64 = s - rb.snd_now;
    uint32_t room = room64 > 0xFFFFFFu ? 0xFFFFFFu : (uint32_t)room64, per = dec + 4;
    if (room <= dec) return;
    uint32_t whole = (room - dec + per - 1) / per;  // turns whose BNE starts before s
    if (whole > left) whole = left;
    uint32_t k = whole - 1;                         // all but the last, each BNE taken
    if (!k) return;
    if (m[0] == 0x4A) rb.snd.a = (uint8_t)(rb.snd.a - k);
    else if (m[0] == 0x5A) rb.snd.b = (uint8_t)(rb.snd.b - k);
    else rb.snd.x = (uint16_t)(rb.snd.x - k);
    rb.snd_now += (uint64_t)per * k;
}

static void RB_HOT snd_run_until(uint64_t s) {
    while (rb.snd_now < s) {
        bool irq = rb.snd.irq && !(SCC & S_I);
        if (rb.snd.wai && !irq) { rb.snd_now = s; break; }
#ifndef RB_TRACE
        if (!irq) snd_loop(s);
#endif
        rb.snd_now += (uint64_t)snd_step();
    }
}

// the commands, in the main CPU's time, for the sound CPU to take at theirs
#define CMDQ 64
static struct { uint64_t t; uint8_t v; } cmdq[CMDQ];
static unsigned cmd_head, cmd_tail;

static void sound_cmd(uint8_t v) {
    if (cmd_head - cmd_tail >= CMDQ) cmd_tail++;            // (never: a frame holds a few)
    cmdq[cmd_head % CMDQ].t = m6809_now(&rb.cpu) + 5;
    cmdq[cmd_head % CMDQ].v = v | 0xC0;                     // (the high two bits are pulled up)
    cmd_head++;
}

static void snd_run_to(uint64_t main_t) {
    while (cmd_tail != cmd_head && cmdq[cmd_tail % CMDQ].t <= main_t) {
        snd_run_until(snd_target(cmdq[cmd_tail % CMDQ].t));
        uint8_t v = cmdq[cmd_tail % CMDQ].v;
        cmd_tail++;
        rb.pia[2].in_b = v;
        pia_cb1(2, v == 0xFF ? 0 : 1);
    }
    snd_run_until(snd_target(main_t));
}

uint64_t rb_snd_trace_cycles(void) { return rb.snd_now; }

// ---- the beam ----------------------------------------------------------------------------
// Boundary k is the start of line (247 + k) % 260, at k * 64 cycles (the
// first VBLANK starts at 0). There MAME draws the line, and its timers move
// CB1 (bit 5 of the line, every 32 lines but 256) and CA1 (lines 240 and up).
static void RB_HOT boundary(void) {
    uint32_t k = rb.line_k++;
    uint64_t t = (uint64_t)k * RB_LINE;
    unsigned v = (RB_LINES - 13 + k) % RB_LINES;
    if (v >= RB_VIS_Y0 && v < RB_VIS_Y0 + RB_VIS_H) {
        rb_frame_t *f = &rb_frame;
        unsigned y = v - RB_VIS_Y0;
        uint32_t *o = (uint32_t *)(void *)f->pix[y];
        const uint8_t *s = rb.ram + (RB_VIS_X0 / 2) * 256 + v;
        uint64_t ch = 0;
        for (int w = 0; w < RB_VIS_WORDS - 1; w++, s += 4 * 256) {
            uint32_t x = (uint32_t)s[0] | (uint32_t)s[256] << 8 | (uint32_t)s[512] << 16 | (uint32_t)s[768] << 24;
            if (x != o[w]) o[w] = x, ch |= 1ull << w;
        }
        uint32_t x = (uint32_t)s[0] | (uint32_t)s[256] << 8;        // (146 bytes: 2 in the last word)
        if (x != o[RB_VIS_WORDS - 1]) o[RB_VIS_WORDS - 1] = x, ch |= 1ull << (RB_VIS_WORDS - 1);
        f->changed[y] |= ch;
        uint8_t *p = f->pal[y];
        if (memcmp(p, rb.pal, 16)) {
            uint16_t pens = 0;
            for (int i = 0; i < 16; i++)
                if (p[i] != rb.pal[i]) pens |= (uint16_t)(1u << i), p[i] = rb.pal[i];
            f->pens[y] |= pens;
        }
        if (v == RB_VIS_Y0 + RB_VIS_H - 1) {
            f->frame = ++rb.frames;
            if (rb_on_frame) rb_on_frame();
        }
    }
    if (!(v & 31) && v != 256) pia_cb1(1, (v >> 5) & 1);
    if (v == 0) pia_ca1(1, 0);
    else if (v == 240) pia_ca1(1, 1);
    snd_run_to(t);
}

static void catch_up(uint64_t t) {
    while ((uint64_t)rb.line_k * RB_LINE < t) boundary();
}

void rb_run_to(uint64_t t) {
    for (;;) {
        uint64_t now = m6809_now(&rb.cpu);
        uint64_t next = (uint64_t)rb.line_k * RB_LINE;
        if (next <= now) { boundary(); continue; }
        if (now >= t) break;
        uint64_t stop = next < t ? next : t;
        maincpu_run(&rb.cpu, (int)(stop - now));
    }
    snd_run_to(t);
}

void rb_init(const rb_roms_t *r) {
    roms = *r;
    memset(rb.ram, 0, sizeof rb.ram);
    memset(rb.pal, 0, sizeof rb.pal);
    rb.bank = 0;
    rb.cocktail = 0;
    rb.pia[0].ca1 = rb.pia[1].ca1 = rb.pia[2].ca1 = 1;      // (MAME's: CA1 starts high, CB1 low)
    rb.pia[0].cb1 = rb.pia[1].cb1 = rb.pia[2].cb1 = 0;
    rb.pia[2].in_b = 0;
    for (int n = 0; n < 3; n++) pia_reset(n);
    rb.bl_solid = rb.bl_w = rb.bl_h = 0;
    rb.bl_src = rb.bl_dst = 0;
    map_pages();
    memset(&rb.cpu, 0, sizeof rb.cpu);
    maincpu_reset(&rb.cpu);
    rb.line_k = 0;
    rb.frames = 0;
    memset(&rb.snd, 0, sizeof rb.snd);
    memset(rb.snd_ram, 0, sizeof rb.snd_ram);
    rb.snd.cc = 0xC0 | S_I;
    rb.snd.pc = s_rd16(0xFFFE);
    rb.snd_now = 0;
    cmd_head = cmd_tail = 0;
    rb_dac_head = 0;
    dac_write(0xFF);                    // (the PIA's reset drives port A high)
}
