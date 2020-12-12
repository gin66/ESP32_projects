#!/usr/bin/env python3

import png, copy

from cffi import FFI

ffi = FFI()

with open("read.h","r") as fp:
    header = fp.read()
ffi.cdef(header)

ffi.set_source("_read",  # name of the output C extension
"""
    #include "read.h"
""",
    sources=['read.c'],   # includes pi.c as additional sources
    libraries=['m'])    # on Unix, link with the math library

ffi.compile(verbose=True)

image = ffi.new("uint8_t[]", 320*240*2);
internal = ffi.new("struct read_s *");
digitized = ffi.new("uint8_t[]", 320*240//8);

from _read import lib
lib.find_pointer(image,digitized,internal)

with open("image","rb") as fp:
    img = fp.read()

print(len(img))

find_pointer = True

img_data = []
digitized = []
i = 0
for _ in range(240):
    row = []
    dig_row = []
    img_data.append(row)
    digitized.append(dig_row)
    for _ in range(320):
        r = img[i] & 0xf8
        g = ((img[i] & 0x07) << 5) | ((img[i] & 0xe0) >> 3)
        b = (img[i+1] & 0x1f) << 3

        if not find_pointer:
            if 4*r*r > 3*g*g + 3*b*b and r > g and r > b:
                dig_row.append(1)
                if True:
                    r = 255
                    g = 255
                    b = 255
            else:
                dig_row.append(0)
                r >>= 2
                g >>= 2
                b >>= 2

        if find_pointer:
            if 4*r*r > 3*g*g + 3*b*b and r > g and r > b and r > 0x30:
                dig_row.append(1)
                if True:
                    r = 255
                    g = 255
                    b = 255
            else:
                dig_row.append(0)
                r >>= 2
                g >>= 2
                b >>= 2

        row.append(r)
        row.append(g)
        row.append(b)
        i += 2

if False:        
    f = open('image.png', 'wb')
    w = png.Writer(320, 240, greyscale=False)
    w.write(f, img_data)
    f.close()

# clear frame
for i in range(8):
    for y in range(240):
        digitized[y][i] = 0
        digitized[y][319-i] = 0
    for x in range(320):
        digitized[i][x] = 0
        digitized[239-i][x] = 0


old_digitized = copy.deepcopy(digitized)
if find_pointer:
    n = 3
    for y in range(n,240-n):
        for x in range(n,320-n):
            s = 0
            s += old_digitized[y+n][x] + old_digitized[y-n][x]
            s += old_digitized[y][x+n] + old_digitized[y][x-n] 
            s += old_digitized[y+n][x+n] + old_digitized[y-n][x-n]
            s += old_digitized[y+n][x-n] + old_digitized[y-n][x+n]
            if s < 8 :
                digitized[y][x] = 0
else:
    n = 4
    for y in range(n,240-n):
        for x in range(n,320-n):
            clearit = 0
            if digitized[y+n][x] + digitized[y-n][x] < digitized[y][x]:
                clearit += 1
            if digitized[y][x+n] + digitized[y][x-n] < digitized[y][x]:
                clearit += 1
            if digitized[y+n][x+n] + digitized[y-n][x-n] < digitized[y][x]:
                clearit += 1
            if digitized[y+n][x-n] + digitized[y-n][x+n] < digitized[y][x]:
                clearit += 1
            if clearit >= 3 :
                digitized[y][x] = 0


digitized = [[255*p for p in row] for row in digitized]

f = open('image.png', 'wb')
w = png.Writer(320, 240, greyscale=True)
w.write(f, digitized)
f.close()
