#!/bin/sh
# mamerun.sh OUTDIR SCRIPT FRAMES [EVERY] -- MAME's robotron, headless, with
# rbdump.lua: OUTDIR/mame.dump. RB_WAV set: MAME's sound to that file too
# (-wavwrite, 48 kHz). RB_ROMS is the set's zip. The NVRAM starts empty
# (all zero), as rb_host's CMOS does.
#
# MAME runs headless, always: no window, no sound out, as fast as it goes,
# and a time limit so it can't hang.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$1; script=$2; frames=$3; every=${4:-1}
roms=${RB_ROMS:?RB_ROMS is the robotron.zip to use}
mkdir -p "$out"
rp=$(mktemp -d "${TMPDIR:-/tmp}/rbrp.XXXXXX")
cp "$roms" "$rp/robotron.zip"
mkdir "$rp/cfg" "$rp/nvram"
secs=$(( frames / 60 + 60 ))
RB_SCRIPT="$script" RB_DUMP="$out/mame.dump" RB_FRAMES="$frames" RB_EVERY="$every" \
SDL_VIDEODRIVER=dummy mame robotron -rompath "$rp" \
    -video none -videodriver dummy -window -nomaximize -sound none \
    -nothrottle -frameskip 0 -skip_gameinfo -seconds_to_run "$secs" \
    -cfg_directory "$rp/cfg" -nvram_directory "$rp/nvram" -snapshot_directory "$rp/snap" \
    -autoboot_delay 0 -autoboot_script "$here/rbdump.lua" ${RB_WAV:+-wavwrite "$RB_WAV"} >"$out/mame.log" 2>&1 || true
rm -rf "${rp:?}"
