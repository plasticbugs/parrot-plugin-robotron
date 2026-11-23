// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// Robotron: 2084 -- Williams' 1982 arcade game on the ColecoVision: the
// cart runs the arcade board (rb.c, from MAME 0.288's emulation: the 6809,
// the blitters, the PIAs, the CMOS and the sound board's 6808 and DAC)
// from the user's ROMs on the card, and rbcv.c sends the picture and the
// sound to the console as the frame kernel's lists. RESET goes back to
// the menu.
//
//   core 1  serves the kernel and a list a TV frame (cvframes.c); the
//           kernel reads both controllers (CONTROLLER2)
//   core 0  this loop: a list at a time (rbrun.c), the board run for the
//           list's time, the changes to the picture of its last frame and
//           the list's samples, handed to core 1 as soon as it will take it
//
// The console runs the lists back to back (their sound is a sample
// stream), and each list runs the board for just the list's time (16.77
// ms), so the game keeps the arcade's speed. A list that isn't ready when
// the console wants it makes the console run the last one again (its sound
// once more) and puts the game a list behind. The note counts those.
//
// The ROMs (MAME's "robotron" set) are read from the card's /ROBOTRON
// folder, loose or zipped, or from /ROBOTRON.ZIP, each known by its size
// and CRC-32. The board's CMOS (the high scores, the settings, the
// bookkeeping) is /ROBOTRON/NVRAM on the card, MAME's nvram file as it is:
// read at the start, written back when it has changed and no wave is being
// played (a sector a list, each holding the loop up for a few milliseconds).
//
// Controls (the kit's arcade keys, twin-stick): controller 1's stick (or a
// USB pad's d-pad or left stick) moves; controller 2's stick (or the pad's
// right stick, or its face buttons: top up, bottom down, left left, right
// right, two for a diagonal) fires that way. With one controller: hold
// the left fire button to fire the way you move, or the right one to stand
// still and fire the way the stick points (on a pad, L and R). Keypad * or
// #, or -, is a coin; keypad 1 or + starts one player, keypad 2 two. The
// board's own switches: keypad 0 is ADVANCE, keypad 9 held AUTO UP.
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#if __has_include("hardware/watchdog.h")
#include "hardware/watchdog.h"
#else                                   // (the kit's model of the console, test/host: no watchdog)
static inline void watchdog_enable(uint32_t ms, bool pause) { (void)ms; (void)pause; }
static inline void watchdog_update(void) {}
#endif

#include "cvdiag.h"
#include "cvframes.h"
#include "cvinput.h"
#include "cvsd.h"
#include "cvusb.h"
#include "gbkernel.h"
#include "handoff.h"
#include "vdp.h"
#include "rb.h"
#include "rbcard.h"
#include "rbcv.h"
#include "rbroms.h"
#include "rbrun.h"

#define OPEN_US     20000           // serve the console this long before the card's (blocking) start
#define WATCHDOG_MS 4000            // a hang reboots into the menu
#define NOTE_US     1000000         // the menu's note is brought up to date this often
#define GAME        "Robotron: 2084"  // in the menu's notes
#define TAG         "RB"
#define NV_NAME     "NVRAM"         // the CMOS, in RBCARD_HOME (MAME's nvram/robotron/nvram)
#define NV_QUIET    120             // lists with no wave on the screen before the CMOS is written

static rbcv_t enc;
static rbrun_t run;
static rb_roms_t roms;
static uint8_t list[RBCV_LIST_MAX];
static size_t list_len;
static bool list_pending;                   // built, not published yet
static uint32_t seen_frames;
static uint8_t *scratch;                    // rbcv's room before the game: a message's canvas, the card's reads
static size_t scratch_n;
static fs_t fs;                             // (the CMOS's file is written all game long: not in the scratch)

#define CANVAS      (*(canvas_t *)(void *)scratch)
_Static_assert(sizeof(canvas_t) <= RBCV_ROOM, "a message's canvas goes in rbcv's room");

// ---- the console ---------------------------------------------------------------------
static void tasks(void) {
    cvusb_task();
    cvf_task(time_us_32());
    watchdog_update();
}

static void publish(void) {
    if (list_pending && cvf_publish(list, list_len)) list_pending = false;
}

// TV frames the console has started since the last call (0: none yet, or
// the last list isn't out): for the messages' lists, a TV frame each
static uint32_t new_frames(void) {
    tasks();
    publish();
    if (list_pending) return 0;
    uint32_t f = cvf_frames(), n = f - seen_frames;
    seen_frames = f;
    return n;
}

static void send(void) {
    list_len = rbcv_tv_frame(&enc, list);
    list_pending = true;
    publish();
}

