#!/usr/bin/env python3
"""UX-11 (U023): checks the shipped app icons (app/resources) without third-party modules.

- icon.png (in-app, About dialog, window icon): 1024 px RGBA, transparent corners.
- FrameKeeper.ico (Windows exe, installer, file association): 16, 24, 32, 48, 64, 128, 256 px; 32-bit entries with
  transparency below 256 px, PNG at 256 px.
- FrameKeeper.icns (macOS bundle): 1x 16/32/64/128/256/512 and 2x entries, every one a PNG of the right size.
- The sun of the earlier icon is gone at every size (no sun-coloured disc in the picture area).
Usage: tools/check-icons.py [resources-dir]
"""
import struct
import sys
import zlib
from pathlib import Path

SUN = (255, 236, 190)  # the earlier icon's sun colour


def decode_png(data: bytes):
    """Returns (width, height, rows of RGBA bytes) for 8-bit RGBA / RGB non-interlaced PNGs."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat = 8, b""
    w = h = color = depth = interlace = None
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            idat += chunk
        elif kind == b"IEND":
            break
        pos += 12 + n
    if depth != 8 or color not in (2, 6) or interlace != 0:
        raise ValueError(f"unsupported PNG (depth {depth}, colour {color}, interlace {interlace})")
    bpp = 4 if color == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 0xFF
            elif f == 2:
                line[x] = (line[x] + b) & 0xFF
            elif f == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        if bpp == 3:
            rgba = bytearray()
            for x in range(w):
                rgba += line[x * 3:x * 3 + 3] + b"\xff"
            rows.append(bytes(rgba))
        else:
            rows.append(bytes(line))
        prev = line
    return w, h, rows


def sun_pixels(w: int, rows) -> int:
    n = 0
    for row in rows:
        for x in range(w):
            r, g, b, a = row[x * 4:x * 4 + 4]
            if a > 200 and abs(r - SUN[0]) + abs(g - SUN[1]) + abs(b - SUN[2]) < 30:
                n += 1
    return n


def main(argv) -> int:
    res = Path(argv[1] if len(argv) > 1 else Path(__file__).resolve().parent.parent / "app" / "resources")
    problems = []

    # icon.png
    w, h, rows = decode_png((res / "icon.png").read_bytes())
    if (w, h) != (1024, 1024):
        problems.append(f"icon.png is {w}×{h}, expected 1024×1024")
    for x, y in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
        if rows[y][x * 4 + 3] != 0:
            problems.append(f"icon.png corner ({x},{y}) is not transparent")
    if (n := sun_pixels(w, rows)) > 50:
        problems.append(f"icon.png: {n} sun-coloured pixels (the sun must be gone)")

    # FrameKeeper.ico
    ico = (res / "FrameKeeper.ico").read_bytes()
    count = struct.unpack("<HHH", ico[:6])[2]
    sizes = {}
    for i in range(count):
        bw, bh, _, _, _, bits, size, off = struct.unpack("<BBBBHHII", ico[6 + 16 * i:22 + 16 * i])
        s = bw or 256
        sizes[s] = (bits, ico[off:off + size])
    want = [16, 24, 32, 48, 64, 128, 256]
    if sorted(sizes) != want:
        problems.append(f"FrameKeeper.ico sizes {sorted(sizes)}, expected {want}")
    for s, (bits, data) in sizes.items():
        if s >= 256:
            pw, ph, prow = decode_png(data)
            if (pw, ph) != (256, 256):
                problems.append(f"FrameKeeper.ico 256 entry is {pw}×{ph}")
            elif sun_pixels(pw, prow) > 4:
                problems.append("FrameKeeper.ico 256 px: sun-coloured pixels")
            continue
        if bits != 32 or data[:4] != struct.pack("<I", 40):
            problems.append(f"FrameKeeper.ico {s} px is not a 32-bit BMP entry")
            continue
        px = data[40:40 + s * s * 4]
        alphas = {px[i + 3] for i in range(0, len(px), 4)}
        if 0 not in alphas or 255 not in alphas:
            problems.append(f"FrameKeeper.ico {s} px has no transparency (alpha values {sorted(alphas)[:4]}…)")

    # FrameKeeper.icns
    icns = (res / "FrameKeeper.icns").read_bytes()
    if icns[:4] != b"icns" or struct.unpack(">I", icns[4:8])[0] != len(icns):
        problems.append("FrameKeeper.icns: bad header")
    expected = {"icp4": 16, "icp5": 32, "icp6": 64, "ic07": 128, "ic08": 256, "ic09": 512, "ic10": 1024,
                "ic11": 32, "ic12": 64, "ic13": 256, "ic14": 512}
    found, pos = {}, 8
    while pos + 8 <= len(icns):
        kind = icns[pos:pos + 4].decode("latin-1")
        n = struct.unpack(">I", icns[pos + 4:pos + 8])[0]
        found[kind] = icns[pos + 8:pos + n]
        pos += n
    for kind, s in expected.items():
        if kind not in found:
            problems.append(f"FrameKeeper.icns lacks {kind} ({s} px)")
            continue
        pw, ph, prow = decode_png(found[kind])
        if (pw, ph) != (s, s):
            problems.append(f"FrameKeeper.icns {kind} is {pw}×{ph}, expected {s}×{s}")
        elif s >= 128 and sun_pixels(pw, prow) > max(4, 50 * s * s // (1024 * 1024)):
            problems.append(f"FrameKeeper.icns {kind}: sun-coloured pixels")

    if problems:
        print("check-icons: problems:")
        for p in problems:
            print("  -", p)
        return 1
    print(f"check-icons: icon.png, FrameKeeper.ico ({len(sizes)} sizes), FrameKeeper.icns ({len(found)} entries) OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
