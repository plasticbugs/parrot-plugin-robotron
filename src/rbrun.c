// rbrun.c -- see rbrun.h.
#include "rbrun.h"

#include <string.h>

static rbrun_t *running;

void rbrun_frame(void) {
    rbrun_t *r = running;
    if (!r) return;
    if (r->cv->in_pass || !r->cv->dirty || r->since >= RBRUN_SKIP) {
        if (r->cv->in_pass) r->pieces++;
        if (r->budget > 0 && rbcv_picture(r->cv, &rb_frame, &r->budget)) {
            r->shown_frame = rb_frame.frame;
            r->pictures++;
            r->since = 0;
            if (r->on_picture) r->on_picture();
        }
    } else {
        r->waited++;
    }
}

void rbrun_start(rbrun_t *r, rbcv_t *cv) {
    memset(r, 0, sizeof *r);
    r->cv = cv;
    running = r;
    rb_on_frame = rbrun_frame;
    rbsnd_init();
    r->target = rb_time();
}

size_t rbrun_list(rbrun_t *r, uint8_t *out) {
    r->lists++;
    r->budget = RBRUN_BUDGET;
    r->target = ((uint64_t)r->lists * RBCV_LIST_CYCLES_Q16 >> 16) + RBRUN_LEAD;
    if (r->since < 255) r->since++;
    rb_run_to(r->target);
    rbsnd_pcm(r->pcm, RBCV_PCM_N);
    rbcv_sound_pcm(r->cv, r->pcm, RBCV_PCM_N);
    return rbcv_tv_frame(r->cv, out);
}
