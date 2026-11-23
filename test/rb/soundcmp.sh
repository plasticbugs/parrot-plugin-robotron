#!/bin/zsh
# soundcmp.sh [frames] [seed] -- the sound against MAME's, and how loud it is.
#
# A game (script.py's: a coin, one player, the sticks at random) in MAME's
# robotron (headless, -wavwrite), in rb_host (the DAC's level held between
# the sound CPU's writes, at 48 kHz) and in cart_host (the plugin's samples:
# rbsnd.c, rbcv.c). cart_host's lists go through the SDK's real kernel on
# MAME's ColecoVision (tools/mame-replay.sh with MAME_WAV), as the console
# would play them, and also as the DAC's levels straight from the lists (no
# MAME). Each against MAME's robotron (sndcmp.c), and the replay's loudness
# (tools/wavlevel.py) against an ordinary cartridge's, about -22 dBFS rms
# (docs/testing.md). Everything goes under $TMPDIR (RB_WORK). RB_ROMS is
# the robotron.zip; MAME_ROMPATH a folder with the ColecoVision BIOS
# (coleco.zip), if MAME's own rompath has none. (Time Pilot's.)
set -e
here=${0:A:h}
src="$here/../../src"
kit="$here/../.."
sdk="$kit/sdk/firmware/revc/sdk"
frames=${1:-5400}
seed=${2:-7}
work=${RB_WORK:-${TMPDIR:-/tmp}/robotron-test}
roms=${RB_ROMS:?RB_ROMS is the robotron.zip to use}
out="$work/sound"
rm -rf "${out:?}"; mkdir -p "$out"
cc -O2 -Wall -I"$src" -o "$out/rb_host" "$here/rb_host.c" "$src/rb.c" "$src/rbroms.c" "$src/swzip.c"
cc -O2 -Wall -Wno-unused-function -DRB_HOST_NO_MAIN -I"$src" -I"$sdk" -o "$out/cart_host" "$here/cart_host.c" "$here/rb_host.c" \
    "$src/rb.c" "$src/rbroms.c" "$src/swzip.c" "$src/rbcv.c" "$src/rbsnd.c" "$src/rbrun.c" "$sdk/cvscale.c" -lm
cc -O2 -Wall -o "$out/sndcmp" "$here/sndcmp.c" -lm
python3 -I "$here/script.py" "$frames" "$seed" > "$out/script.txt"
"$out/rb_host" "$roms" -frames "$frames" -script "$out/script.txt" -wav "$out/model.wav"
RB_WAV="$out/mame.wav" "$here/mamerun.sh" "$out/mame" "$out/script.txt" "$frames" 1000000
lists=$(( frames * 16640 * 3579545 / 1000000 / 60036 ))    # (a frame is 16.64 ms, a list 16.77)
secs=$(( frames / 60 + 20 ))
"$out/cart_host" "$roms" -lists "$lists" -script "$out/script.txt" -out "$out/pcm.bin" -wav "$out/levels.wav" > "$out/pcm.log"
MAME_WAV="$out/pcm-raw.wav" "$kit/tools/mame-replay.sh" "$out/pcm.bin" "$out/replay" "$secs" > /dev/null
# the replay starts later and runs 0.5% fast (wavalign.py): stretched back
# and lined up by the envelopes (sndcmp finds the rest, within 50 ms)
python3 -I "$here/wavalign.py" "$out/mame.wav" "$out/pcm-raw.wav" "$out/pcm.wav" 60036/59736
echo "the board's sound (the DAC at 48 kHz), sample for sample:"; "$out/sndcmp" -raw "$out/mame.wav" "$out/model.wav"
echo "the board's sound, at the cart's rate:"; "$out/sndcmp" "$out/mame.wav" "$out/model.wav"
echo "the plugin's samples, the DAC's levels from the lists:"; "$out/sndcmp" "$out/mame.wav" "$out/levels.wav"
echo "the plugin's lists on MAME's ColecoVision:"; "$out/sndcmp" "$out/mame.wav" "$out/pcm.wav"
echo "how loud (the replay; an ordinary cartridge is about -22 dBFS rms):"
python3 -I "$kit/tools/wavlevel.py" "$out/pcm-raw.wav"
