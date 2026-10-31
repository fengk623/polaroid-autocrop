#!/usr/bin/env python3
"""Drafts a real-scan ground-truth file (schema fk.realgt/1, docs/v1/REAL-SCAN-GT.md) by measuring edges on the
scan's pixels — independently of the detector. The result is a draft (`annotation.by = "agent-draft"`) that the
owner must review before it counts as evidence.

Usage:
  draft-gt.py <scan.tif> --seeds seeds.json --out <scan-sha12>.gt.json [--set tuning|regression]
              [--background white|dark|none] [--dpi MEASURED] [--overlays DIR]

`seeds.json`: {"prints": [{"id": "P01", "outer": [[x,y]×4], "inner": [[x,y]×4]}]} — rough corners (top-left,
top-right, bottom-right, bottom-left in scan coordinates); outer seeds up to 2 mm outside the paper (the plateau
must be paper), inner seeds within ±1 mm. Any source will do (a
detector run, a hand-made list); only search windows are taken from them. A side of the outer that lies on the
scan border (`--fills-scan` prints) is recorded as the scan border with "edge": "scan_border".

Edge rule (the same on every side, measured on 7-pixel-wide averaged profiles along the side's normal,
[1 2 1]-smoothed): the first gradient peak above max(6σ, 1.5 levels/px) walking from the plateau of the known
material — the paper 2.5–3.5 mm inside the seed for the outer edge, the frame 0.6–1.2 mm outside the seed for the
inner edge — with sub-pixel parabola fit. Outer: the paper-side flank of the dark edge line on white backgrounds
(the shadow band and the lid's bright band are outside), the step itself on dark ones. Inner: the window edge, also
where a light reagent band follows the frame (the band belongs to the picture). Uncertainty: half the width of the
gradient peak at half maximum.
Each side: robust line fit (two passes, residuals > 3σ dropped) over the central 80 %; corners = intersections.
`--overlays DIR` writes one review sheet per print (the middle of every side, 3× enlarged, draft edge in green). The
sheets show scan content: keep them local, never commit them.
"""
import argparse
import hashlib
import json
import math
import os
import sys

import numpy as np

try:
    import tifffile
except ImportError:  # pragma: no cover
    tifffile = None
from PIL import Image

Image.MAX_IMAGE_PIXELS = None


def load(path):
    if tifffile is not None:
        a = tifffile.imread(path)
    else:
        a = np.asarray(Image.open(path))
    a = a.astype(np.float32)
    if a.ndim == 3:
        a = a[..., :3].mean(axis=2)
    if a.max() > 255.5:
        a = a / 257.0
    return a


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def sample(a, p0, n, t0, t1, width=3):
    """Profile from p0 + n*t0 to p0 + n*t1 (1-px steps), averaged over ±width px along the edge."""
    d = np.array([-n[1], n[0]])
    ts = np.arange(t0, t1 + 1e-9, 1.0)
    out = []
    for t in ts:
        vals = []
        for k in range(-width, width + 1):
            x = p0[0] + n[0] * t + d[0] * k
            y = p0[1] + n[1] * t + d[1] * k
            xi, yi = int(math.floor(x)), int(math.floor(y))
            if 0 <= xi < a.shape[1] and 0 <= yi < a.shape[0]:
                vals.append(a[yi, xi])
        out.append(np.mean(vals) if vals else np.nan)
    return ts, np.array(out)


def first_edge(ts, v, start, step, lo_mm_px):
    """Walks from index `start` in direction `step` (+1/-1) and returns (tau, halfwidth) of the first
    significant gradient peak, or None. `lo_mm_px`: samples of the plateau used for the noise estimate."""
    sm = v.copy()
    sm[1:-1] = (v[:-2] + 2 * v[1:-1] + v[2:]) / 4
    g = np.zeros_like(sm)
    g[1:-1] = (sm[2:] - sm[:-2]) / 2
    plateau = g[lo_mm_px]
    sg = 1.4826 * np.nanmedian(np.abs(plateau - np.nanmedian(plateau))) + 1e-6
    thr = max(6 * sg, 1.5)
    i = start
    while 1 <= i < len(g) - 1:
        if abs(g[i]) > thr:
            # climb to the local maximum of |g| in the walking direction
            while 1 <= i + step < len(g) - 1 and abs(g[i + step]) >= abs(g[i]):
                i += step
            y0, y1, y2 = abs(g[i - 1]), abs(g[i]), abs(g[i + 1])
            den = y0 - 2 * y1 + y2
            off = 0.5 * (y0 - y2) / den if den != 0 else 0.0
            tau = ts[i] + max(-0.5, min(0.5, off))
            # half width at half maximum on either side
            hw = 0
            while 1 <= i - hw - 1 and abs(g[i - hw - 1]) > y1 / 2:
                hw += 1
            hw2 = 0
            while i + hw2 + 1 < len(g) - 1 and abs(g[i + hw2 + 1]) > y1 / 2:
                hw2 += 1
            return tau, max(0.5, (hw + hw2 + 1) / 2)
        i += step
    return None


