// rbsnd.c -- see rbsnd.h.
#include "rbsnd.h"

#include <math.h>

#include "rb.h"
#include "rbcv.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define SND_HOT __attribute__((section(".time_critical.rbsnd")))
#else
#define SND_HOT
#endif

// Sample n covers the sound CPU's cycles S(n) to S(n + 1): a sample is a
// hundredth of a list (60,036 of the console's T at 3,579,545 Hz), at the
// sound CPU's 894,887 Hz (rb.h) 150.09 of its cycles. S is kept as a whole
// part and a remainder (S_DEN of a cycle), so nothing drifts.
#define S_NUM ((uint64_t)RBCV_LIST_T * RB_SND_HZ)
#define S_DEN ((uint64_t)RBCV_PCM_N * RBCV_CONSOLE_HZ)
static uint64_t s_at;                   // S(n), whole
static uint64_t s_frac;                 // and the remainder, in S_DEN
static uint32_t rd;                     // the next DAC write to take
static uint8_t level;                   // the DAC as the writes so far leave it
static float hp_x, hp_y, hp_a;          // the high-pass (the resting level off)
uint32_t rbsnd_samples, rbsnd_lost, rbsnd_waiting_max;

void rbsnd_init(void) {
    s_at = rb.snd_now;
    s_frac = 0;
    rd = rb_dac_head;
    level = rb.dac;
    hp_x = (float)level * (0.25f / 128.0f);     // (at rest from the start: no step)
    hp_y = 0;
    hp_a = expf(-2.0f * 3.14159265f * 15.0f / ((float)RBCV_PCM_N * RBCV_CONSOLE_HZ / RBCV_LIST_T));
    rbsnd_samples = rbsnd_lost = rbsnd_waiting_max = 0;
}

void SND_HOT rbsnd_pcm(int16_t *out, int n) {
    if (rb_dac_head - rd > rbsnd_waiting_max) rbsnd_waiting_max = rb_dac_head - rd;
    if (rb_dac_head - rd > RB_DAC_RING) {          // (never, as the board runs: a list's writes fit)
        rbsnd_lost += rb_dac_head - rd - RB_DAC_RING;
        rd = rb_dac_head - RB_DAC_RING;
    }
    for (int k = 0; k < n; k++) {
        uint64_t t0 = s_at;
        s_at += S_NUM / S_DEN;
        s_frac += S_NUM % S_DEN;
        if (s_frac >= S_DEN) { s_at++; s_frac -= S_DEN; }
        uint64_t t1 = s_at, t = t0;
        uint32_t acc = 0;
        // the writes before t1: the level held between them, weighed by its time
        while (rd != rb_dac_head) {
            uint32_t e = rb_dac[rd & (RB_DAC_RING - 1)];
            uint32_t d = ((uint32_t)t1 - (e >> 8)) & 0xFFFFFF;     // (24 bits of time: t1 - the write's)
            if (!d || d >= 0x800000) break;                       // at or after t1: the next sample's
            uint64_t tw = t1 - d;
            if (tw > t) {
                acc += level * (uint32_t)(tw - t);
                t = tw;
            }
            level = (uint8_t)e;
            rd++;
        }
        acc += level * (uint32_t)(t1 - t);
        // MAME's: the DAC's 0-255 as -1.0 to 127/128, at 0.25 into the
        // speaker (its rest taken off below, so only the scale counts)
        float x = (float)acc / (float)(t1 - t0) * (0.25f / 128.0f);
        hp_y = hp_a * (hp_y + x - hp_x);
        hp_x = x;
        float v = hp_y * RBSND_GAIN * 64.0f;
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32767.0f) v = -32767.0f;
        out[k] = (int16_t)lrintf(v);
    }
    rbsnd_samples += (uint32_t)n;
}
