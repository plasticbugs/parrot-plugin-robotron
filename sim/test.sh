#!/bin/sh
# sim/test.sh [NAME...] -- the simulator, checked headless: the kit's
# examples and the template's plugin, each built with sim/run.sh and run
# with --headless and a script of inputs, as test/run.sh runs them on the
# models alone. No window opens and nothing plays.
#
# For each: the log must say what test/run.sh's does; the picture must be a
# 256x192 screenshot; and where test/run.sh has run first (its pictures in
# $TMPDIR/parrotcart-test), the simulator's picture and log must be the
# same as its. Beyond that, what only the simulator does: the Sound
# example's scale, in tune in the WAV; the SD card as a folder, the save
# landing in it as a file; a file opened with --open.
#
#   sim/test.sh                 everything
#   sim/test.sh sound sd-card   just those
#
# The names: hello screen sound usb-gamepad usb-keyboard-mouse usb-midi-out
# sd-card open-file frames two-controllers psram lua vdp usb. Pictures and
# logs: build-sim/test/.
set -e
# (on Windows, in Git Bash or MSYS2: C:/... paths throughout, and the shell
# told not to turn the card's "/NOTES/..." into a path on this computer)
kit=$(cd "$(dirname "$0")/.." && { pwd -W 2>/dev/null || pwd; })
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
out=$kit/build-sim/test
host=${TMPDIR:-/tmp}/parrotcart-test
mkdir -p "$out"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy  # (the simulator insists, headless, anyway)
py=
for p in python3 python; do "$p" -c 'import sys' >/dev/null 2>&1 && { py=$p; break; }; done
[ -n "$py" ] || { echo "sim/test.sh: needs Python 3"; exit 2; }
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) windows=1 ;; *) windows=0 ;; esac
status=0
ONLY="$*"
wanted() { [ -z "$ONLY" ] && return 0; case " $ONLY " in *" $1 "*) return 0 ;; esac; return 1; }
fail() { echo "  FAIL: $*"; status=1; }
compared=0

# run NAME DIR [simulator options] -- the plugin in DIR, headless; o= its
# output, $out/NAME. A run clears that folder only if a run made it (it holds
# .sim-test, or run.txt from an older sim/test.sh), so a card folder with the
# test's name is left alone and the test fails.
run() {
    name=$1; dir=$2; shift 2
    o=$out/$name
    echo "$name"
    skip=
    if [ -e "$o" ] && [ ! -f "$o/.sim-test" ] && [ ! -f "$o/run.txt" ] && [ -n "$(ls -A "$o" 2>/dev/null)" ]; then
        fail "$o isn't a test's output, so it stays: give the test or that folder another name"
        skip=1; return 0
    fi
    rm -rf "$o"; mkdir -p "$o"; : > "$o/.sim-test"
    [ -d "$dir" ] || { echo "  (not here: $dir)"; skip=1; return 0; }
    if "$kit/sim/run.sh" "$dir" --headless --out "$o" "$@" >"$o/run.txt" 2>&1; then :; else
        fail "the simulator stopped with an error:"; sed 's/^/    /' "$o/run.txt" | tail -15
    fi
    # (warnings fail the check, as in test/run.sh; not MSVC's, about code written for GCC)
    if grep -q 'compiler warnings' "$o/run.txt"; then
        if [ $windows = 1 ]; then grep 'compiler warnings' "$o/run.txt"; else fail "$(grep 'compiler warnings' "$o/run.txt")"; fi
    fi
    grep '^  [0-9]* ms' "$o/run.txt" || true
    [ -f "$o/screen.png" ] && { "$py" -I "$kit/tools/png256.py" check "$o/screen.png" >/dev/null || fail "screen.png isn't a 256x192 screenshot"; }
    same_as "$name"
}

# midi_log FILE LINE... -- what --midi-log wrote: these lines, no others
midi_log() {
    [ -z "$skip" ] || return 0
    f=$1; shift
    printf '%s\n' "$@" > "$f.want"
    cmp -s "$f" "$f.want" || { fail "$f isn't what the plugin should have sent:"; diff "$f.want" "$f" | head -8 | sed 's/^/    /'; }
}

# same_as NAME [picture] -- the last run's picture and log against test/run.sh's
# NAME, if it has run (the picture only: a card folder isn't macOS's image, so
# its sector reads, and the log's times, differ)
same_as() {
    [ -z "$skip" ] && [ -f "$host/$1/screen.png" ] || return 0
    compared=$((compared + 1))
    if "$py" -I "$kit/tools/png256.py" same "$o/screen.png" "$host/$1/screen.png" >"$o/same.txt" 2>&1; then
        echo "  test/host: the same picture"
    else fail "the picture isn't test/host's $1: $(cat "$o/same.txt")"; fi
    if [ "$2" != picture ] && [ -f "$host/$1/log.txt" ] && ! cmp -s "$o/log.txt" "$host/$1/log.txt"; then
        fail "the log isn't test/host's $1:"; diff "$host/$1/log.txt" "$o/log.txt" | head -8 | sed 's/^/    /'
    fi
}

# expect PATTERN... -- in the last run's log (grep -E)
expect() {
    [ -z "$skip" ] || return 0
    for want in "$@"; do
        grep -Eq -- "$want" "$o/log.txt" || fail "nothing in $o/log.txt matches: $want"
    done
}

# save_check FILE MAGIC RUNS -- a save file a plugin wrote in a card folder:
# 512 bytes, starting with MAGIC, a run count at byte 8 (as test/run.sh's
# card_check reads it on a card image)
save_check() {
    got=$("$py" -I -c 'import struct,sys; d=open(sys.argv[1],"rb").read(); print(len(d), d[:len(sys.argv[2])].decode(), struct.unpack_from("<I", d, 8)[0])' "$1" "$2" 2>&1) || true
    if [ "$got" = "512 $2 $3" ]; then echo "  the folder: $(basename "$1") is 512 bytes, run $3"
    else fail "$1: $got (wanted run $3)"; fi
}

