// arm_bench.c -- the cart's code for test/rb/armcount.py: what a list (a
// TV frame) costs on the Cortex-M33, counted in instructions on Unicorn.
// Built bare (no SDK) with the plugin's flags (armcount.sh). The Python
// side puts MAME's robotron.zip in bench_zip, calls bench_init() once and
// bench_list() for each list. The game plays itself: ADVANCE through the
// factory settings (a fresh CMOS, as main.c does), a coin and 1P start,
// then the sticks at random (a new way every 8-40 frames), a coin and a
// start again every 50 seconds in case the game is over. bench_wave set:
// the game starts at that wave (the wave's number poked as wave 1 begins),
// for the heavier waves (the brains, the tanks). (Time Pilot's.)
#include <string.h>

#include "../../src/rb.h"
#include "../../src/rbcv.h"
#include "../../src/rbroms.h"
#include "../../src/rbrun.h"
#include "../../src/rbsnd.h"
#include "swzip.h"

uint8_t bench_zip[0x20000];
uint32_t bench_zip_len;
uint8_t bench_wave;                     // 0: wave 1 as the game has it
uint8_t bench_behind;                   // (armcount.py's queue; unused here)
uint32_t bench_len, bench_pictures, bench_hurried, bench_found;
uint8_t bench_out[RBCV_LIST_MAX];       // the last list (armcount.py reads it back)
static rbcv_t cv;
static rbrun_t run;
static uint32_t rng = 2084, next_way;
static uint8_t buf[0x3000];

static uint32_t rand32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void on_frame(void) {
    uint32_t n = rb.frames, t = (n - 1500) % 3000;             // (from frame 1500: a coin, a start)
    rb_in.in2 = (n >= 700 && n < 720) ? 0x02 : 0;               // ADVANCE (the factory settings)
    if (n >= 1500 && t < 6) rb_in.in2 |= 0x10;                  // a coin
    rb_in.in0 &= (uint8_t)~0x10;
    if (n >= 1500 && t >= 60 && t < 66) rb_in.in0 |= 0x10;      // 1P start
    if (bench_wave && n == 1580) rb.ram[0xBDED] = bench_wave;
    if (n >= 1700 && n >= next_way) {
        static const uint8_t ways[9] = {0, 1, 2, 4, 8, 5, 9, 6, 10};  // up, down, left, right, the diagonals
        uint32_t r = rand32();
        uint8_t mv = ways[r % 9], fire = ways[1 + (r >> 8) % 8];
        rb_in.in0 = (uint8_t)((rb_in.in0 & 0x30) | mv | (fire & 3) << 6);
        rb_in.in1 = (uint8_t)(fire >> 2);
        next_way = n + 8 + (r >> 16) % 33;
    }
    rbrun_frame();
}

void bench_init(void) {
#if defined(__arm__)
    volatile uint32_t *cpacr = (volatile uint32_t *)0xE000ED88;     // the FPU on (CP10, CP11)
    *cpacr |= 0xFu << 20;
#endif
    swzip_entry_t e;
    for (int i = 0; swzip_entry(bench_zip, bench_zip_len, i, &e); i++) {
        int r = rbroms_wanted(e.usize, e.crc, bench_found);
        if (r < 0 || e.usize > sizeof buf || !swzip_read(&e, buf)) continue;
        rbroms_put(r, buf);
        bench_found |= 1u << r;
    }
    static rb_roms_t roms;
    rbroms_roms(&roms);
    rbcv_init(&cv);
    rb_init(&roms);
    rbrun_start(&run, &cv);
    rb_on_frame = on_frame;
}

void bench_list(void) {
    bench_len = (uint32_t)rbrun_list(&run, bench_out);
    bench_pictures = run.pictures;
    bench_hurried = run.waited;
}

void _exit(int x) { (void)x; for (;;); }
