#!/usr/bin/env python3
"""FK-012 (U026): independent decode of the Linear DNG that FrameKeeper exports.

LibRaw (rawpy, not our code) decodes the DNG; the reference is the 16-bit TIFF export of the same image area from
the same generated scans (seed 5: two round prints, seed 8: two Polaroid 600). Two things are checked:

1. Pixel data (gate): LibRaw's camera-space output (no colour matrix, unit white balance, no auto brightness, no
   maximum adjustment, gamma 1) equals the TIFF linearised with the sRGB curve (the scans carry an sRGB profile, so
   the DNG's camera space is linear sRGB): per channel gain within 0.5 %, correlation >= 0.9999, mean absolute error
   after the gain <= 0.1 % of full scale. This covers layout, size, orientation, white and black level and the
   linearisation.
2. Colour matrix (gate): the ColorMatrix1 LibRaw reads equals the inverse of the scan profile's colorants (D50, as
   written, CalibrationIlluminant1 = D50) within 1e-4.

Reported, not a gate: LibRaw's own sRGB rendering. LibRaw treats a DNG colour matrix as if it were relative to
D65 and ignores CalibrationIlluminant1, so its colours differ slightly from a DNG-conformant reader's (Adobe Camera
Raw / Lightroom); those readers still need checking with the real software (docs/v1/FORMAT-MATRIX.md).

Round image areas: the TIFF is transparent outside the circle and the DNG keeps the scanned corners (it has no
alpha), so only the TIFF's opaque pixels are compared.

Usage: dng-interop.py --fkcli PATH --fkfixture PATH --out DIR [--json report.json]
Needs: numpy, rawpy (LibRaw), tifffile + imagecodecs (LZW). CI installs them; not product dependencies.
"""
import argparse
import glob
import json
import os
import struct
import subprocess
import sys


def srgb_to_linear(v):
    import numpy as np

    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def icc_colorants(icc: bytes):
    """rXYZ, gXYZ, bXYZ of an ICC profile as the columns of a 3×3 matrix (PCS XYZ, D50)."""
    count = struct.unpack(">I", icc[128:132])[0]
    tags = {}
    for i in range(count):
        sig, off, size = struct.unpack(">4sII", icc[132 + 12 * i:144 + 12 * i])
        tags[sig] = icc[off:off + size]

    def xyz(sig):
        d = tags[sig]
        return [struct.unpack(">i", d[8 + 4 * k:12 + 4 * k])[0] / 65536.0 for k in range(3)]

    r, g, b = xyz(b"rXYZ"), xyz(b"gXYZ"), xyz(b"bXYZ")
    return [[r[k], g[k], b[k]] for k in range(3)]


def compare(ref, dec, mask):
    import numpy as np

    out = []
    for c in range(3):
        x, y = ref[..., c][mask], dec[..., c][mask]
        gain = float((x * y).sum() / max((x * x).sum(), 1e-12))
        corr = float(np.corrcoef(x, y)[0, 1])
        mae = float(np.abs(y / gain - x).mean())
        out.append({"gain": round(gain, 5), "correlation": round(corr, 6), "maeAfterGain": round(mae, 6)})
    return out


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
        with tifffile.TiffFile(tif) as t:
            img = t.pages[0].asarray().astype(np.float64) / 65535.0
            icc_tag = t.pages[0].tags.get(34675)
            icc = bytes(icc_tag.value) if icc_tag is not None else b""
        opaque = img[..., 3] >= 1.0 if img.shape[-1] == 4 else np.ones(img.shape[:2], dtype=bool)
        ref = srgb_to_linear(img[..., :3])
        entry["comparedPixels"] = int(opaque.sum())
        with rawpy.imread(dng) as raw:
            entry["libraw"] = {"color_desc": raw.color_desc.decode("latin-1", "replace"),
                               "num_colors": int(raw.num_colors),
                               "sizes": [int(raw.sizes.width), int(raw.sizes.height)],
                               "white_level": int(raw.white_level),
                               "black_level_per_channel": [int(v) for v in raw.black_level_per_channel],
                               "camera_whitebalance": [round(float(v), 5) for v in raw.camera_whitebalance]}
            cam_xyz = np.array(raw.rgb_xyz_matrix, dtype=np.float64)[:3, :3]
            camera = raw.postprocess(output_color=rawpy.ColorSpace.raw, gamma=(1, 1), no_auto_bright=True,
                                     output_bps=16, user_wb=[1.0, 1.0, 1.0, 1.0], adjust_maximum_thr=0.0,
                                     user_flip=0)
        with rawpy.imread(dng) as raw:
            rendered = raw.postprocess(output_color=rawpy.ColorSpace.sRGB, gamma=(1, 1), no_auto_bright=True,
                                       output_bps=16, use_camera_wb=True, adjust_maximum_thr=0.0, user_flip=0)
        dec = camera.astype(np.float64) / 65535.0
        if dec.shape != ref.shape:
            entry["error"] = f"size {dec.shape} vs TIFF {ref.shape}"
            ok = False
            report["files"].append(entry)
            continue
        entry["pixelData"] = compare(ref, dec, opaque)
        data_ok = all(abs(c["gain"] - 1) <= 0.005 and c["correlation"] >= 0.9999 and c["maeAfterGain"] <= 0.001
                      for c in entry["pixelData"])
        entry["pixelDataOk"] = data_ok
        if icc:
            expected = np.linalg.inv(np.array(icc_colorants(icc)))
            err = float(np.abs(cam_xyz - expected).max())
            entry["colorMatrix"] = {"read": np.round(cam_xyz, 5).tolist(), "expected": np.round(expected, 5).tolist(),
                                    "maxError": round(err, 7)}
            matrix_ok = err <= 1e-4
        else:
            entry["colorMatrix"] = {"error": "the TIFF reference has no ICC profile"}
            matrix_ok = False
        entry["colorMatrixOk"] = matrix_ok
        entry["librawSrgbRendering"] = compare(ref, rendered.astype(np.float64) / 65535.0, opaque)
        ok = ok and data_ok and matrix_ok
        report["files"].append(entry)
    report["ok"] = ok and bool(report["files"])
    text = json.dumps(report, indent=1)
    print(text)
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            f.write(text + "\n")

    def fmt(chs):
        return "; ".join(f"{'RGB'[i]} gain {c['gain']} corr {c['correlation']} MAE {c['maeAfterGain']}"
                         for i, c in enumerate(chs))

    for e in report["files"]:
        if "error" in e:
            print(f"::error title=FK-012 Linear DNG ({e['dng']})::{e['error']}")
            continue
        m = e["colorMatrix"]
        print(f"::notice title=FK-012 LibRaw decode of {e['dng']}::pixel data "
              f"{'OK' if e['pixelDataOk'] else 'OUT OF TOLERANCE'} ({fmt(e['pixelData'])}); colour matrix "
              f"{'OK' if e['colorMatrixOk'] else 'DIFFERS'} (max error {m.get('maxError', m.get('error'))}); "
              f"LibRaw sRGB rendering, not a gate: {fmt(e['librawSrgbRendering'])}; "
              f"white {e['libraw']['white_level']}, black {e['libraw']['black_level_per_channel'][:3]}, "
              f"camera WB {e['libraw']['camera_whitebalance'][:3]}")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
