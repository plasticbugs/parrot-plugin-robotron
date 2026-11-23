// rbroms.c -- see rbroms.h. The sizes and CRC-32s are MAME 0.288's
// (williams.cpp's robotron). The two decoder PROMs aren't needed.
#include "rbroms.h"

#include <stddef.h>
#include <string.h>

rb_rom_store_t rb_rom_store;

#define L(o) (uint32_t)(offsetof(rb_rom_store_t, lo) + (o))
#define H(o) (uint32_t)(offsetof(rb_rom_store_t, hi) + (o))
const rb_rom_file_t rb_rom_files[RB_ROM_FILES] = {
    {"2084_rom_1b_3005-13.e4", 0x1000, 0x66c7d3efu, L(0x0000)},
    {"2084_rom_2b_3005-14.c4", 0x1000, 0x5bc6c614u, L(0x1000)},
    {"2084_rom_3b_3005-15.a4", 0x1000, 0xe99a82beu, L(0x2000)},
    {"2084_rom_4b_3005-16.e5", 0x1000, 0xafb1c561u, L(0x3000)},
    {"2084_rom_5b_3005-17.c5", 0x1000, 0x62691e77u, L(0x4000)},
    {"2084_rom_6b_3005-18.a5", 0x1000, 0xbd2c853du, L(0x5000)},
    {"2084_rom_7b_3005-19.e6", 0x1000, 0x49ac400cu, L(0x6000)},
    {"2084_rom_8b_3005-20.c6", 0x1000, 0x3a96e88cu, L(0x7000)},
    {"2084_rom_9b_3005-21.a6", 0x1000, 0xb124367bu, L(0x8000)},
    {"2084_rom_10b_3005-22.a7", 0x1000, 0x13797024u, H(0x0000)},
    {"2084_rom_11b_3005-23.c7", 0x1000, 0x7e3c1b87u, H(0x1000)},
    {"2084_rom_12b_3005-24.e7", 0x1000, 0x645d543eu, H(0x2000)},
    {"video_sound_rom_3_std_767.ic12", 0x1000, 0xc56c1d28u, (uint32_t)offsetof(rb_rom_store_t, snd)},
};
#undef L
#undef H

uint32_t rb_crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
    static const uint32_t t[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
                                   0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
                                   0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ t[crc & 15];
        crc = (crc >> 4) ^ t[crc & 15];
    }
    return ~crc;
}

int rbroms_wanted(uint32_t size, uint32_t crc, uint32_t found) {
    for (int i = 0; i < RB_ROM_FILES; i++)
        if (rb_rom_files[i].size == size && rb_rom_files[i].crc == crc && !(found & 1u << i)) return i;
    return -1;
}

bool rbroms_size_ok(uint32_t size) {
    for (int i = 0; i < RB_ROM_FILES; i++)
        if (rb_rom_files[i].size == size) return true;
    return false;
}

void rbroms_put(int i, const uint8_t *data) {
    const rb_rom_file_t *f = &rb_rom_files[i];
    memmove((uint8_t *)&rb_rom_store + f->at, data, f->size);
}

void rbroms_roms(rb_roms_t *r) {
    r->lo = rb_rom_store.lo;
    r->hi = rb_rom_store.hi;
    r->snd = rb_rom_store.snd;
}
