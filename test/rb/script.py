# script.py FRAMES [SEED] -- a game for the tests, in rbdump.lua's script
# form: the factory settings' Advance (a new CMOS waits for it), a coin, one
# player, then the stick and the fire stick turned every 10 to 40 frames,
# at random (seeded, so both sides play the same game).
import random
import sys

frames = int(sys.argv[1])
rnd = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 2084)
print("700 :IN2 02\n720 :IN2 00\n1500 :IN2 10\n1506 :IN2 00\n1560 :IN0 10\n1566 :IN0 00")
move = [0, 1, 2, 4, 8, 5, 6, 9, 10]          # none, up, down, left, right and the diagonals
fire = [(0, 0), (0x40, 0), (0x80, 0), (0, 1), (0, 2), (0x40, 1), (0x40, 2), (0x80, 1), (0x80, 2)]
f = 1600
while f < frames:
    m = rnd.choice(move)
    fi = rnd.choice(fire[1:]) if rnd.random() < 0.85 else fire[0]
    in0 = m | fi[0]
    # a coin and a start now and then, so a game over starts another
    if rnd.random() < 0.03:
        print(f"{f} :IN2 10\n{f + 6} :IN2 00")
    if rnd.random() < 0.03:
        in0 |= 0x10
    print(f"{f} :IN0 {in0:02X}\n{f} :IN1 {fi[1]:02X}")
    f += rnd.randint(10, 40)
