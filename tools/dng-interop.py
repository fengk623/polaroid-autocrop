#!/usr/bin/env python3
"""FK-012 (U026): independent decode of the Linear DNG that FrameKeeper exports.

Decodes the DNG with LibRaw (rawpy, not our code) and compares it with the 16-bit TIFF export of the same image
area, both from the same generated scans (seed 5: two round prints, seed 8: two Polaroid 600). LibRaw is asked for linear sRGB (gamma 1, no auto brightness, the DNG's
own neutral), the TIFF is linearised with the sRGB curve (the generated scan carries an sRGB profile). Passes when
every channel matches: gain within 5 %, correlation ≥ 0.995, mean absolute error after the gain ≤ 1 % of full scale.
Round image areas: the TIFF is transparent outside the circle and the DNG keeps the scanned corners (it has no
alpha), so only the TIFF's opaque pixels are compared.

Usage: dng-interop.py --fkcli PATH --fkfixture PATH --out DIR [--json report.json]
Needs: numpy, rawpy (LibRaw), tifffile + imagecodecs (LZW). CI installs them; not product dependencies.
"""
import argparse
import glob
import json
import os
import subprocess
import sys


def srgb_to_linear(v):
    import numpy as np

    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fkcli", required=True)
    ap.add_argument("--fkfixture", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--json")
    a = ap.parse_args()

    import numpy as np
    import rawpy
    import tifffile

    os.makedirs(a.out, exist_ok=True)
    for seed in (5, 8):
        scan = os.path.join(a.out, f"scan{seed}.tif")
        subprocess.run([a.fkfixture, "--out", scan, "--dpi", "300", "--count", "2", "--seed", str(seed), "--bits",
                        "16"], check=True, stdout=subprocess.DEVNULL)
        for fmt, extra in (("dng", []), ("tif", ["--bits", "16"])):
            subprocess.run([a.fkcli, "export", scan, "--out-dir", os.path.join(a.out, fmt), "--what", "image",
                            "--mode", "crop", "--format", fmt] + extra, check=True, stdout=subprocess.DEVNULL)
    report = {"decoder": f"LibRaw {rawpy.libraw_version} via rawpy {rawpy.__version__}", "files": []}
    ok = True
    for dng in sorted(glob.glob(os.path.join(a.out, "dng", "*.dng"))):
        tif = os.path.join(a.out, "tif", os.path.basename(dng)[:-4] + ".tif")
        entry = {"dng": os.path.basename(dng)}
        img = tifffile.imread(tif).astype(np.float64) / 65535.0
        opaque = img[..., 3] >= 1.0 if img.shape[-1] == 4 else np.ones(img.shape[:2], dtype=bool)
        ref = srgb_to_linear(img[..., :3])
        entry["comparedPixels"] = int(opaque.sum())
        with rawpy.imread(dng) as raw:
            entry["libraw"] = {"color_desc": raw.color_desc.decode("latin-1", "replace"),
                               "num_colors": int(raw.num_colors), "sizes": [int(raw.sizes.width), int(raw.sizes.height)]}
            rgb = raw.postprocess(gamma=(1, 1), no_auto_bright=True, output_bps=16, use_camera_wb=True,
                                  output_color=rawpy.ColorSpace.sRGB, user_flip=0)
        dec = rgb.astype(np.float64) / 65535.0
        if dec.shape != ref.shape:
            entry["error"] = f"size {dec.shape} vs TIFF {ref.shape}"
            ok = False
            report["files"].append(entry)
            continue
        channels = []
        for c in range(3):
            x, y = ref[..., c][opaque], dec[..., c][opaque]
            gain = float((x * y).sum() / max((x * x).sum(), 1e-12))
            corr = float(np.corrcoef(x, y)[0, 1])
            mae = float(np.abs(y / gain - x).mean())
            good = abs(gain - 1) <= 0.05 and corr >= 0.995 and mae <= 0.01
            ok = ok and good
            channels.append({"gain": round(gain, 4), "correlation": round(corr, 5), "maeAfterGain": round(mae, 5),
                             "ok": good})
        entry["channels"] = channels
        report["files"].append(entry)
    report["ok"] = ok and bool(report["files"])
    text = json.dumps(report, indent=1)
    print(text)
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            f.write(text + "\n")
    for e in report["files"]:
        for i, c in enumerate(e.get("channels", [])):
            print(f"::notice title=FK-012 Linear DNG decoded by LibRaw ({e['dng']}, channel {'RGB'[i]})::"
                  f"gain {c['gain']}, correlation {c['correlation']}, MAE after gain {c['maeAfterGain']}"
                  f"{'' if c['ok'] else ' — OUT OF TOLERANCE'}")
        if "error" in e:
            print(f"::error title=FK-012 Linear DNG ({e['dng']})::{e['error']}")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