def outer_edge(a, base, n, ppmm):
    """n points into the paper. The first significant gradient peak walking outward from the paper plateau
    (2.5–3.5 mm inside the seed): the paper-side flank of the dark edge line on white backgrounds, the step itself
    on dark ones. Returns (tau, uncertainty) relative to base, or None."""
    ts, v = sample(a, base, n, -2.5 * ppmm, 4.0 * ppmm)
    plateau = np.where((ts >= 2.5 * ppmm) & (ts <= 3.5 * ppmm))[0]
    if np.isnan(v[plateau]).any():
        return None
    # beyond the scan border (paper close to it): keep the samples between the border and the paper
    bad = np.where(np.isnan(v))[0]
    if len(bad):
        cut = int(bad.max()) + 1
        ts, v, plateau = ts[cut:], v[cut:], plateau - cut
    start = int(np.where(ts <= 2.5 * ppmm)[0][-1])
    return first_edge(ts, v, start, -1, plateau)


def inner_edge(a, base, n, ppmm):
    """n points into the image. The first significant gradient peak walking from the frame plateau (1.2 mm on
    the frame side) into the image: the window edge, also where a light reagent band follows the frame.
    Returns (tau, uncertainty) or None."""
    ts, v = sample(a, base, n, -1.5 * ppmm, 1.5 * ppmm)
    if np.isnan(v).any():
        return None
    plateau = np.where((ts <= -0.6 * ppmm) & (ts >= -1.2 * ppmm))[0]
    if len(plateau) < 5 or np.nanstd(v[plateau]) > 6:
        return None  # no clean frame plateau (dust, writing)
    start = int(plateau[-1])
    r = first_edge(ts, v, start, +1, plateau)
    if r is None or abs(r[0]) > 1.0 * ppmm:
        return None
    return r


def fit_line(pts):
    pts = np.array(pts)
    keep = np.ones(len(pts), bool)
    for _ in range(2):
        c = pts[keep].mean(axis=0)
        u, s, vt = np.linalg.svd(pts[keep] - c)
        d = vt[0]
        nrm = np.array([-d[1], d[0]])
        r = (pts - c) @ nrm
        sg = 1.4826 * np.median(np.abs(r[keep] - np.median(r[keep]))) + 1e-6
        keep = np.abs(r - np.median(r[keep])) <= max(3 * sg, 1.0)
    return c, d, float(np.std(r[keep])), int(keep.sum())


def intersect(c1, d1, c2, d2):
    A = np.array([d1, -d2]).T
    t = np.linalg.solve(A, c2 - c1)
    return c1 + d1 * t[0]


def measure(a, quad, ppmm, kind, fills=None):
    quad = [np.array(p, float) for p in quad]
    centre = sum(quad) / 4
    lines, unc, info = [], [], []
    for s in range(4):
        p, q = quad[s], quad[(s + 1) % 4]
        d = (q - p) / np.linalg.norm(q - p)
        n = np.array([-d[1], d[0]])
        if np.dot(centre - p, n) < 0:
            n = -n
        if kind == "outer" and fills and fills[s]:
            lines.append((p, d))
            unc.append(0.0)
            info.append({"edge": "scan_border"})
            continue
        pts, us = [], []
        L = np.linalg.norm(q - p)
        for k in range(60):
            t = 0.1 + 0.8 * (k + 0.5) / 60
            base = p + (q - p) * t
            r = outer_edge(a, base, n, ppmm) if kind == "outer" else inner_edge(a, base, n, ppmm)
            if r is None:
                continue
            pts.append(base + n * r[0])
            us.append(r[1])
        if len(pts) < 10:
            raise SystemExit(f"{kind} side {s}: only {len(pts)} measurable profiles; check the seeds")
        c, dd, sd, kept = fit_line(pts)
        if np.dot(dd, d) < 0:
            dd = -dd
        lines.append((c, dd))
        unc.append(float(np.median(us)))
        info.append({"profiles": len(pts), "kept": kept, "residualPx": round(sd, 2), "lengthPx": round(L, 1)})
    corners = [intersect(*lines[(s + 3) % 4], *lines[s]) for s in range(4)]
    return [[round(float(c[0]), 2), round(float(c[1]), 2)] for c in corners], [round(u, 2) for u in unc], info


