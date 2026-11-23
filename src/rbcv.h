// rbcv: the cart's side of Robotron: 2084 on the ColecoVision. Portable C
// (no pico-sdk), used by the plugin and by the Mac tests.
//
// The console runs the SDK's frame kernel (gbkernel.asm, with controller
// 2): each TV frame it takes a command list from the cart and runs it.
// This makes those lists from the board's frames (rb.h) and its sound
// (rbsnd.h):
//
//   once:                         rbcv_init(e)
//   at each board frame:          rbcv_picture(e, frame, &budget) (or rbcv_canvas for a message)
//   each list:                    rbcv_sound_pcm(e, ...)
//                                 len = rbcv_tv_frame(e, list)
//
// The picture. MAME shows 292 x 240 of the board's 304 x 256 bitmap. In a
// wave the game uses 278 x 229 of it: the scores and lives (lines 7-14),
// the play field in its border (lines 15-229, columns 6-283) and the wave
// under it (lines 231-235). The TMS9918 has 256 x 192, so:
//
//   lines 0-7      the scores and lives, at the arcade's size (every
//                  digit as the game draws it), 18 columns to the left
//   lines 8-186    the play field and its border, 278 x 215 to 256 x 179:
//                  22 of its columns and 36 of its lines are pairs made one
//   lines 187-191  "N WAVE" (or the credits), at the arcade's size
//
// Two lines or columns become one the way sdk/cvscale.h has it (a stroke
// one pixel wide survives), spread evenly, so the men keep their shapes.
// Between waves (the attract mode, the title, the high scores, the
// rectangles between waves) the board draws on all of its 292 x 240, and
// the picture is all of it scaled the same way (36 column pairs, 48 line
// pairs). The play field's border tells the two apart; the play layout
// stays two seconds after the border goes, so a wave's end and a man lost
// don't change the layout and back.
//
// Each line's 16 colours (the palette, BBGGGRRR) are the TMS9918 colours
// nearest them (OKLab), as the beam drew the line. A place of 8 x 1 pixels
// has two colours on the TMS9918: where a place has more, each pixel takes
// the nearer of its two most used.
//
// A list carries the bytes that differ from what the console holds, as
// many as the frame has time for, the places the board changed first.
//
// The sound: samples through the SN76489's three tones parked at a period
// of 1, their attenuators summed as a DAC, RBCV_PCM_N a list, with the
// console running the lists back to back.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rb.h"

#define RBCV_W      256
#define RBCV_H      192
#define RBCV_BYTES  6144            // pattern bytes, and as many colour bytes
#define RBCV_LIST_MAX 3000
#define RBCV_PCM_N  100             // samples a list
#define RBCV_LIST_T 60036           // a list's length in the console's T (3,579,545 a second)
#define RBCV_CONSOLE_HZ 3579545u
// the board's main CPU cycles (1 MHz) in a list, << 16
#define RBCV_LIST_CYCLES_Q16 ((uint64_t)RBCV_LIST_T * RB_CLOCK * 65536u / RBCV_CONSOLE_HZ)

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define RBCV_HOT __attribute__((section(".time_critical.rbcv")))
#else
#define RBCV_HOT
#endif

enum { RBCV_PLAY, RBCV_FULL };      // the layouts (above)
#define RBCV_HOLD 120               // frames the play layout outlasts the border

typedef struct {
    uint32_t tv_frames, pictures, pat_bytes, col_bytes, runs, refreshed, pcm_frames;
    uint32_t settled;               // lists that ended with nothing left to send
    uint32_t t_max;                 // the longest list, in T
    uint32_t lines;                 // the board's lines that changed (in the pictures made)
    uint32_t cells;                 // 8 x 1 places made again
    uint32_t layouts;               // changes of layout
    uint32_t clipped;               // samples the soft knee took in
} rbcv_stats_t;

typedef struct {
    // the console's VRAM as the lists so far leave it, and the newest picture
    uint8_t vram_pat[RBCV_BYTES], vram_col[RBCV_BYTES];
    uint8_t want_pat[RBCV_BYTES], want_col[RBCV_BYTES];
    bool names_done;
    uint16_t names_at;
    uint16_t cursor;                // where the next list's scan starts
    uint16_t refresh_at;
    bool dirty;                     // want differs from vram somewhere (or may)
    uint32_t bands;                 // the same, for each band of 8 lines (256 bytes)

    bool pic_valid;                 // the picture state holds the last picture
    bool in_pass, pass_all;         // a picture under way (all of it, or what changed)
    uint16_t pass_y, pass_r;        // its next board line, its next TMS9918 line
    uint8_t layout;                 // RBCV_PLAY or RBCV_FULL
    uint32_t play_frame;            // the board's last frame with the play field's border

    // the sound
    bool pcm;                       // the lists carry samples
    uint8_t pcm_gain;               // 0-32: the DAC comes up over the first lists
    uint8_t pcm_bytes[RBCV_PCM_N][3];  // each sample's three attenuators, in the order written
    uint8_t dac_att[3];             // the attenuators as the last sample left them
    uint8_t tone_bytes[12];
    uint8_t tone_n;
    uint8_t resync;
    rbcv_stats_t stats;
} rbcv_t;

void rbcv_init(rbcv_t *e);
// room the plugin may use before the first rbcv_picture (a message's
// canvas, the card's reads); rbcv_canvas and the sound leave it alone. At
// least RBCV_ROOM bytes: a message's canvas (vdp.h's canvas_t) fits.
#define RBCV_ROOM (50 * 1024)
uint8_t *rbcv_scratch(size_t *n);

// The picture under way, or a new one, of the board's frame f (whole: at
// rb_on_frame), its lines' notes of what changed cleared as they're taken:
// as much as *budget allows (rbcv.c has the costs), less what it spent.
// True when the picture is done; false: the rest at the next frame.
bool rbcv_picture(rbcv_t *e, rb_frame_t *f, int32_t *budget);
// the layout a frame would have
int rbcv_layout_of(const rb_frame_t *f);
// a new picture: 256 x 192 pixels, a TMS9918 colour each (1-15); for messages
void rbcv_canvas(rbcv_t *e, const uint8_t *px);

// The next list's sound: n samples at the lists' rate, in the DAC's levels
// either side of its rest, 64 a level (rbsnd.h; n = 0: none, a message: the
// console waits for the vertical blank again, and the DAC is let go).
void rbcv_sound_pcm(rbcv_t *e, const int16_t *s, int n);

// the next list into out (RBCV_LIST_MAX bytes); returns its length
size_t rbcv_tv_frame(rbcv_t *e, uint8_t *out);

// the picture the console shows once the lists so far have run, a TMS9918
// colour a pixel (tests, previews); the picture wanted; the picture before
// the TMS9918's two colours a place (every pixel its own colour)
void rbcv_screen(const rbcv_t *e, uint8_t *px);
void rbcv_wanted(const rbcv_t *e, uint8_t *px);
void rbcv_ideal(const rbcv_t *e, uint8_t *px);
// bytes the console still needs (tests; a whole scan)
uint32_t rbcv_backlog(const rbcv_t *e);
// the DAC's level for the three tones' attenuators (tests: the lists' sound as a WAV)
float rbcv_dac_level(int att0, int att1, int att2);
// the TMS9918's colours as 0xRRGGBB (previews)
extern const uint32_t rbcv_tms_rgb[16];
// a palette byte (BBGGGRRR) as MAME's resistors make it, 0xRRGGBB
uint32_t rbcv_pal_rgb(uint8_t v);
