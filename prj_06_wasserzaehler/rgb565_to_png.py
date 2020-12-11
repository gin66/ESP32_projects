#!/usr/bin/env python3

import png

with open("image","rb") as fp:
    img = fp.read()

print(len(img))

img_data = []
i = 0
for _ in range(240):
    row = []
    img_data.append(row)
    for _ in range(320):
        r = img[i] & 0xf8
        g = ((img[i] & 0x07) << 5) | ((img[i] & 0xe0) >> 3)
        b = (img[i+1] & 0x1f) << 3

        if True:
            if 4*r*r > 3*g*g + 3*b*b and r > g and r > b:
                if True:
                    r = 255
                    g = 255
                    b = 255
            else:
                r >>= 2
                g >>= 2
                b >>= 2

        row.append(r)
        row.append(g)
        row.append(b)
        i += 2

        
f = open('image.png', 'wb')
w = png.Writer(320, 240, greyscale=False)
w.write(f, img_data)
f.close()
