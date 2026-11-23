#!/bin/sh
# trace.sh [seconds] [from] [script] -- the main CPU against MAME's,
# instruction by instruction: MAME's debugger (headless, -debug -debugger
# none) traces it from the end of frame FROM (default 2) for SECONDS, each
# instruction's registers and the CPU's total cycles; rb_host -trace writes
# the same from the model. Both play SCRIPT's inputs (rbdump.lua's form).
# Every line must be the same. RB_CPU=sound: the sound board's 6808 the
# same way (rb_host -strace). Everything goes under $TMPDIR
# (RB_WORK). RB_ROMS is the robotron.zip to use.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../src" && pwd)
secs=${1:-3}
from=${2:-2}
script=${3:-}
[ -n "$script" ] && script=$(cd "$(dirname "$script")" && pwd)/$(basename "$script")
work=${RB_WORK:-${TMPDIR:-/tmp}/robotron-test}
roms=${RB_ROMS:?RB_ROMS is the robotron.zip to use}
out="$work/trace"
rm -rf "${out:?}"; mkdir -p "$out"
cc -O2 -Wall -DRB_TRACE -I"$src" -o "$out/rb_host" "$here/rb_host.c" "$src/rb.c" "$src/rbroms.c" "$src/swzip.c"
if [ "${RB_CPU:-main}" = sound ]; then
    "$out/rb_host" "$roms" -frames $(( from + secs * 60 + 10 )) -strace "$out/model.tr" -tracefrom $(( from - 1 )) \
        ${script:+-script "$script"} 2>/dev/null
    cpu=:soundcpu
    fmt='"@ %04X %02X %02X %04X %04X %02X %d\\n",pc,a,b,x,s,cc,totalcycles'
else
    "$out/rb_host" "$roms" -frames $(( from + secs * 60 + 10 )) -trace "$out/model.tr" -tracefrom "$from" \
        ${script:+-script "$script"} 2>/dev/null
    cpu=:maincpu
    fmt='"@ %04X %02X %02X %04X %04X %04X %04X %02X %02X %d\\n",pc,a,b,x,y,u,s,dp,cc,totalcycles'
fi

cat > "$out/trace.lua" <<LUA
if not _G.rb_traced then
  _G.rb_traced = true
  local ports = manager.machine.ioport.ports
  local events = {}
  local script = "$script"
  if script ~= "" then
    for line in io.lines(script) do
      local f, port, v = line:match("^(%d+)%s+(%S+)%s+(%x+)")
      if f then f = tonumber(f); events[f] = events[f] or {}; table.insert(events[f], { port, tonumber(v, 16) }) end
    end
  end
  local seen = {}
  _G.rb_sub = emu.register_frame_done(function()
    local n = math.floor(manager.machine.time:as_double() / 0.01664 + 0.5)
    if seen[n] then return end
    seen[n] = true
    for _, e in ipairs(events[n] or {}) do
      for _, f in pairs(ports[e[1]].fields) do f:set_value((e[2] & f.mask) ~= 0 and 1 or 0) end
    end
    if n ~= $from then return end
    manager.machine.debugger.visible_cpu = manager.machine.devices['$cpu']
    manager.machine.debugger:command('trace $out/mame.tr,$cpu,noloop,{tracelog $fmt}')
  end)
end
LUA
rp=$(mktemp -d "${TMPDIR:-/tmp}/rbrp.XXXXXX")
cp "$roms" "$rp/robotron.zip"
mkdir "$rp/cfg" "$rp/nvram"
SDL_VIDEODRIVER=dummy mame robotron -rompath "$rp" -debug -debugger none \
    -video none -videodriver dummy -window -nomaximize -sound none \
    -nothrottle -skip_gameinfo -seconds_to_run $(( from / 60 + secs + 1 )) \
    -cfg_directory "$rp/cfg" -nvram_directory "$rp/nvram" -snapshot_directory "$rp/snap" \
    -autoboot_delay 0 -autoboot_script "$out/trace.lua" >"$out/mame.log" 2>&1 || true
rm -rf "${rp:?}"

python3 -I - "$out" "${RB_CPU:-main}" <<'PY'
import sys
out = sys.argv[1]
def at_lines(path):
    try:
        with open(path) as f:
            return [l.split() for l in f if l.startswith("@ ")]
    except FileNotFoundError:
        return []
mame = at_lines(f"{out}/mame.tr")[:-1]
model = at_lines(f"{out}/model.tr")
if not mame:
    print("MAME wrote no trace (see mame.log)"); sys.exit(1)
start = next((i for i, l in enumerate(model) if l == mame[0]), None)
if start is None:
    print("MAME's first line isn't in the model's:", " ".join(mame[0])); sys.exit(1)
n = min(len(mame), len(model) - start)
for i in range(n):
    if mame[i] != model[start + i]:
        print(f"instruction {i} differs\n  MAME  {' '.join(mame[i])}\n  model {' '.join(model[start + i])}")
        for k in range(max(0, i - 6), i):
            print("  before", " ".join(mame[k]))
        sys.exit(1)
print(f"{sys.argv[2]} CPU: {n} instructions the same, registers and cycle counts (from cycle {mame[0][-1]})")
PY
