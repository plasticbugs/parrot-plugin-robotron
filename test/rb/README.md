# Robotron: 2084: how it works and how it's tested

The cart runs the arcade board the way MAME 0.288 emulates it: the 6809, the two blitters, the three 6821 PIAs, the CMOS, the scanline counter, and the sound board's 6808 and DAC. The tests in this folder run the same game in MAME and in this code. They compare the RAM, the palette and the CMOS at every frame, and both CPUs' registers and cycle counts at every instruction, and they match.

Two things in MAME matter for the sound board. MAME's 6808 divides its 3,579,545 Hz crystal by 4 rounding up, so it runs 894,887 cycles a second, not 894,886.25. Over a game that's 9 cycles in 12 seconds against the main CPU, enough to move a sound command by an instruction. And after TAP, or a CLI that clears the I flag, MAME runs the next instruction inside the first one, before an interrupt can come. The model does both.

## The picture

The board's bitmap is 304 by 256, of which MAME shows 292 by 240. In a wave the game uses 278 by 229 of it:

| Arcade lines | What | On the TV |
|---|---|---|
| 7-14 | scores and lives | lines 0-7, at the arcade's size, 18 columns to the left |
| 15-229 | the play field in its border, columns 6-283 | lines 8-186: 278 x 215 become 256 x 179 |
| 231-235 | the wave number | lines 187-191, at the arcade's size |

In the play field, 22 pairs of columns and 36 pairs of lines become one each, spread evenly. Two pixels become one the way the SDK's `cvscale.h` does it, so a stroke one pixel wide survives and the men keep their shapes.

Between waves (the attract mode, the title, the high scores and the rectangles) the game draws on the whole screen, and the whole 292 by 240 is scaled to 256 by 192 the same way. The play field's border tells the two apart. The play layout stays for two seconds after the border goes, so a wave's end or a man lost doesn't switch the layout back and forth.

The palette is 16 pens a line (the game changes them as the beam goes). Each pen takes the ColecoVision colour nearest it in OKLab. A grey goes only to black, grey or white, and a very dark colour goes to black. A place of 8 by 1 pixels has two colours on the TMS9918. Where it has more, each pixel takes the nearer of the place's two most used.

The board's picture is taken line by line as the beam draws it, with a note of which 8-pixel words and which pens changed. A picture is made when the board finishes a frame, only where something changed. Each list may spend about 0.8 million cycles on the picture. A picture that needs more, because the whole screen changed, carries on at the next frame from that frame's lines. So a list is never late for it. Over 90 seconds from power-on, the attract mode and then a game, that comes to 48 pictures a second (`cart_host`), and the board's full 60 during a wave.

## The sound

The sound board's DAC is played as samples, 100 a list (5,962 a second), through the SN76489's three tones parked as a DAC. Each sample is the mean of the DAC's level over its time, the level held between the 6808's writes, as MAME's DAC holds it. The resting level is taken off with a pole at 15 Hz.

`test/rb/soundcmp.sh` plays a 90-second game in MAME's `robotron` and through the plugin, and compares them:

| What | Sound to difference | Spectral distance from MAME |
|---|---|---|
| The board's DAC, at the cart's sample rate | 26.0 dB | 1.7 dB |
| The plugin's lists on MAME's ColecoVision, through the SDK's kernel | 10.1 dB | 6.9 dB |

The replay measures -22.3 dBFS rms (median -22.9, peak -3.9) with `tools/wavlevel.py`. Ordinary ColecoVision cartridges measured the same way give -20.5 to -23.9. Peaks are rounded off with a soft knee from 3/4 of full scale, never clipped. Robotron's sounds are loud square waves, and about 29% of a game's samples reach the knee, where a full-swing square wave (220 levels of 255) comes out 3% quieter.

## The cost

`test/rb/armcount.sh` builds the plugin's code for the Cortex-M33 with the plugin's flags and counts what each list costs on Unicorn, in the M33's book cycles. A list has 2.52 million cycles at 150 MHz.

| The game (`--from 1550`) | Average | 99th percentile | Worst | Lists late |
|---|---|---|---|---|
| Wave 1 on, 7,200 lists | 12.0 ms (72%) | 18.9 ms | 20.2 ms | 0 |
| Wave 5 on (`--wave 5`), 5,400 lists | 12.9 ms (77%) | 19.1 ms | 20.1 ms | 0 |
| Wave 9 on (`--wave 9`), 5,400 lists | 12.9 ms (77%) | 19.2 ms | 20.1 ms | 0 |

A list has 16.77 ms. One that takes longer is fine if those before it took less, because the plugin makes each list as soon as the last is published and `cvframes` holds one more. "Late" counts the lists the console would have run again, and there were none. On the cart, the menu's note after RESET says the same thing from the real hardware: `RB 60/s 0L 19.3ms` is 60 pictures a second, no list run again, and the slowest list in the last second.

Per list, the sound board's 6808 takes about 0.46 million cycles, the main 6809 and the blitter about 0.64 million, and the picture about 0.4 million. Robotron's sound ROM spends a third of its instructions in delay loops (`DECA` then `BNE` back to it, and the same with `DECB` and `DEX`). `rb.c` runs all but the last turn of such a loop at once, which leaves the board exactly as running each instruction would. `loopcheck.sh` checks this, and the traces against MAME run every instruction.

## The tests

- `test/run.sh` and `sim/test.sh` run the plugin on the kit's model of the console and in the simulator. With `RB_ROMS=path/to/robotron.zip` they play a game from a card with the ROMs on it, check the CMOS the plugin writes to the card and read it back. They also find the ROMs loose and as `/ROBOTRON.ZIP`, and name one that's missing. Without `RB_ROMS` they check only the missing-ROM message.
- `test/rb/compare.sh [frames] [seed]` checks the board against MAME frame by frame: the RAM, the palette and the CMOS, over a game `script.py` makes. MAME can stop in the middle of an instruction at the end of a frame, so a difference that's gone within two frames is counted as a blip, not a failure (seed 5 over 7,200 frames: 163 blips, one of them two frames running, 14 bytes at most).
- `test/rb/trace.sh [seconds] [from] [script]` checks the main CPU against MAME's, instruction by instruction, from the end of frame `from`. With `RB_CPU=sound` it checks the 6808.
- `test/rb/loopcheck.sh [frames] [seed]` checks the shortcut through the sound ROM's delay loops (above) against running every instruction: the 6808's registers, cycles and RAM at every frame, and every DAC write and its time, are the same.
- `test/rb/soundcmp.sh` compares the sound with MAME's and measures how loud it is.
- `test/rb/armcount.sh` counts the cost of a list on the Cortex-M33 (`--wave n` starts the game at wave n).
- `test/rb/cart_host` (built from `cart_host.c`, `rb_host.c` and `src/`) runs the cart's side on the Mac and writes previews, list statistics and the lists themselves for `tools/mame-replay.sh`.

They need MAME's `robotron.zip` (`RB_ROMS`) and, for the console's side, the ColecoVision BIOS in `MAME_ROMPATH`. MAME runs headless. Neither file is in this repository.

The kit's console model expects every list to end 1,200 cycles before its TV frame. This plugin's lists run back to back, 60,036 cycles each, because their length is the sample clock. The kit's porting notes describe this for sample sound. The plugin's entries in `test/run.sh` and `sim/test.sh` allow those lines and nothing else, and MAME runs every list.