// ---- messages ------------------------------------------------------------------------
static void center(int y, const char *s, uint8_t color, int scale) {
    int w = (int)strlen(s) * 8 * scale;
    canvas_text(&CANVAS, w < VDP_W ? (VDP_W - w) / 2 : 0, y, s, color, scale);
}

static void message(const char *head, const char *l1, const char *l2, const char *l3) {
    canvas_init(&CANVAS);
    center(24, "ROBOTRON", C_LRED, 3);
    center(52, "2084", C_LYELLOW, 3);
    if (head) center(88, head, C_WHITE, 1);
    if (l1) center(108, l1, C_GRAY, 1);
    if (l2) center(120, l2, C_GRAY, 1);
    if (l3) center(132, l3, C_GRAY, 1);
    center(176, "RESET: BACK TO THE MENU", C_LBLUE, 1);
    rbcv_canvas(&enc, &CANVAS.px[0][0]);
}

// serve the console, a list a frame, for us microseconds (forever: 0)
static void serve_for(uint32_t us) {
    uint32_t t0 = time_us_32();
    while (!us || time_us_32() - t0 < us) {
        if (!new_frames()) continue;
        rbcv_sound_pcm(&enc, NULL, 0);
        send();
    }
}

static void __attribute__((noreturn)) fail(const char *why, const char *l2, const char *l3, const char *menu_note) {
    message("IT CAN'T START", why, l2, l3);
    cvf_note(menu_note);
    serve_for(0);
    for (;;) tight_loop_contents();
}

// between a file's sectors: the console keeps its picture
static void card_tick(void) {
    if (!new_frames()) return;
    rbcv_sound_pcm(&enc, NULL, 0);
    send();
}

// ---- the CMOS on the card -------------------------------------------------------------
// MAME's nvram file: the 1K of the board's CMOS, a byte a nibble (the high
// four read as 1s). A file just made (or not the board's) starts the game
// from nothing: it restores the factory settings, and waits for ADVANCE,
// which the plugin presses for it (fresh_cmos).
static fs_file_t nv_f;
static bool nv_ok, fresh_cmos;
static uint8_t nv_saved[0x400];             // the CMOS as the card has it
static uint8_t nv_last[0x400];              // the CMOS a list ago
static uint32_t nv_quiet;                   // lists with no wave on the screen and no change to the CMOS
static int nv_writing = -1;                 // the sector being written, a list each (-1: none)
static uint8_t sector[512];

static void nv_start(void) {
    fs_set_writer(&fs, cvsd_write);
    uint16_t name[8];
    int n = 0;
    for (; NV_NAME[n]; n++) name[n] = (uint8_t)NV_NAME[n];
    bool made = false;
    if (fs_create(&fs, RBCARD_HOME, name, n, sizeof rb.cmos, &nv_f, &made) != FS_OK || fs_open(&fs, &nv_f) != FS_OK ||
        !fs_lba(&fs, 1))
        return;                             // (no saves: the game plays from a CMOS of its own)
    nv_ok = true;
    bool good = !made;
    for (int s = 0; s < 2 && good; s++) {
        if (!cvsd_read(fs_lba(&fs, (uint32_t)s), sector)) good = false;
        else memcpy(rb.cmos + s * 512, sector, 512);
    }
    if (good)                               // all zero (MAME's own start) or the board's (each high nibble F)
        for (int i = 0; i < 0x400; i++)
            if (rb.cmos[i] && (rb.cmos[i] & 0xF0) != 0xF0) { good = false; break; }
    if (!good) memset(rb.cmos, 0, sizeof rb.cmos);
    fresh_cmos = true;
    for (int i = 0; i < 0x400; i++)
        if (rb.cmos[i]) { fresh_cmos = false; break; }
    memcpy(nv_saved, rb.cmos, sizeof nv_saved);
    memcpy(nv_last, rb.cmos, sizeof nv_last);
    if (made) nv_writing = 0;               // (a new file holds whatever the card did: written now)
}

// once a list: a sector of the CMOS written, if it has changed, and for
// NV_QUIET lists neither has it nor has the screen had a wave on it (never
// mid-wave; once the high score's initials are in)
static void nv_step(void) {
    if (!nv_ok) return;
    if (enc.layout == RBCV_PLAY || memcmp(nv_last, rb.cmos, sizeof nv_last)) {
        nv_quiet = 0;
        memcpy(nv_last, rb.cmos, sizeof nv_last);
    } else if (nv_quiet < NV_QUIET) {
        nv_quiet++;
    }
    if (nv_writing < 0) {
        if (nv_quiet < NV_QUIET || !memcmp(nv_saved, rb.cmos, sizeof nv_saved)) return;
        memcpy(nv_saved, rb.cmos, sizeof nv_saved);
        nv_writing = 0;
    }
    if (fs_write_sec(&fs, (uint32_t)nv_writing, nv_saved + nv_writing * 512) != FS_OK) nv_ok = false;
    nv_writing = nv_writing == 1 ? -1 : nv_writing + 1;
}