def write_overlays(path, prints, out_dir, stem):
    """One PNG per print: the middle of each outer and inner side, 3× enlarged and contrast-stretched, with the
    drafted edge in green — what the owner looks at when reviewing the draft."""
    from PIL import ImageDraw
    os.makedirs(out_dir, exist_ok=True)
    img = Image.open(path).convert("RGB")
    for p in prints:
        tiles = []
        for kind in ("outer", "inner"):
            Q = np.array(p[kind]["points"], float)
            for s in range(4):
                m = (Q[s] + Q[(s + 1) % 4]) / 2
                r = 60
                box = (int(m[0] - r), int(m[1] - r), int(m[0] + r), int(m[1] + r))
                c = np.asarray(img.crop(box)).astype(float)
                lo, hi = np.percentile(c, 1), np.percentile(c, 99.5)
                t = Image.fromarray(np.clip((c - lo) / (hi - lo + 1e-6) * 255, 0, 255).astype("uint8"))
                t = t.resize((t.width * 3, t.height * 3), Image.NEAREST)
                d = ImageDraw.Draw(t)
                a, b = Q[s], Q[(s + 1) % 4]
                d.line([((a[0] - box[0]) * 3, (a[1] - box[1]) * 3), ((b[0] - box[0]) * 3, (b[1] - box[1]) * 3)],
                       fill=(0, 220, 0), width=1)
                d.text((4, 4), f"{kind} {['top', 'right', 'bottom', 'left'][s]}", fill=(255, 0, 255))
                tiles.append(t)
        w, h = tiles[0].width, tiles[0].height
        sheet = Image.new("RGB", (w * 4, h * 2))
        for i, t in enumerate(tiles):
            sheet.paste(t, ((i % 4) * w, (i // 4) * h))
        sheet.save(os.path.join(out_dir, f"{stem}_{p['id']}.png"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scan")
    ap.add_argument("--seeds", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--set", default="tuning", choices=["tuning", "regression"])
    ap.add_argument("--background", default="white", choices=["white", "dark", "none"])
    ap.add_argument("--dpi", type=float, default=0, help="measured DPI when the tag is wrong")
    ap.add_argument("--overlays", default="", help="directory for review images (edge crops with the draft)")
    args = ap.parse_args()
    seeds = json.load(open(args.seeds))
    img = Image.open(args.scan)
    dpiTag = float(img.info.get("dpi", (0, 0))[0] or 0)
    a = load(args.scan)
    h, w = a.shape
    dpi = args.dpi or dpiTag or 300.0
    ppmm = dpi / 25.4
    prints = []
    for sp in seeds["prints"]:
        fills = sp.get("scanBorder")  # [top, right, bottom, left] sides on the scan border
        outer, uo, io = measure(a, sp["outer"], ppmm, "outer", fills)
        inner, ui, ii = measure(a, sp["inner"], ppmm, "inner")
        prints.append({"id": sp["id"], "outer": {"kind": "quad", "points": outer},
                       "inner": {"kind": "quad", "points": inner},
                       "uncertaintyPx": {"outer": uo, "inner": ui},
                       "measurement": {"outer": io, "inner": ii},
                       "notes": sp.get("notes", "")})
    bits = 16 if (tifffile and tifffile.imread(args.scan).dtype == np.uint16) else 8
    out = {"schema": "fk.realgt/1",
           "scan": {"sha256": sha256(args.scan), "width": w, "height": h, "bits": bits,
                    "channels": 3, "dpiTagged": dpiTag, "dpiMeasured": args.dpi or None,
                    "dpiConfidence": "measured" if args.dpi else ("tagged" if dpiTag else "unknown")},
           "set": args.set, "background": args.background,
           "coordinates": "scan pixels, origin top-left, x right, y down; pixel (x, y) covers [x, x+1) x "
                          "[y, y+1); corners are continuous positions on the edge line",
           "prints": prints,
           "annotation": {"by": "agent-draft", "reviewedBy": None, "method": "tools/gt/draft-gt.py",
                          "seeds": os.path.basename(args.seeds)}}
    json.dump(out, open(args.out, "w"), indent=1)
    if args.overlays:
        write_overlays(args.scan, prints, args.overlays, os.path.splitext(os.path.basename(args.out))[0])
    print(args.out, len(prints), "prints")


if __name__ == "__main__":
    sys.exit(main())
