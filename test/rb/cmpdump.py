# cmpdump.py MAME.DUMP MODEL.DUMP -- rbdump.lua's records against rb_host's,
# frame by frame: the RAM, the palette, the CMOS.
#
# MAME's frame_done can catch its CPU in the middle of an instruction (its
# 6809 stops at any bus cycle), where the model always finishes the
# instruction first: a frame can differ by the few bytes that one
# instruction writes, and the next frame is the same again. Frames are
# periodic, so two frames running can catch it so (an interrupt's pushes
# on the stack, other values each time). A byte that differs three frames
# running is a real difference: that's a failure, with the first frame and
# where it is.
import sys

REC = 8 + 0xC000 + 16 + 0x400

def frames(path):
    with open(path, "rb") as f:
        d = f.read()
    out = {}
    for at in range(0, len(d) - REC + 1, REC):
        r = d[at:at + REC]
        if r[:4] != b"RBFR":
            break
        out[int.from_bytes(r[4:8], "little")] = r[8:]
    return out

def where(i):
    return f"RAM {i:04X}" if i < 0xC000 else f"palette {i - 0xC000}" if i < 0xC010 else f"CMOS {i - 0xC010:03X}"

mame, model = frames(sys.argv[1]), frames(sys.argv[2])
common = sorted(set(mame) & set(model))
if not common:
    print("no frames in common"); sys.exit(1)
blips = twice = big = 0
prev2, prev = set(), set()
for n in common:
    a, b = mame[n], model[n]
    diffs = set() if a == b else {i for i in range(len(a)) if a[i] != b[i]}
    if diffs and prev2 & prev & diffs:
        first = min(prev2 & prev & diffs)
        print(f"frames {n - 2} to {n} differ: {len(diffs)} bytes, {where(first)} in each "
              f"(MAME {mame[n - 2][first]:02X}, model {model[n - 2][first]:02X} first); " + ", ".join(where(i) for i in sorted(diffs)[:8]))
        sys.exit(1)
    if diffs:
        blips += 1
        twice += bool(prev & diffs)
        big = max(big, len(diffs))
    prev2, prev = prev, diffs
print(f"{len(common)} frames the same: RAM, palette and CMOS (frames {common[0]} to {common[-1]}; "
      f"{blips} caught MAME's CPU in an instruction, {twice} of them the frame after another, {big} bytes at most)")
