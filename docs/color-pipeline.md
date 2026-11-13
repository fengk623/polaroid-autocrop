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

## Display (as implemented since 0.7.0-alpha.8, U025 / FK-011)

- macOS: scan → sRGB 8-bit; Qt windows are sRGB-tagged and macOS converts to the display (not verified in a real
  window yet: HV).
- Windows: scan → the ICC profile of the monitor the window is on (`GetICMProfileW` on that monitor's device
  context, through `QWindowsScreen::handle()`); sRGB when no profile is installed, it is not an RGB profile, or lcms
  cannot target it. `FK_DISPLAY_SRGB=1` forces sRGB.
- **Follows the window**: read again when the window moves to another screen, when screens are added or removed,
  and every 5 s (an ICC changed in the system settings sends no notification). When the profile changes, the proxy
  on screen is converted at once and then read again from the scan (exact), the full-resolution patch and the
  composite preview are made again, thumbnails are converted from the old profile to the new one, and proxies of
  other scans are read again when shown. A result converted for the previous profile (a job in flight) is
  discarded. The About dialog names the profile in use.
- MinIsWhite: rows from the reader are already in normal polarity; the display no longer inverts them again
  (before U025 the full-resolution view of a MinIsWhite scan was a negative while the preview was right).
- Display conversion never changes source or output pixels (self-test: outputs identical whatever the profile).
- Overlays, handles and the canvas surround are drawn in sRGB and never pass through the image transform.
- Not yet verified with a colorimeter (HV-04).
