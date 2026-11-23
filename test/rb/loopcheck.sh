#!/bin/sh
# loopcheck.sh [frames] [seed] -- rb.c's shortcut through the sound ROM's
# delay loops (snd_loop) changes nothing. The board with it and without it
# (the traces' build, -DRB_TRACE, runs every instruction) over a game: the
# 6808's registers, cycles and RAM at every frame, every DAC write and its
# time, and the main board's RAM, palette and CMOS, all the same. RB_ROMS
# is MAME's robotron.zip; everything goes under $TMPDIR (RB_WORK).
set -e
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../src"
frames=${1:-5400}
seed=${2:-7}
work=${RB_WORK:-${TMPDIR:-/tmp}/robotron-test}/loopcheck
roms=${RB_ROMS:?RB_ROMS is the robotron.zip to use}
rm -rf "$work"; mkdir -p "$work"
for b in fast slow; do
    flags=; [ $b = slow ] && flags=-DRB_TRACE
    cc -O2 $flags -I"$src" -o "$work/rb_$b" "$here/rb_host.c" "$src/rb.c" "$src/rbroms.c" "$src/swzip.c"
done
python3 -I "$here/script.py" "$frames" "$seed" > "$work/script.txt"
for b in fast slow; do
    "$work/rb_$b" "$roms" -frames "$frames" -script "$work/script.txt" -sdump "$work/$b.snd" -dump "$work/$b.dump"
done
cmp "$work/fast.snd" "$work/slow.snd"
cmp "$work/fast.dump" "$work/slow.dump"
echo "loopcheck: $frames frames, the same with the shortcut and without it ($(grep -c '^w ' "$work/fast.snd") DAC writes)"
