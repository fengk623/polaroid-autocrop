# Project, sidecar and template formats

All files: UTF-8 JSON, LF, 2-space indent, keys in lower camelCase. Unknown keys are preserved on save
(forward compatibility within a major schema). Coordinates follow RFC-0002.

## 1. Project `*.fkproj` — schema `fk.project/1`
```json
{
  "schema": "fk.project/1",
  "projectId": "uuid", "name": "Summer 2026", "createdAt": "ISO-8601", "modifiedAt": "ISO-8601",
  "app": {"name": "FrameKeeper", "version": "0.5.0"},
  "settings": {"expectedCount": 4, "safetyMarginPx": 1.0, "exportDefaults": {}, "colorPolicy": "keep"},
  "scans": [{
    "scanId": "uuid", "path": "/abs/path/scan01.tif", "relPath": "../scans/scan01.tif",
    "fingerprint": {"size": 0, "mtime": "ISO-8601", "quickSha256": "hex", "fullSha256": null},
    "info": {"width": 0, "height": 0, "channels": 3, "bits": 16, "compression": "lzw",
             "dpiX": 1600, "dpiY": 1600, "iccDescription": "…", "iccSha256": "hex"},
    "detection": {"at": "ISO-8601", "params": {}, "warnings": [], "proxyScale": 7.0},
    "items": [{
      "itemId": "uuid", "index": 1, "label": "P01", "filmType": "polaroid_600",
      "outer": {"kind": "quad", "points": [[x,y],[x,y],[x,y],[x,y]], "source": "auto", "cornerRadiusPx": 0},
      "inner": {"kind": "ellipse", "center": [x,y], "rx": 0, "ry": 0, "angle": 0, "source": "manual"},
      "upright": 0, "confidence": 0.93, "reasons": [], "reviewed": false, "excluded": false,
      "edits": [{"editVersionId": "uuid", "version": 1, "path": "…_P01_EDIT_v001.tif",
                 "sidecar": "…_P01_EDIT_v001.fk.json", "exportedAt": "ISO-8601",
                 "relinked": {"path": "…", "sha256": "hex", "matchedBy": "xmp", "at": "ISO-8601"}}],
      "composite": {"maskMode": "hard_edge", "featherPx": 0, "preserveEdgePx": 0, "strokes": [],
                    "lastOutput": null},
      "history": {"undo": [], "redo": []}
    }]
  }]
}
```
Save: write `name.fkproj.tmp-<rand>` → flush+fsync → rename over `name.fkproj`; previous file copied to
`name.fkproj.bak` first. Load failure → offer `.bak`.

## 2. Migrations
`v1` is the first schema. Each future version adds `migrate_vN_to_vN1(json)` and a fixture file
`tests/contract/data/project_vN.fkproj`.

## 3. Template `*.fktemplate.json` — schema `fk.template/1`
```json
{"schema": "fk.template/1", "templateId": "uuid", "name": "4-up jig", "createdDpi": 1200,
 "scanSize": [10200, 14040],
 "slots": [{"index": 1, "filmType": "polaroid_600",
            "outer": [[u,v],[u,v],[u,v],[u,v]], "inner": {"kind": "quad", "points": [[u,v],…]}}]}
```
`u,v` are fractions of the scan width/height.

## 4. Sidecar `*.fk.json` — schema `fk.sidecar/1`
```json
{"schema": "fk.sidecar/1", "projectId": "uuid", "scanId": "uuid",
 "scanFingerprint": {"size": 0, "quickSha256": "hex"}, "scanFile": "scan01.tif",
 "itemId": "uuid", "editVersionId": "uuid", "version": 1, "label": "P01",
 "inner": {}, "outer": {}, "geometry": {"mode": "pixel_crop", "upright": 1, "angle": 0.0, "scale": 1.0,
   "outToScan": [9 numbers], "scanToOut": [9 numbers], "sourceRect": [x,y,w,h]},
 "sourceSize": [w,h], "exportSize": [w,h], "bits": 16, "channels": 3,
 "icc": {"description": "…", "sha256": "hex"}, "dpi": [1600, 1600],
 "file": {"name": "…_P01_EDIT_v001.tif", "sha256": "hex"},
 "signature": "base64 of 4096 float32",
 "software": "FrameKeeper 0.5.0", "exportedAt": "ISO-8601"}
```

## 5. XMP written into exports
Namespace `fk` = `https://github.com/fengk623/polaroid-autocrop/ns/1.0/`:
`fk:ProjectID`, `fk:ScanID`, `fk:ItemID`, `fk:EditVersionID`, `fk:Version`, `fk:FrameMode`
(`real_scan` | `synthetic` | `image_area`), plus `xmpMM:DocumentID = uuid:<editVersionId>`,
`xmpMM:InstanceID = uuid:<random>`.
