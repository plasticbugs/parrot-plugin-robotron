#!/bin/sh
# compare.sh [frames] [seed] -- the board against MAME 0.288, frame by
# frame: the RAM (the picture and the work RAM), the palette and the CMOS
# at every frame_done, over a game script.py makes (a coin, one player, the
# sticks at random; default 7200 frames, two minutes). MAME runs headless.
# Everything goes under $TMPDIR (RB_WORK). RB_ROMS is the robotron.zip.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../src" && pwd)
frames=${1:-7200}
seed=${2:-2084}
work=${RB_WORK:-${TMPDIR:-/tmp}/robotron-test}
roms=${RB_ROMS:?RB_ROMS is the robotron.zip to use}
out="$work/compare-$seed"
rm -rf "${out:?}"; mkdir -p "$out"
cc -O2 -Wall -I"$src" -o "$out/rb_host" "$here/rb_host.c" "$src/rb.c" "$src/rbroms.c" "$src/swzip.c"
python3 -I "$here/script.py" "$frames" "$seed" > "$out/script.txt"
"$out/rb_host" "$roms" -frames "$frames" -script "$out/script.txt" -dump "$out/model.dump"
"$here/mamerun.sh" "$out" "$out/script.txt" "$frames"
python3 -I "$here/cmpdump.py" "$out/mame.dump" "$out/model.dump"
