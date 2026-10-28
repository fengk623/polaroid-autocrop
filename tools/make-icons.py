#!/usr/bin/env python3
"""Generates the FrameKeeper app icon (our own artwork) as PNG, ICO and ICNS.

Design: a tilted instant print (white frame, wide bottom border) on a deep teal tile, with teal corner
brackets marking the detected frame. Usage: tools/make-icons.py app/resources
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


def render(size: int) -> Image.Image:
    S = 4 * size  # supersample
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = int(S * 0.22)
    d.rounded_rectangle([int(S * 0.04), int(S * 0.04), int(S * 0.96), int(S * 0.96)], r, fill=(18, 52, 55, 255))
    # print card (portrait 88×107) with shadow
    card = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    cd = ImageDraw.Draw(card)
    w, h = S * 0.50, S * 0.61
    x0, y0 = (S - w) / 2, (S - h) / 2 - S * 0.01
    cd.rounded_rectangle([x0, y0, x0 + w, y0 + h], int(S * 0.012), fill=(246, 244, 238, 255))
    # image area: warm gradient sky over a teal sea
    ix0, iy0 = x0 + w * 0.06, y0 + w * 0.07
    iw = w * 0.88
    for i in range(int(iw)):
        t = i / iw
        col = (int(250 - 40 * t), int(170 + 20 * t), int(120 + 70 * t), 255)
        cd.line([ix0, iy0 + i, ix0 + iw, iy0 + i], fill=col)
    cd.rectangle([ix0, iy0 + iw * 0.62, ix0 + iw, iy0 + iw], fill=(40, 120, 130, 255))
    cd.ellipse([ix0 + iw * 0.58, iy0 + iw * 0.30, ix0 + iw * 0.80, iy0 + iw * 0.52], fill=(255, 236, 190, 255))
    shadow = card.split()[3].filter(ImageFilter.GaussianBlur(S * 0.02))
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sh.putalpha(shadow.point(lambda a: int(a * 0.45)))
    card = card.rotate(-7, resample=Image.BICUBIC, center=(S / 2, S / 2))
    sh = sh.rotate(-7, resample=Image.BICUBIC, center=(S / 2 + S * 0.01, S / 2 + S * 0.02))
    img.alpha_composite(sh)
    img.alpha_composite(card)
    # corner brackets (detected outline) in teal
    b = ImageDraw.Draw(img)
    m, L, t = S * 0.17, S * 0.12, max(2, int(S * 0.028))
    col = (53, 181, 168, 255)
    for (cx, cy, dx, dy) in [(m, m, 1, 1), (S - m, m, -1, 1), (S - m, S - m, -1, -1), (m, S - m, 1, -1)]:
        b.line([cx, cy, cx + dx * L, cy], fill=col, width=t)
        b.line([cx, cy, cx, cy + dy * L], fill=col, width=t)
    return img.resize((size, size), Image.LANCZOS)


def main(out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    big = render(1024)
    big.save(out / "icon.png")
    big.save(out / "FrameKeeper.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    big.save(out / "FrameKeeper.icns")
    print("icons written to", out)


if __name__ == "__main__":
    main(Path(sys.argv[1] if len(sys.argv) > 1 else "app/resources"))