// ---- the controls ----------------------------------------------------------------------
// Ways as gamepad.h's GP_UP GP_DOWN GP_LEFT GP_RIGHT, opposite ways at once cancelled
static uint32_t ways(uint32_t d) {
    if ((d & (GP_UP | GP_DOWN)) == (GP_UP | GP_DOWN)) d &= ~(uint32_t)(GP_UP | GP_DOWN);
    if ((d & (GP_LEFT | GP_RIGHT)) == (GP_LEFT | GP_RIGHT)) d &= ~(uint32_t)(GP_LEFT | GP_RIGHT);
    return d;
}
// a console stick's 4 bits (low = pressed: up 1, right 2, down 4, left 8)
static uint32_t stick_ways(uint8_t joy) {
    return (joy & 1 ? 0 : GP_UP) | (joy & 2 ? 0 : GP_RIGHT) | (joy & 4 ? 0 : GP_DOWN) | (joy & 8 ? 0 : GP_LEFT);
}
// a USB pad's analog stick past about 40% each way
static uint32_t analog_ways(int x, int y) {
    return (y > 820 ? GP_UP : 0) | (y < -820 ? GP_DOWN : 0) | (x < -820 ? GP_LEFT : 0) | (x > 820 ? GP_RIGHT : 0);
}

// The kit's arcade keys (.claude/skills/parrot-cart-plugin/references/
// porting.md, twin-stick), as the board's ports have them (active high):
// IN0 0x01 up, 0x02 down, 0x04 left, 0x08 right (moving), 0x10 1P start,
// 0x20 2P start, 0x40 fire up, 0x80 fire down; IN1 0x01 fire left, 0x02
// fire right; IN2 0x01 AUTO UP, 0x02 ADVANCE, 0x10 coin 1. A coin is a
// pulse of 6 lists from the press, and the key has to be up for 3 lists
// before the next one counts.
static void controls(void) {
    uint8_t joy, fire, lines, joy2, fire2, lines2;
    cvf_input(&joy, &fire, &lines);                     // low = pressed
    cvf_input2(&joy2, &fire2, &lines2);                 // controller 2
    const int key = cvin_decode_key(lines);
    uint32_t buttons = 0, pad_move = 0, pad_aim = 0;
    gamepad_t p;
    if (cvusb_pad(0, &p)) {
        buttons = p.buttons;
        pad_move = cvusb_pad_dirs(&p);
        pad_aim = analog_ways(p.rx, p.ry) | (buttons & GP_NORTH ? GP_UP : 0) | (buttons & GP_SOUTH ? GP_DOWN : 0) |
                  (buttons & GP_WEST ? GP_LEFT : 0) | (buttons & GP_EAST ? GP_RIGHT : 0);
    }
    uint32_t move = ways(stick_ways(joy) | pad_move);
    uint32_t aim = ways(stick_ways(joy2) | pad_aim);
    // one controller: the left fire button fires the way the man moves (or
    // last moved), the right one stands him still and fires the way the
    // stick points (or the way it last fired)
    static uint32_t last_move = GP_UP, last_aim = GP_UP;
    const bool fire_move = !(fire & 1) || (buttons & GP_L), fire_stand = !(fire & 2) || (buttons & GP_R);
    if (!aim && fire_stand) {
        aim = move ? move : last_aim;
        move = 0;
    } else if (!aim && fire_move) {
        aim = move ? move : last_move;
    }
    if (move) last_move = move;
    if (aim) last_aim = aim;
    const bool coin = key == CVIN_STAR || key == CVIN_HASH || (buttons & GP_MINUS);
    const bool start1 = key == 1 || (buttons & GP_PLUS);
    const bool start2 = key == 2;
    static bool coin_held;
    static int coins_owed, coin_down, coin_up;
    if (coin && !coin_held && coins_owed < 2) coins_owed++;
    coin_held = coin;
    if (!coin_down && !coin_up && coins_owed) { coins_owed--; coin_down = 6; }
    const bool coin_switch = coin_down > 0;
    if (coin_down) { if (!--coin_down) coin_up = 3; }
    else if (coin_up) coin_up--;
    uint8_t in0 = 0, in1 = 0, in2 = 0;
    if (move & GP_UP) in0 |= 0x01;
    if (move & GP_DOWN) in0 |= 0x02;
    if (move & GP_LEFT) in0 |= 0x04;
    if (move & GP_RIGHT) in0 |= 0x08;
    if (start1) in0 |= 0x10;
    if (start2) in0 |= 0x20;
    if (aim & GP_UP) in0 |= 0x40;
    if (aim & GP_DOWN) in0 |= 0x80;
    if (aim & GP_LEFT) in1 |= 0x01;
    if (aim & GP_RIGHT) in1 |= 0x02;
    if (coin_switch) in2 |= 0x10;
    if (key == 0) in2 |= 0x02;              // ADVANCE
    if (key == 9) in2 |= 0x01;              // AUTO UP
    // a CMOS from nothing: the game restores the factory settings, then
    // waits for ADVANCE (at its 700th frame, as in MAME with an empty nvram)
    if (fresh_cmos && rb.frames >= 700 && rb.frames < 720) in2 |= 0x02;
    rb_in.in0 = in0;
    rb_in.in1 = in1;
    rb_in.in2 = in2;
}

