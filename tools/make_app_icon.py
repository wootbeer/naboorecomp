#!/usr/bin/env python3
"""
Draws the app icon (icons/app.png and icons/app.ico): a starfighter seen from
above, in amber on a dark disc. Original artwork, made only from simple shapes.

    python3 tools/make_app_icon.py [output dir, default icons]

Needs Pillow. The Windows resource icons/app.res is built from app.ico with:
    llvm-rc /FO icons/app.res <a file containing: 1 ICON "app.ico">
"""
import os
import sys

from PIL import Image, ImageDraw

S = 1024  # drawn large, scaled down for smoothness
AMBER = (240, 176, 48, 255)
AMBER_DARK = (176, 112, 24, 255)
SILVER = (214, 220, 228, 255)
NAVY = (14, 22, 44, 255)
RING = (40, 56, 96, 255)


def p(*pts):
    return [(x * S, y * S) for x, y in pts]


def draw():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse(p((0.02, 0.02), (0.98, 0.98)), fill=NAVY, outline=RING, width=int(S * 0.025))

    # Engine nacelles with their long rear spikes, either side of the body.
    for side in (-1, 1):
        cx = 0.5 + side * 0.215
        d.polygon(p((cx - 0.055, 0.42), (cx + 0.055, 0.42), (cx + 0.045, 0.70), (cx - 0.045, 0.70)), fill=AMBER)
        d.ellipse(p((cx - 0.055, 0.36), (cx + 0.055, 0.48)), fill=AMBER)
        d.polygon(p((cx - 0.02, 0.70), (cx + 0.02, 0.70), (cx, 0.90)), fill=SILVER)
        # Wing joining nacelle and body.
        x0, x1 = (0.5, cx) if side > 0 else (cx, 0.5)
        d.polygon(p((x0, 0.50), (x1, 0.47), (x1, 0.58), (x0, 0.60)), fill=AMBER_DARK)

    # Body: a long tapered needle nose to the front.
    d.polygon(p((0.5, 0.08), (0.545, 0.40), (0.56, 0.62), (0.5, 0.70), (0.44, 0.62), (0.455, 0.40)), fill=AMBER)
    # Cockpit and a silver stripe down the spine.
    d.ellipse(p((0.475, 0.40), (0.525, 0.50)), fill=SILVER)
    d.polygon(p((0.5, 0.16), (0.51, 0.38), (0.49, 0.38)), fill=SILVER)
    return img


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "icons")
    os.makedirs(out, exist_ok=True)
    img = draw()
    img.resize((256, 256), Image.LANCZOS).save(os.path.join(out, "app.png"))
    img.resize((256, 256), Image.LANCZOS).save(os.path.join(out, "app.ico"),
                                               sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])


if __name__ == "__main__":
    main()
