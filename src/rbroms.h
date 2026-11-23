// rbroms.h -- where the ROMs go: MAME's "robotron" set (the Solid Blue
// label one), each file known by its size and CRC-32, never by its name, so
// a merged zip with its clones' folders works as it is. Portable: the
// plugin and the Mac tests share it. (Time Pilot's tproms.h.)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "rb.h"

typedef struct {
    const char *name;                   // MAME's name, for messages
    uint32_t size, crc;
    uint32_t at;                        // where its bytes go in rb_rom_store
} rb_rom_file_t;

#define RB_ROM_FILES 13
#define RBROMS_ALL   ((1u << RB_ROM_FILES) - 1)
extern const rb_rom_file_t rb_rom_files[RB_ROM_FILES];

typedef struct {
    uint8_t lo[0x9000], hi[0x3000], snd[0x1000];
} rb_rom_store_t;
extern rb_rom_store_t rb_rom_store;

uint32_t rb_crc32(uint32_t crc, const uint8_t *p, uint32_t n);
// which of the files still to find (not in found) has this size and CRC-32 (-1: none)
int rbroms_wanted(uint32_t size, uint32_t crc, uint32_t found);
bool rbroms_size_ok(uint32_t size);     // one of the files' sizes
// a file's bytes (size of them, its CRC-32 right) to its place
void rbroms_put(int i, const uint8_t *data);
// the board's ROMs, from the store
void rbroms_roms(rb_roms_t *r);
