// rbcard.h -- the ROMs on the card, through fsro: loose or zipped in
// /ROBOTRON (MAME's robotron.zip, as it is), or /ROBOTRON.ZIP. Portable: the
// plugin and the Mac test (test/rb/card.sh, on disk images) share it.
// (Time Pilot's tpcard.c.)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "cvsd.h"                       // (fs_t: the SDK's sdplayer/fsro.h)
#include "rbroms.h"

#define RBCARD_HOME "/ROBOTRON"
#define RBCARD_ZIP  "/ROBOTRON.ZIP"

// Every file in RBCARD_HOME, then RBCARD_ZIP, until all are found: each
// ROM to its place (rbroms.h). A zip is never read whole: its directory,
// then each wanted entry's packed bytes into buf and unpacked after them.
// buf: max bytes (the directory, and a packed ROM with the ROM after it,
// must fit); tick: called between sectors (the plugin serves the
// console), or NULL. Returns rbroms' found bits.
uint32_t rbcard_load_roms(fs_t *fs, uint8_t *buf, uint32_t max, void (*tick)(void));
