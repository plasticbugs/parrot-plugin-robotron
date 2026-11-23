// rbrun: the cart's loop, a list at a time, shared by the plugin and the
// Mac tests so both run the board the same way. Portable. (Time Pilot's
// tprun.c.)
//
// Each list runs the board for the list's time (RBCV_LIST_T of the
// console's T: 16.77 ms, a little more than the board's 16.64 ms frame),
// to a cycle count kept from the start so nothing drifts; RBCV_PCM_N
// samples of the sound go with the list (rbsnd.c), taken RBRUN_LEAD
// cycles behind the board so the DAC's writes for them are always made.
//
// The picture: made as the board finishes a frame (rb_on_frame, the board
// stopped at its last line), so it is the whole frame, as the beam drew
// it. A picture is begun only when the console has had the last one's
// bytes (or RBRUN_SKIP lists have gone by): a picture that needs more than
// a list can carry is sent over the lists it takes, and the game runs on
// at its own speed meanwhile. Each list may spend RBRUN_BUDGET on the
// picture (rbcv.c's costs): one that needs more (the whole screen changed)
// goes on at the next frame, so a list is never late for it.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rb.h"
#include "rbcv.h"
#include "rbsnd.h"

#define RBRUN_LEAD 2600                 // main CPU cycles: 2.6 ms
#ifndef RBRUN_SKIP
#define RBRUN_SKIP 4                    // a picture at least every 4 lists, sent or not
#endif
#ifndef RBRUN_BUDGET
#define RBRUN_BUDGET 1800               // the picture's work a list (test/rb/armcount.sh: about 0.8 M cycles)
#endif

typedef struct {
    rbcv_t *cv;
    uint32_t lists;                     // since the start
    uint64_t target;                    // the main CPU cycle this list runs to
    uint32_t pictures;
    uint32_t waited;                    // frames that made no picture (the last one's bytes not all sent)
    uint32_t pieces;                    // frames that went on with a picture the budget had cut short
    int32_t budget;                     // what this list may still spend on the picture
    uint32_t shown_frame;               // the board's frame the last picture was made from
    uint8_t since;                      // lists since the last picture
    int16_t pcm[RBCV_PCM_N];
    void (*on_picture)(void);           // (tests: after each picture is made)
} rbrun_t;

// the board started (rb_init done); sets rb_on_frame to rbrun_frame
void rbrun_start(rbrun_t *r, rbcv_t *cv);
// one list: the board for its time, then the list into out; returns its length
size_t rbrun_list(rbrun_t *r, uint8_t *out);
// at each frame the board finishes (rb_on_frame; a test's own calls it on)
void rbrun_frame(void);
