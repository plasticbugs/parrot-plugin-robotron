# What in this repository came from elsewhere

The plugin's code is BSD-3-Clause (`LICENSE`). The plugin template it
started from (its build files, `lib/`, `test/`, `tools/`, `docs/` and
workflows) and the copy of the cart's SDK in `sdk/` are by the Parrot Cart
contributors, under the MIT license below. These parts came from other
projects and keep their original licenses:

| Where | What | License |
|---|---|---|
| `sdk/firmware/revc/sdk/tinyusb/` | [TinyUSB](https://github.com/hathach/tinyusb) (Ha Thach and contributors), with the cart's fixes, listed in its `VENDORED.md` | MIT (`sdk/firmware/revc/sdk/tinyusb/LICENSE`) |
| `sdk/firmware/revc/midihello/tinyusb_fix/hub.c` | TinyUSB 0.18's hub driver with one fix (used only when a plugin builds against the pico-sdk's TinyUSB) | MIT (its header) |
| `sim/third_party/rtmidi/` | [RtMidi](https://github.com/thestk/rtmidi) 6.1.0 (Gary P. Scavone and contributors), unchanged, listed in its `VENDORED.md`: the simulator's MIDI ports | MIT-style (`sim/third_party/rtmidi/LICENSE`) |
| `lua/vendor/lua-5.4.8/` | [Lua](https://www.lua.org) 5.4.8 (Roberto Ierusalimschy, Luiz Henrique de Figueiredo and Waldemar Celes, PUC-Rio), with two lines of `luaconf.h` changed, listed in its `VENDORED.md`: the Lua plugin's language | MIT (`lua/vendor/lua-5.4.8/LICENSE`) |
| `cmake/pico_sdk_import.cmake` | from the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | BSD 3-Clause |
| `tools/licenses/GPL-2.0.txt`, `GPL-3.0.txt`, `LGPL-2.1.txt` | the GNU licenses, as the [Free Software Foundation](https://www.gnu.org/licenses/) publishes them | verbatim copies, as each text allows |
| `tools/licenses/Apache-2.0.txt` | the Apache License 2.0, as the [Apache Software Foundation](https://www.apache.org/licenses/) publishes it | verbatim copy |

A built plugin also contains the Raspberry Pi Pico SDK's runtime (BSD
3-Clause) and, if it uses USB, TinyUSB (MIT). Say so in your plugin's
credits (`docs/release-checklist.md`). A plugin written in Lua (`docs/lua.md`)
also contains Lua (MIT).

The 8x8 screen font's letter shapes come from the 1982 arcade game Mr. Do!

## MAME

The code in `src/` was written for this plugin. Parts of it are ports of
MAME's code, or were written with MAME's code as the reference. That code
is under the BSD 3-Clause license (`LICENSE`). It comes from MAME
(<https://github.com/mamedev/mame>, tag `mame0288`), and its copyright
holders are listed here.

| Here | From MAME | Copyright holders |
|---|---|---|
| `src/rb.c`, `src/rb.h`: the memory map, the ROM bank, the blitter, the palette, the CMOS, the scanline counter and its interrupts, the sound board's map, the sound commands and the DAC | `src/mame/midway/williams.cpp`, `williams_m.cpp`, `williams_v.cpp`, `williamsblitter.cpp` | Aaron Giles |
| `src/rb.c`: the three 6821 PIAs | `src/devices/machine/6821pia.cpp` | Aaron Giles, Nathan Woods |
| `src/rb.c`: the sound board's 6808 (its instructions, its interrupts and its clock) | `src/devices/cpu/m6800/m6800.cpp`, `m6800.h`, `6800ops.hxx` | Aaron Giles |
| `src/rb.c`: the screen's timing (when each line is drawn) | `src/emu/screen.cpp` | Aaron Giles |
| `src/m6809.h`, `src/m6809_core.h` (from Parrot Cart's Star Wars plugin) | `src/devices/cpu/m6809/m6809.cpp`, `m6809.lst`, `base6x09.lst` | Nathan Woods |
| `src/rbroms.c`: the ROMs' names, sizes and CRC-32s | `src/mame/midway/williams.cpp` | Aaron Giles |

The zip reader in `src/swzip.c` (the SDK's) follows RFC 1951 and the
method of zlib's `contrib/puff` by Mark Adler (zlib license). It was
written afresh.

`src/rbcv.c`, `src/rbrun.c` and the card and sample code follow the cart's
Time Pilot and Donkey Kong plugins, by Parrot Cart.

Robotron: 2084 is a trademark of its owners. The game is Williams
Electronics', 1982. This plugin contains none of the game.

## The plugin template's license

```
MIT License

Copyright (c) 2026 Parrot Cart contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
