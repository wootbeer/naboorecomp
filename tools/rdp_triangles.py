"""Decode the triangles in an RDP command stream (from tools/rsp_reference.c)
and draw them flat, to see what the microcode produced (STATUS.md round 17).

    python3 tools/rdp_triangles.py gfx_600.rdp out.png [--list N]

Each triangle is drawn in its first vertex's shade colour (grey when it has
none). Positions come from the edge coefficients: the top vertex is (XH, YH),
the middle one is on the major edge's opposite side at YM, the bottom at YL.
"""
import argparse
import struct

from PIL import Image, ImageDraw


def s32(x):
    return x - (1 << 32) if x & 0x80000000 else x


def decode(path):
    data = open(path, "rb").read()
    words = struct.unpack(">%dQ" % (len(data) // 8), data)
    tris = []
    i = 0
    while i < len(words):
        w = words[i]
        op = (w >> 56) & 0x3F
        length = 1
        if 0x08 <= op <= 0x0F:
            length = 4 + (8 if op & 4 else 0) + (8 if op & 2 else 0) + (2 if op & 1 else 0)
            if i + length > len(words):
                break
            lft = (w >> 55) & 1
            yl = ((w >> 32) & 0x3FFF) / 4.0 if not ((w >> 45) & 1) else -((0x4000 - ((w >> 32) & 0x3FFF)) / 4.0)
            ym = (((w >> 16) & 0x3FFF) - (0x4000 if (w >> 29) & 1 else 0)) / 4.0
            yh = ((w & 0x3FFF) - (0x4000 if (w >> 13) & 1 else 0)) / 4.0
            yl = (((w >> 32) & 0x3FFF) - (0x4000 if (w >> 45) & 1 else 0)) / 4.0
            xl, dxldy = s32(words[i + 1] >> 32) / 65536.0, s32(words[i + 1] & 0xFFFFFFFF) / 65536.0
            xh, dxhdy = s32(words[i + 2] >> 32) / 65536.0, s32(words[i + 2] & 0xFFFFFFFF) / 65536.0
            xm, dxmdy = s32(words[i + 3] >> 32) / 65536.0, s32(words[i + 3] & 0xFFFFFFFF) / 65536.0
            top = (xh, yh)
            mid = (xm + dxmdy * (ym - int(yh)), ym) if ym != yh else (xm, yh)
            # The major edge runs from the top to the bottom.
            bottom = (xh + dxhdy * (yl - yh), yl)
            colour = (128, 128, 128)
            if op & 4:
                sw = words[i + 4]
                colour = tuple(max(0, min(255, (s32((sw >> 32) & 0xFFFFFFFF if k < 2 else sw & 0xFFFFFFFF) >> (16 if k in (0, 2) else 0)) & 0xFFFF)) for k in range(3))
                r = ((words[i + 4] >> 48) & 0xFFFF)
                g = ((words[i + 4] >> 32) & 0xFFFF)
                b = ((words[i + 4] >> 16) & 0xFFFF)
                colour = tuple(min(255, c & 0xFF if c > 255 else c) for c in (r, g, b))
            tris.append((op, top, mid, bottom, colour, lft))
        elif op in (0x24, 0x25):
            length = 2
        i += length
    return tris


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rdp")
    ap.add_argument("png")
    ap.add_argument("--list", type=int, default=0)
    ap.add_argument("--width", type=int, default=400)
    ap.add_argument("--height", type=int, default=448)
    args = ap.parse_args()
    tris = decode(args.rdp)
    print(f"{len(tris)} triangles")
    for t in tris[:args.list]:
        print(t)
    img = Image.new("RGB", (args.width, args.height), (0, 0, 0))
    draw = ImageDraw.Draw(img)
    for op, top, mid, bottom, colour, lft in tris:
        draw.polygon([top, mid, bottom], fill=colour, outline=None)
    img.save(args.png)


if __name__ == "__main__":
    main()