# a card, as a folder: test/run.sh's tree
tree() {
    rm -rf "$1"
    mkdir -p "$1/NOTES" "$1/PLUGINS"
    printf 'Hello from the card.\nThis is the first file.\n' > "$1/A first file.txt"
    i=1; : > "$1/NOTES/Long story.txt"
    while [ $i -le 60 ]; do echo "Line $i of a story long enough to scroll, and to wrap around the screen's 30 columns." >> "$1/NOTES/Long story.txt"; i=$((i + 1)); done
}

# ---- the plugin in src/ (tools/new-plugin.py rewrites this part) ----
# Robotron: 2084 plays from the user's ROMs: RB_ROMS names MAME's
# robotron.zip (never in the repository). The card is a folder here, so the
# runs have names of their own (a folder's reads and times aren't an
# image's); the message with no ROMs is the same picture as test/run.sh's.
# The DAC's lists run back to back, 60,036 T each by the kit's costs (as
# in test/run.sh, which says more): the model's frame check fails each one.
# rb_run allows just those lines, up to 60,199 T; anything else still fails.
rb_run() {
    was=$status; status=0
    run "$@" > "$out/$1.out" 2>&1
    long=$(grep -Ec 'FAIL at [0-9]+ ms: a list takes 60[01][0-9][0-9] T: a frame is 59736$' "$out/$1/run.txt" || true)
    grep -Ev 'FAIL at [0-9]+ ms: a list takes 60[01][0-9][0-9] T: a frame is 59736$' "$out/$1.out" |
        sed 's/ -- FAILED$/ -- its long lists only (below)/' | grep -v '^  FAIL: the simulator stopped with an error:$'
    [ "$long" -gt 0 ] && echo "  $long lists of 60,1xx T: the DAC's, back to back (allowed)"
    if [ $status != 0 ] && ! grep -Ev 'FAIL at [0-9]+ ms: a list takes 60[01][0-9][0-9] T: a frame is 59736$' "$out/$1/run.txt" |
            grep -Eq 'FAIL at|error' && ! grep -v '^  FAIL: the simulator stopped with an error:$' "$out/$1.out" | grep -q '^  FAIL'; then
        status=0
    fi
    [ $status = 0 ] && status=$was
}
rb_folder() {                           # rb_folder DIR [ZIP]: a card folder, /ROBOTRON holding ZIP
    rm -rf "$1"; mkdir -p "$1/ROBOTRON" "$1/PLUGINS"
    [ -n "$2" ] && cp "$2" "$1/ROBOTRON/robotron.zip"
    printf 'not a ROM\n' > "$1/ROBOTRON/readme.txt"
}
if wanted robotron-noroms; then
    rb_folder "$out/card-rb-noroms"
    run rb-noroms "$kit/src" --ms 1500 --card "$out/card-rb-noroms"
    expect 'note "Robotron: 13 ROMs missing"' 'back to the menu' 'psg 80 00 9F A0 00 BF C0 00 DF FF'
    same_as robotron-noroms picture
fi
if wanted robotron; then
    if [ -n "$RB_ROMS" ]; then
        # a fresh card: the factory settings (ADVANCE pressed by the plugin),
        # a coin and a game with both sticks, then the game's CMOS on the
        # card as MAME's nvram file
        rb_folder "$out/card-rb" "$RB_ROMS"
        rb_run rb-game "$kit/src" --ms 30000 --card "$out/card-rb" \
            --script "14000:key=* 14150:key= 15000:key=1 15150:key= 16000:joy=up 16000:joy2=left 18000:joy=down+right 19000:joy2=up+right 21000:joy= 21000:joy2=down 23000:joy2= 24000:joy=left 24000:joy2=right 27000:joy= 27000:joy2="
        expect 'note "RB [0-9]+/s' 'back to the menu, its bottom line "RB' 'psg .*E4 FF'
        got=$("$py" -I -c 'import sys
d = open(sys.argv[1], "rb").read()
print(len(d), "board" if any(d) and all(b == 0 or b >> 4 == 15 for b in d) else "other")' "$out/card-rb/ROBOTRON/NVRAM" 2>&1) || true
        if [ "$got" = "1024 board" ]; then echo "  the folder: ROBOTRON/NVRAM is the board's CMOS, 1024 bytes"
        else fail "the folder's ROBOTRON/NVRAM: $got"; fi
    else
        echo "rb-game"; echo "  (RB_ROMS isn't set: MAME's robotron.zip, for the game)"
    fi
fi
if wanted robotron-usb; then
    if [ -n "$RB_ROMS" ]; then
        rb_folder "$out/card-rb-usb" "$RB_ROMS"
        rb_run rb-usb "$kit/src" --ms 24000 --card "$out/card-rb-usb" \
            --script "1000:pad=up 14000:pad=minus 14150:pad=up 15000:pad=plus 15150:pad=up 16000:lstick=-1800,0 16500:rstick=0,1900 18000:pad=north+east 19000:pad=l 20000:pad=r+down 22000:snap=usb"
        expect 'usb: the port is a host' 'back to the menu, its bottom line "RB'
    else
        echo "rb-usb"; echo "  (RB_ROMS isn't set: MAME's robotron.zip, for the game)"
    fi
fi
# ---- the examples ----
# (left out with examples/: tools/new-plugin.py --examples keeps them)

[ $compared = 0 ] && echo "(no test/host pictures to compare with: test/run.sh hasn't run here)"
[ $status = 0 ] && echo "all simulator checks passed (pictures: $out/*/screen.png)"
exit $status
