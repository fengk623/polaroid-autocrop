# THIRD-PARTY-LICENSES

Machine-checked by `tools/check-licenses.py` (every dependency in `cmake/deps/CMakeLists.txt` and every
Qt module linked by the app must appear here). The full licence texts are in `licenses/` (index:
`licenses/README.md`, with the Qt source link and the notes on replacing the Qt libraries); the packaging
scripts copy that folder into every package — `licenses\` beside `FrameKeeper.exe`,
`FrameKeeper.app/Contents/Resources/licenses/` — and `tools/release/check-package.py` checks it there (FK-014).
Until 0.7.0-alpha.15 this paragraph said so while the packages carried only this file and `LICENSE`.

| Component | Version | Licence | Linking | Used for | Obligations |
| --- | --- | --- | --- | --- | --- |
| libtiff | 4.7.2 | libtiff licence (BSD-like) | static | TIFF/DNG I/O | keep copyright notice |
| LittleCMS 2 | 2.19.1 (tag lcms2.19.1) | MIT | static | ICC colour management | keep notice |
| libpng | 1.6.59 | PNG Reference Library License v2 | static | PNG I/O | keep notice |
| libjpeg-turbo | 3.2.1 | IJG + BSD-3-Clause + zlib | static | JPEG I/O (libtiff JPEG codec, JPEG export) | notice; IJG acknowledgement "this software is based in part on the work of the Independent JPEG Group" |
| zlib | 1.3.1 | zlib | static | Deflate | notice |
| nlohmann/json | 3.12.0 | MIT | header-only | JSON | notice |
| Qt 6 (Core, Gui, Qml, Quick, QuickControls2, QuickShapes, QuickDialogs2, QuickTemplates2) | 6.8.3 | LGPL-3.0 | dynamic | desktop UI | ship LGPL text + Qt source link; allow relinking (frameworks/DLLs replaceable) |
| Components inside Qt (FreeType, HarfBuzz, PCRE2, libpng, zlib, double-conversion, …) | per component | per component (`licenses/Qt-third-party.md`) | inside the Qt libraries | Qt | keep their notices (generated from Qt's attribution files) |
| Microsoft Visual C++ runtime | 14.x (VS 2022) | Visual Studio distributable code terms | app-local DLLs (Windows) | C++ runtime | redistributed unmodified beside the app |
| Inno Setup | 6 | Inno Setup License | installer only (not part of the app) | Windows installer and uninstaller | — |
| GoogleTest | 1.17.0 | BSD-3-Clause | tests only (not shipped) | unit tests | — |
| LibRaw | 0.22.x | LGPL-2.1 / CDDL-1.0 | CI test tool only (not shipped) | DNG decode test | — |

Referenced, not used: SpektraLab (GPL-3.0-or-later; UI and process reference only, no code copied);
PhotoFrame/Encadré (no licence; observable behaviour only, no assets/text).
