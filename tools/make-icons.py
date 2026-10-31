#!/usr/bin/env python3
"""Generates the FrameKeeper app icon (our own artwork) as PNG, ICO and ICNS.

Design: a tilted instant print (white frame, wide bottom border) on a deep teal tile, with teal corner
brackets marking the detected frame. UX-11 (U023): no sun — the image area is a plain warm-to-teal gradient over
a teal band, so the print and its frame are what reads at every size. Each ICO / ICNS size is rendered on its
own (not scaled down from 1024 px) with strokes kept at least ~1 px wide at the target size.
Usage: tools/make-icons.py app/resources
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


def render(size: int) -> Image.Image:
    S = 4 * size  # supersample
    px = S / size  # supersampled pixels per target pixel
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
    shadow = card.split()[3].filter(ImageFilter.GaussianBlur(S * 0.02))
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sh.putalpha(shadow.point(lambda a: int(a * 0.45)))
    card = card.rotate(-7, resample=Image.BICUBIC, center=(S / 2, S / 2))
    sh = sh.rotate(-7, resample=Image.BICUBIC, center=(S / 2 + S * 0.01, S / 2 + S * 0.02))
    img.alpha_composite(sh)
    img.alpha_composite(card)
    # corner brackets (detected outline) in teal
    b = ImageDraw.Draw(img)
    # strokes at least 1.2 target pixels wide, so the brackets stay visible at 16 / 24 / 32 px
    m, L, t = S * 0.17, S * 0.12, max(2, int(S * 0.028), int(round(1.2 * px)))
    col = (53, 181, 168, 255)
    for (cx, cy, dx, dy) in [(m, m, 1, 1), (S - m, m, -1, 1), (S - m, S - m, -1, -1), (m, S - m, 1, -1)]:
        b.line([cx, cy, cx + dx * L, cy], fill=col, width=t)
        b.line([cx, cy, cx, cy + dy * L], fill=col, width=t)
    return img.resize((size, size), Image.LANCZOS)


def write_ico(path: Path, sizes) -> None:
    """Classic ICO layout: 32-bit BMP (DIB + AND mask) entries below 256 px, PNG only for 256 px.
    Some tools (installer builders, older Windows APIs) reject PNG-compressed small entries."""
    import io
    import struct

    entries = []
    for s in sizes:
        im = render(s).convert("RGBA")
        if s >= 256:
            buf = io.BytesIO()
            im.save(buf, "PNG")
            data = buf.getvalue()
        else:
            px = im.tobytes("raw", "BGRA")
            rows = [px[y * s * 4:(y + 1) * s * 4] for y in range(s)][::-1]  # bottom-up
            mask_row = ((s + 31) // 32) * 4
            header = struct.pack("<IiiHHIIiiII", 40, s, 2 * s, 1, 32, 0, s * s * 4 + mask_row * s, 0, 0, 0, 0)
            data = header + b"".join(rows) + b"\0" * (mask_row * s)
        entries.append((s, data))
    out = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    for s, data in entries:
        out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    path.write_bytes(out + b"".join(d for _, d in entries))


def write_icns(path: Path) -> None:
    """ICNS with PNG entries for every size macOS asks for: 1× 16/32/64/128/256/512 (icp4, icp5, icp6, ic07,
    ic08, ic09) and 2× (ic11 = 16@2, ic12 = 32@2, ic13 = 128@2, ic14 = 256@2, ic10 = 512@2), each rendered on its
    own. Pillow's writer leaves out the 1× 16 and 32 px entries used by list views on 1× displays."""
    import io
    import struct

    cache = {}

    def png(size: int) -> bytes:
        if size not in cache:
            buf = io.BytesIO()
            render(size).save(buf, "PNG")
            cache[size] = buf.getvalue()
        return cache[size]

    types = [(b"icp4", 16), (b"icp5", 32), (b"icp6", 64), (b"ic07", 128), (b"ic08", 256), (b"ic09", 512),
             (b"ic11", 32), (b"ic12", 64), (b"ic13", 256), (b"ic14", 512), (b"ic10", 1024)]
    body = b"".join(t + struct.pack(">I", 8 + len(png(s))) + png(s) for t, s in types)
    path.write_bytes(b"icns" + struct.pack(">I", 8 + len(body)) + body)


def main(out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    big = render(1024)
    big.save(out / "icon.png")
    write_ico(out / "FrameKeeper.ico", [16, 24, 32, 48, 64, 128, 256])
    write_icns(out / "FrameKeeper.icns")
    print("icons written to", out)


if __name__ == "__main__":
    main(Path(sys.argv[1] if len(sys.argv) > 1 else "app/resources"))
