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
                 "sidecar": "", "exportedAt": "ISO-8601",
                 "relinked": {"path": "…", "sha256": "hex", "matchedBy": "file_hash+embedded_ids+content",
                              "at": "ISO-8601",
                              "transform": {"rotate": 2, "rotateSource": "content", "scale": false,
                                            "lowerDepth": false, "contentScore": 0.97, "gradedSize": [w, h],
                                            "iccSource": "export", "iccSha256": "hex"}}}],
      "composite": {"maskMode": "hard_edge", "featherPx": 0, "preserveEdgePx": 0, "strokes": [],
                    "lastOutput": null},
      "history": {"undo": [], "redo": []}
    }]
  }],
  "exportRecords": [{"schema": "fk.sidecar/1", "editVersionId": "uuid", "…": "same fields as §4"}],
  "iccProfiles": [{"sha256": "hex", "description": "Adobe RGB (1998)…", "data": "base64 of the ICC bytes"}]
}
```
`exportRecords` (since 0.6.0-alpha.3, UX-07): one record per export, with exactly the fields of a sidecar (§4):
the exported geometry snapshot, sizes, bit depth, ICC, file name and SHA-256, content signature. Relinking takes
the geometry from here, so no `.fk.json` is needed beside the graded file. `edits[].sidecar` is empty unless the
portable sidecar option is on. Records of a schema this version cannot read are kept verbatim.
Compatibility: `exportRecords` is a new **top-level** key in the same schema `fk.project/1`. Versions before
0.6.0-alpha.3 preserve unknown top-level keys verbatim, so they open, edit and save such projects without losing the
records (they just do not use them). Opening an older project imports the readable sidecar files of its exports
into `exportRecords` (saved with the next save), so those exports keep relinking after their sidecars are gone.
`edits[].relinked.transform` (since 0.6.0-alpha.5, FK-002): the transform the user confirmed when relinking —
`rotate` = 90° clockwise turns from the export to the graded file (undone when compositing), `rotateSource`
none/content/user, `scale` (resampling to the export size confirmed), `lowerDepth`, `contentScore`, `gradedSize`.
Preview and output use it as stored. Absent (relinks of earlier versions; earlier versions drop it on save): the
turn is taken from the picture only when unambiguous. `matchedBy` lists the supporting evidence since 0.6.0-alpha.4
(older values: `xmp`, `sidecar`, `file_name`).
`fingerprint.fullSha256` (FK-007): written at import since 0.6.0-alpha.7 (added when a scan of an older project is
opened); with it every change of the scan file is detected before output. `transform.gradedIccSha256` (since
0.6.0-alpha.7): the graded file's embedded profile at relink, to tell a re-grade from a re-profiled file.
`iccProfiles` and `transform.iccSource`/`iccSha256` (since 0.6.0-alpha.6, FK-003): how the colours of a graded
file without a usable embedded profile are read (export/scan/srgb/adobe_rgb/file), confirmed by the user; the
profile bytes are stored once per SHA-256 (verified on load) so the choice survives a missing profile file or scan.
Manual alignments (since 0.6.0-alpha.4, RFC-0006 §New alignment): a record with `"origin": "manual_alignment"`
places a graded file on a print that has no export in this project. It has no `exportedAt`; `file` is the graded
file; its geometry is the print's current Inner (pixel crop or straightened); `version` is the print's next version
and a matching `edits[]` entry has an empty `path`. `origin` is absent on export records (absent = export).
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

## 4. Sidecar `*.fk.json` — schema `fk.sidecar/1` (optional since 0.6.0-alpha.3)
Written next to an export only when Settings ▸ "Also write a portable sidecar" is on. The same object is always
stored in the project's `exportRecords`.
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
 "software": "FrameKeeper 0.5.0", "exportedAt": "ISO-8601",
 "origin": "manual_alignment (project records only; absent for exports)"}
```

## 5. XMP written into exports
Namespace `fk` = `https://github.com/fengk623/polaroid-autocrop/ns/1.0/`:
`fk:ProjectID`, `fk:ScanID`, `fk:ItemID`, `fk:EditVersionID`, `fk:Version`, `fk:FrameMode`
(`real_scan` | `synthetic` | `image_area`), plus `xmpMM:DocumentID = uuid:<editVersionId>`,
`xmpMM:InstanceID = uuid:<random>`.
