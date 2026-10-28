# THIRD-PARTY-LICENSES

Machine-checked by `tools/check-licenses.py` (every dependency in `cmake/deps/CMakeLists.txt` and every
Qt module linked by the app must appear here). Full licence texts are installed into the app bundle
(`licenses/`) by the packaging scripts.

| Component | Version | Licence | Linking | Used for | Obligations |
| --- | --- | --- | --- | --- | --- |
| libtiff | 4.7.2 | libtiff licence (BSD-like) | static | TIFF/DNG I/O | keep copyright notice |
| LittleCMS 2 | 2.19.1 (tag lcms2.19.1) | MIT | static | ICC colour management | keep notice |
| libpng | 1.6.59 | PNG Reference Library License v2 | static | PNG I/O | keep notice |
| libjpeg-turbo | 3.2.1 | IJG + BSD-3-Clause + zlib | static | JPEG I/O (libtiff JPEG codec, JPEG export) | notice; IJG acknowledgement "this software is based in part on the work of the Independent JPEG Group" |
| zlib | 1.3.1 | zlib | static | Deflate | notice |
| nlohmann/json | 3.12.0 | MIT | header-only | JSON | notice |
| Qt 6 (Core, Gui, Qml, Quick, QuickControls2, QuickShapes, QuickDialogs2, QuickTemplates2) | 6.8.3 | LGPL-3.0 | dynamic | desktop UI | ship LGPL text + Qt source link; allow relinking (frameworks/DLLs replaceable) |
| GoogleTest | 1.17.0 | BSD-3-Clause | tests only (not shipped) | unit tests | — |
| LibRaw | 0.22.x | LGPL-2.1 / CDDL-1.0 | CI test tool only (not shipped) | DNG decode test | — |

Referenced, not used: SpektraLab (GPL-3.0-or-later; UI and process reference only, no code copied);
PhotoFrame/Encadré (no licence; observable behaviour only, no assets/text).
