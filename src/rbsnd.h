// rbsnd: Robotron's sound for the cart (portable). The sound board's DAC
// (an MC1408 the 6808 writes through PIA 2's port A; rb.c logs each write
// with its time, rb_dac) as samples at the lists' rate (5,962 a second):
// each sample the mean of the DAC's level over its time (the level held
// between writes, as the DAC holds it), the resting level taken off (a
// pole at 15 Hz) and scaled to the DAC's levels on the console
// (rbcv_sound_pcm: +-32767 is +-511 levels, its soft knee from 191).
#pragma once
#include <stdint.h>

// the samples' scale: the console's DAC levels for 1.0 of MAME's (its
// speaker's full scale; the board's DAC goes in at 0.25 of it, as MAME
// routes it). test/rb/soundcmp.sh measures the loudness: -22.3 dBFS rms
// over a 90-second game, an ordinary cartridge's (-20.5 to -23.9).
#ifndef RBSND_GAIN
#define RBSND_GAIN 880.0f
#endif

void rbsnd_init(void);                  // after rb_init
// the next n samples (the board must have run past their time)
void rbsnd_pcm(int16_t *out, int n);
extern uint32_t rbsnd_samples;          // made so far
extern uint32_t rbsnd_lost;             // DAC writes the ring lost (the board ran too far ahead)
extern uint32_t rbsnd_waiting_max;      // the most writes waiting in the ring at a list's start (tests)
