// rb: the Robotron: 2084 board (Williams, 1982), as MAME 0.288 has it
// (src/mame/midway/williams*.cpp, BSD-3-Clause): a 6809 at 1 MHz, 48K of
// RAM that is also the picture (304 x 256, 4 bits a pixel), the banked
// ROMs, the special chip (the blitter, SC1), three 6821 PIAs, the CMOS and
// the sound board (a 6808 at 894,886 Hz and an 8-bit DAC). Portable C, for
// the cart and the Mac tests.
//
// The board keeps one clock, the main CPU's cycles from the start. A
// scanline is 64 of them and a frame 260 lines (16,640 cycles, 60.10 a
// second). As the beam passes each visible line, the line's pixels and
// the palette are copied into rb_frames[] (MAME draws each line at its
// start), so a picture never shows half of a sprite the game is moving.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "m6809.h"

#define RB_CLOCK   1000000u             // main CPU cycles a second
#define RB_LINE    64                   // cycles a scanline
#define RB_LINES   260
#define RB_FRAME   (RB_LINE * RB_LINES) // 16,640
#define RB_VIS_Y0  7                    // MAME's visible area: lines 7-246, pixels 6-297
#define RB_VIS_H   240
#define RB_VIS_X0  6
#define RB_VIS_W   292
#define RB_VIS_B   (RB_VIS_W / 2)       // VRAM bytes a visible line (two pixels each)
// the sound CPU: a 6808 on a 3,579,545 Hz crystal, which it divides by 4.
// MAME rounds that up (m6800.h: (clocks + 3) / 4), so its 6808 runs
// 894,887 cycles a second, not 894,886.25, and the sound's timing against
// the main CPU's drifts by that 0.84 ppm: 9 cycles in 12 s. As MAME's:
#define RB_SND_HZ  894887u
#define RB_SND_NUM RB_SND_HZ            // the sound CPU's cycles for RB_SND_DEN of the main's
#define RB_SND_DEN RB_CLOCK

// the controls, as MAME's ports have them (active high)
typedef struct {
    uint8_t in0;    // 01 move up, 02 down, 04 left, 08 right, 10 1P start, 20 2P start, 40 fire up, 80 fire down
    uint8_t in1;    // 01 fire left, 02 fire right
    uint8_t in2;    // 01 auto up, 02 advance, 04 coin 3, 08 high score reset, 10 coin 1, 20 coin 2, 40 tilt
} rb_in_t;
extern rb_in_t rb_in;

typedef struct {
    const uint8_t *lo;                  // 36K: $0000-$8FFF (banked over the RAM for reads)
    const uint8_t *hi;                  // 12K: $D000-$FFFF
    const uint8_t *snd;                 // 4K:  the sound CPU's $F000-$FFFF
} rb_roms_t;

typedef struct {
    uint8_t ctl_a, ctl_b, ddr_a, ddr_b, out_a, out_b, in_b;
    uint8_t irq_a1, irq_b1, ca1, cb1, irq_a, irq_b;
} rb_pia_t;

// The picture as the beam draws it: each visible line's VRAM bytes (two
// pixels each, the even one in the high nibble; 146 of them, then 2 spare
// for whole words) and the palette then (BBGGGRRR). One buffer: a line is
// taken as the beam passes it, over the last frame's. Each line also says
// what changed since the picture was last taken from it (rbcv clears
// these): its words (4 bytes, 8 pixels; bit w for bytes 4w-4w+3) and its
// pens whose colour did. rb_on_frame is called as the last line is taken:
// the frame is whole then, until the next frame's first line, 21 lines on.
#define RB_VIS_STRIDE 148
#define RB_VIS_WORDS  37
typedef struct {
    uint8_t pix[RB_VIS_H][RB_VIS_STRIDE];
    uint8_t pal[RB_VIS_H][16];
    uint64_t changed[RB_VIS_H];
    uint16_t pens[RB_VIS_H];
    uint32_t frame;                     // its number (rb.frames once it was whole)
} rb_frame_t;

typedef struct {
    uint8_t ram[0xC000];                // $0000-$BFFF: the picture, then the work RAM
    uint8_t cmos[0x400];                // $CC00-$CFFF, 4 bits a byte (the high 4 read as 1)
    uint8_t pal[16];
    uint8_t bank;                       // $C900 bit 0: reads of $0000-$8FFF from the ROMs
    uint8_t cocktail;
    rb_pia_t pia[3];                    // 0: the controls, 1: IN2, the interrupts, the sound; 2: the sound board's
    uint8_t bl_solid, bl_w, bl_h;       // the blitter's registers
    uint16_t bl_src, bl_dst;
    m6809_t cpu;
    uint32_t line_k;                    // the next scanline boundary: line_k * 64 cycles
    uint32_t frames;                    // frames drawn (at the start of line 246, the last)
    // the sound board
    struct {
        uint16_t pc, x, s;
        uint8_t a, b, cc;
        uint8_t irq, wai;
    } snd;
    uint8_t snd_ram[0x100];
    uint64_t snd_now;                   // its cycles from the start
    uint8_t dac;
} rb_t;
extern rb_t rb;
extern rb_frame_t rb_frame;

// The DAC's writes, a ring the sound code reads: each the sound CPU's cycle
// count (its low 24 bits) << 8 | the byte. The sound takes them a list
// behind the board, at most (test/rb/cart_host reports the most waiting).
#define RB_DAC_RING 2048
extern uint32_t rb_dac[RB_DAC_RING];
extern uint32_t rb_dac_head;            // writes so far

void rb_init(const rb_roms_t *roms);    // and reset; the CMOS as it is (rb.cmos)
uint64_t rb_time(void);                 // main CPU cycles from the start
void rb_run_to(uint64_t t);             // the board until t (the CPU a few cycles past it)
extern void (*rb_on_frame)(void);       // as each frame is drawn whole (rb_frame)

// tests: the registers before each main CPU instruction (host builds only)
extern void (*rb_trace)(const m6809_t *c);
extern void (*rb_strace)(void);         // the sound CPU's (rb.snd)
extern uint64_t rb_snd_trace_cycles(void);