// ---- the game ------------------------------------------------------------------------
static uint32_t published, frames_at_start, us_max, pics_at_note;

// one list: the controls, the board for the list's time, the list made,
// then handed over as soon as core 1 takes it; then the CMOS's sector, if
// one is to be written
static void game_list(void) {
    uint32_t t0 = time_us_32();
    controls();
    list_len = rbrun_list(&run, list);
    uint32_t us = time_us_32() - t0;
    if (us > us_max) us_max = us;
    while (!cvf_publish(list, list_len)) tasks();
    published++;
    tasks();
    nv_step();
}

// the menu's bottom line, should RESET come: pictures a second, lists the
// console ran again for want of a new one (since the start), the slowest
// list this second, the bus's odd selects
static void update_note(void) {
    static uint32_t t_last;
    uint32_t t = time_us_32();
    if (t - t_last < NOTE_US) return;
    int32_t again = (int32_t)(cvf_frames() - frames_at_start) - (int32_t)(published - 1);
    if (again < 0) again = 0;
    unsigned per_s = MIN(run.pictures - pics_at_note, 99u), ms = MIN(us_max / 1000, 99u), tenths = us_max / 100 % 10;
    uint32_t unsel, early, unhdr;
    cvf_bus_counts(&unsel, &early, &unhdr);
    char n[48];
    snprintf(n, sizeof n, TAG " %u/s %ldL %u.%ums s%u h%u", per_s, (long)MIN(again, 9999), ms, tenths,
             (unsigned)MIN(unsel, 999u), (unsigned)MIN(unhdr, 999u));
    cvf_note(n);
    t_last = t;
    pics_at_note = run.pictures;
    us_max = 0;
}

int main(void) {
    static const cvf_config_t cfg = {gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN};
    cvf_start(&cfg);
    cvd_start();
    cvd_stage(1);
    cvusb_start();                          // after the kernel's start: a host, for what is plugged into the hub
    rbcv_init(&enc);
    scratch = rbcv_scratch(&scratch_n);
    message("LOOKING FOR THE ROMS", "IN " RBCARD_HOME " ON THE CARD", NULL, NULL);
    serve_for(OPEN_US);

    if (!cvsd_card_in() || !cvsd_init()) fail(cvsd_card_in() ? "THE CARD DIDN'T ANSWER" : "NO CARD IN THE SLOT", NULL, NULL, GAME ": no card");
    if (fs_mount(&fs, cvsd_read) != FS_OK) fail("NO FAT OR EXFAT ON THE CARD", NULL, NULL, GAME ": card didn't mount");
    cvd_stage(2);
    watchdog_enable(WATCHDOG_MS, true);
    // the zips' reads in the scratch (no message is drawn until they are
    // done with: fail() draws over them)
    uint32_t found = rbcard_load_roms(&fs, scratch, (uint32_t)scratch_n, card_tick);
    cvd_stage(3);
    if (found != RBROMS_ALL) {
        int missing = 0;
        const char *first = "";
        for (int i = 0; i < RB_ROM_FILES; i++)
            if (!(found & 1u << i) && !missing++) first = rb_rom_files[i].name;
        char l2[40], note[48];
        if (!found) snprintf(l2, sizeof l2, "NONE OF ITS %d FILES IS THERE", RB_ROM_FILES);
        else snprintf(l2, sizeof l2, "%d OF %d MISSING: %.14s", missing, RB_ROM_FILES, first);
        for (char *c = l2; *c; c++)
            if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
        snprintf(note, sizeof note, "Robotron: %d ROM%s missing", missing, missing == 1 ? "" : "s");   // (32 characters at most)
        fail("PUT MAME'S ROBOTRON ROMS, OR", "ROBOTRON.ZIP, IN " RBCARD_HOME, l2, note);
    }
    rbroms_roms(&roms);
    nv_start();                             // the CMOS (after the ROMs: its file stays the open one)
    rb_init(&roms);
    rbrun_start(&run, &enc);
    cvd_stage(4);
    cvf_note(GAME);

    while (list_pending) tasks(), publish();          // the last message's list out first
    game_list();                            // (the lists run again are counted from the first game list on)
    frames_at_start = cvf_frames();
    for (;;) {
        game_list();
        update_note();
    }
}
