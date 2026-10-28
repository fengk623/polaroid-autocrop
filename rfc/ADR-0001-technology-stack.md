# ADR-0001 — Technology stack

Status: Accepted · Date: 2026-10-28

## Context
Preferred stack in the product brief: C++20, Qt 6 Qt Quick, CMake, libvips, libtiff, LittleCMS, Exiv2,
OpenCV; CPU reference path; optional GPU. Any deviation needs an ADR showing it suits macOS, Windows,
huge TIFFs, 16-bit, ICC, low memory, future iOS/Android, maintainability, packaging and licensing.

## Decision
| Layer | Choice | Version |
| --- | --- | --- |
| Language | C++20 (core), QML (UI) | GCC ≥ 12, Clang ≥ 15, MSVC 2022 |
| Build | CMake ≥ 3.21, Ninja | — |
| UI | Qt 6.8 LTS line, Qt Quick + Quick Controls (Basic style, own theme), Quick Shapes, Quick Dialogs | 6.8.3 |
| TIFF | libtiff | 4.7.2 |
| Colour | LittleCMS 2 | 2.19.1 |
| PNG / JPEG / zlib | libpng, libjpeg-turbo, zlib | 1.6.59 / 3.2.1 / 1.3.1 |
| JSON | nlohmann/json (header-only) | 3.12.0 |
| Tests | GoogleTest; Qt Quick Test for QML | 1.17.0 |

**Deviations from the preferred stack**

1. *libvips is not used.* All full-resolution work in this product is one of three access patterns —
   sequential row bands of a region, narrow edge bands, or a viewport tile — and all three map directly
   onto libtiff strip/tile decoding (RFC-0003). A thin in-house `TiffSource` with a sliding row window
   gives exact, testable memory bounds (rows × width × bytes), which is what PERFORMANCE-BUDGET.md needs.
   libvips would add GLib/GObject, a demand-driven cache whose memory we would have to tune and measure
   anyway, and ~15 MB per platform package; it is LGPL and builds for iOS/Android are non-trivial.
2. *OpenCV is not used.* Detection needs: box downsampling, Lab distance, thresholds (Otsu), binary
   morphology, connected components, convex hull, rotating-calipers minimum-area rectangle, robust line and
   circle fits, 1-D profile analysis. Each is < 300 lines, deterministic and unit-tested; OpenCV would add
   ~30 MB, a large build matrix, and a second image-container type in the core.
3. *Exiv2 is not used in v1.* The metadata we must preserve in TIFF (resolution, ICC, orientation,
   baseline descriptive tags, XMP packet, EXIF IFD subset) is reachable through libtiff; PNG and JPEG
   metadata through libpng/libjpeg. Exiv2 is GPL-2.0-or-later, which would force the whole app to GPL
   and block a future App Store build (ADR-0011).

These remain replaceable: the core hides them behind `ImageSource`/`ImageSink` and `detection::` APIs.
Re-evaluation trigger: a requirement for formats beyond TIFF/PNG/JPEG/DNG, or measured detection quality
below PRD thresholds that a library would fix.

## Consequences
- + Small, permissive, statically linked core; same code on desktop and future mobile.
- + Memory bounds are explicit in our code.
- − We own the CV primitives: each has unit tests against analytic inputs (RFC-0004).
- − Qt 6.8.4+ LTS patch releases are commercial-only; open-source builds track 6.8.3 until moving to the
  next open-source LTS (re-evaluated at each release).
