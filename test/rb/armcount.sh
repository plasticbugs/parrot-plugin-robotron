#!/bin/sh
# armcount.sh [lists] [--from n] [--wave n] [--profile [--blocks FN]] [--top n] [--trace FILE] [--lists FILE]
# -- what a list costs the cart: the board, its sound, the picture and the
# list built for the Cortex-M33 with the plugin's flags (its FPU on), run on
# Unicorn with its instructions and cycles counted (test/rb/armcount.py).
# The game plays itself (arm_bench.c). RB_ROMS is the robotron.zip. ARM_PY:
# a Python with unicorn, capstone and pyelftools. (Time Pilot's.)
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$here/../../src
sdk=$here/../../sdk/firmware/revc/sdk
roms=${RB_ROMS:?RB_ROMS is the robotron.zip to use}
work=${RB_WORK:-${TMPDIR:-/tmp}/robotron-test}
out="$work/arm"
mkdir -p "$out"
# each file with the plugin's flags (CMakeLists.txt): the emulation -O3 and
# its state not split up, the picture -O2, the card's code -Os
cflags="-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp -mfpu=fpv5-sp-d16 -g -DNDEBUG \
    ${RB_CFLAGS:-} -ffunction-sections -I$src -I$sdk"
objs=
for f in "$here/arm_bench.c" "$src/rb.c" "$src/rbsnd.c" "$src/rbrun.c" "$src/rbcv.c" \
         "$src/rbroms.c" "$src/swzip.c" "$sdk/cvscale.c"; do
    case $(basename "$f") in
        rb.c|rbsnd.c|rbrun.c) opt="-O3 -fno-data-sections" ;;
        rbcv.c|cvscale.c) opt=-O2 ;;
        rbroms.c|swzip.c) opt=-Os ;;
        *) opt="-O2 -fdata-sections" ;;
    esac
    o="$out/$(basename "$f" .c).o"
    # shellcheck disable=SC2086
    arm-none-eabi-gcc $cflags $opt -c -o "$o" "$f"
    objs="$objs $o"
done
# shellcheck disable=SC2086
arm-none-eabi-gcc $cflags -Wl,--gc-sections -nostartfiles \
    -Wl,-e,bench_init -Wl,-Ttext=0x20000000 -Wl,--undefined=bench_list -Wl,--undefined=bench_zip \
    -o "$out/bench.elf" $objs -lm 2>&1 |
    grep -v "warning: cannot find entry\|_exit\|not implemented\|_sbrk\|_write\|_close\|_lseek\|_read\|_fstat\|_isatty\|_getpid\|_kill" || true
arm-none-eabi-size "$out/bench.elf"
exec "${ARM_PY:-python3}" -I "$here/armcount.py" "$out/bench.elf" "$roms" "$@"
