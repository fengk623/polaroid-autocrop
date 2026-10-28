# Colour pipeline

ADR-0003 · RFC-0012 · PRD F12

```
scan TIFF (ICC_s or untagged)
  │ readRows (no conversion; 8/16-bit as stored)
  ├─► proxy (float, encoded values) ─► detection (approx. Lab, sRGB assumption: detection only)
  ├─► display: ICC_s → sRGB (macOS) | monitor ICC (Windows) → RGBA8 tiles   [view only]
  ├─► export image area: ICC_s kept (default) | convert once → ICC_t | assign ICC_t (tag only)
  │       → external editor → graded file (ICC_g or none)
  │              │ ICC_g ≠ ICC_s → convert once ICC_g → ICC_s (relative colorimetric + BPC, 16-bit/float)
  │              │ no ICC → user: interpret as exported ICC / choose / cancel
  └─► composite in ICC_s at source depth: frame pixels = scan pixels (bit-exact)
          → final file: ICC_s (default) | convert once → ICC_t (frame no longer bit-exact; UI says so)
```

Rules
1. At most one conversion per pixel on the path scan → final file (graded pixels: edit-space → scan-space;
   optional final conversion counts as the one conversion for frame pixels).
2. Assign never changes numbers; convert never leaves the old tag.
3. Bit depth: 16-bit paths use `TYPE_RGB_16` transforms; floats only for DNG linearisation.
4. Untagged scans display as sRGB with an "untagged" chip; exports stay untagged unless the user assigns.

## Display (as implemented in v0.1.0)

- macOS: scan → sRGB 8-bit; Qt windows are sRGB-tagged and macOS converts to the display.
- Windows: scan → the primary monitor's ICC profile (`GetICMProfileW`), read once at start-up; sRGB when no
  profile is installed, it is not an RGB profile, or lcms cannot target it. A window on a second monitor with a
  different profile still uses the primary monitor's profile. `FK_DISPLAY_SRGB=1` forces sRGB.
- Overlays, handles and the canvas surround are drawn in sRGB and never pass through the image transform.
- Not yet verified with a colorimeter (HV-04).
