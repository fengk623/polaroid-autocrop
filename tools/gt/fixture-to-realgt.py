#!/usr/bin/env python3
"""Converts fkfixture ground truth (fk.fixture-gt/1) into the real-scan GT format (fk.realgt/1), so that
`fkcli realeval` can be tested end to end without private scans (tests/realeval_fixture.cmake).
Usage: fixture-to-realgt.py <scan.tif> <scan.json> <out.gt.json>"""
import hashlib
import json
import sys


def main():
    scan, gt, out = sys.argv[1:4]
    g = json.load(open(gt, encoding="utf-8"))
    h = hashlib.sha256(open(scan, "rb").read()).hexdigest()
    prints = []
    for p in g["prints"]:
        inner = p["inner"]
        if inner.get("kind") == "ellipse":
            inner = {"kind": "ellipse", "cx": inner["center"][0], "cy": inner["center"][1], "rx": inner["rx"],
                     "ry": inner["ry"], "angle": inner.get("angle", 0.0)}
        else:
            inner = {"kind": "quad", "points": inner["points"]}
        prints.append({"id": f"P{p['index']:02d}", "outer": {"kind": "quad", "points": p["outerQuad"]},
                       "inner": inner, "uncertaintyPx": {"outer": [0.5] * 4, "inner": [0.5] * 4},
                       "notes": "generated (fkfixture)"})
    level = g.get("background", {}).get("level", 0.06)
    out_j = {"schema": "fk.realgt/1",
             "scan": {"sha256": h, "width": g["width"], "height": g["height"], "bits": g["bits"],
                      "channels": g["channels"], "dpiTagged": g["dpi"], "dpiMeasured": None,
                      "dpiConfidence": "tagged"},
             "set": "synthetic", "background": "white" if level > 0.5 else "dark",
             "coordinates": "scan pixels, origin top-left", "prints": prints,
             "annotation": {"by": "fkfixture", "reviewedBy": None}}
    json.dump(out_j, open(out, "w", encoding="utf-8"), indent=1)


if __name__ == "__main__":
    sys.exit(main())
