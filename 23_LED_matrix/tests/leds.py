#!/usr/bin/env python3

panels = [[0 for _ in range(32)] for _ in range(32)]

for p in range(4):
    rotated = p % 2 == 0
    for x in range(32):
        for y in range(8):
            yi = y
            xi = x
            if rotated:
                xi = 31 - xi
                yi = 7 - yi
            if xi % 2 == 1:
                yi = 7 - yi
            index = p * 32 * 8
            index += xi * 8
            index += yi
            panels[y + p * 8][x] = index

for row in panels:
    print(row)

with open("leds.h", "w") as fp:
    fp.write("extern int leds[32][32];\n")

with open("leds.cpp", "w") as fp:
    fp.write("int leds[32][32] = {\n")
    for y in range(32):
        fp.write("   {" + ",".join(["%d" % i for i in panels[y]]) + "}")
        if y < 31:
            fp.write(",")
        fp.write("\n")
    fp.write("}\n")
