# FrameKeeper

**English** · [简体中文](README.zh-CN.md)

FrameKeeper is a free, offline desktop app (macOS and Windows) for people who scan instant-film prints
(Polaroid 600 / i-Type / SX-70, Polaroid round frame, Instax Mini / Square / Wide) on a flatbed scanner
such as the Epson V750.

1. **Find every print** in a large TIFF scan (300–1600 DPI, 8 or 16 bit, many prints per scan, tilted,
   touching) — the real paper edge is never cut; uncertain prints are flagged for review.
2. **Export the image area** of each print (TIFF, PNG, JPEG or Linear DNG) for grading in Photoshop,
   Lightroom or Capture One. Pixel-crop exports are bit-identical to the scan.
3. **Relink** the graded files (by embedded XMP, sidecar or file name, with a content check).
4. **Put the graded image back into the real scanned frame** — every frame pixel comes from the original
   scan — or add a generated border (synthetic frame) instead.

Your scans are never modified, nothing is uploaded, and existing files are never overwritten.

## Download

Get the latest version from [Releases](https://github.com/fengk623/polaroid-autocrop/releases):

| System | File |
| --- | --- |
| macOS 12 or later (Apple Silicon and Intel) | `FrameKeeper-<version>-macOS-universal.dmg` |
| Windows 10/11 64-bit, installer | `FrameKeeper-<version>-Windows-x64-Setup.exe` |
| Windows 10/11 64-bit, no install | `FrameKeeper-<version>-Windows-x64-portable.zip` |

The builds are not code-signed yet; see [docs/INSTALL.md](docs/INSTALL.md) for the one-time steps on the
first launch, and compare the files with `SHA256SUMS.txt`.

## Quick start

1. Drop one or more TIFF scans on the window (or **File ▸ Import Scans…**).
2. Check each print in the filmstrip; red dash-dot outlines need review. Drag corners or edges with
   **Edit Outer (O)** / **Edit Inner (I)**, or type coordinates in the right rail.
3. **Export Image Areas (⌘E / Ctrl+E)**, grade the files in your editor and save them.
4. **Relink Graded Files (⌥⌘I / Ctrl+Alt+I)**, look at **Composite (4)**, then **Export Final
   (⇧⌘E / Ctrl+Shift+E)**.

Tips: scan dark frames on a light backing (a black frame on a black lid cannot be outlined from pixels);
keep the scanner's DPI tag; use a template when you scan with the same jig every time.

## Build from source

Requirements: CMake ≥ 3.21, Ninja, a C++20 compiler (Xcode 15+, Visual Studio 2022, GCC 13+), Qt 6.5+
(6.8.3 is what releases use), Git.

```sh
cmake -S cmake/deps -B build/deps -G Ninja -DFK_DEPS_PREFIX=$PWD/build/deps-install   # pinned libraries
cmake --build build/deps
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$PWD/build/deps-install;/path/to/Qt/6.8.3/<platform>"
cmake --build build/dev
tools/run-tests.sh build/dev            # 136 tests incl. detection evaluation
build/dev/bin/FrameKeeper               # macOS: build/dev/bin/FrameKeeper.app
```

Without Qt, `-DFK_BUILD_APP=OFF` builds the core library, the command-line tool `fkcli`
(detect / export / round trip / evaluation) and the synthetic scan generator `fkfixture`.

## Documentation

- Product requirements: [PRD/PRD.md](PRD/PRD.md) · module designs: [rfc/README.md](rfc/README.md)
- Architecture: [ARCHITECTURE.md](ARCHITECTURE.md) · core API: [API-SPEC-core.md](API-SPEC-core.md)
- Test plan and measured detection quality: [TEST-PLAN.md](TEST-PLAN.md) · performance:
  [PERFORMANCE-BUDGET.md](PERFORMANCE-BUDGET.md)
- What still needs a human to verify: [docs/HUMAN-VERIFICATION.md](docs/HUMAN-VERIFICATION.md)
- Contributing / agent rules: [AGENTS.md](AGENTS.md) · privacy: [SECURITY-PRIVACY.md](SECURITY-PRIVACY.md)

## Licence

MIT (see [LICENSE](LICENSE)). Qt is used under the LGPL-3.0; other libraries are listed in
[THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md). FrameKeeper is not affiliated with Polaroid or
Fujifilm; product names are used only to describe film formats.
