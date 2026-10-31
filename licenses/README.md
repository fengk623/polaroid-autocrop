# Licences of the software in FrameKeeper's packages

FrameKeeper itself: MIT (`LICENSE`, next to this folder). The overview of every component is
`THIRD-PARTY-LICENSES.md`; this folder holds the full licence texts that the packages carry (FK-014). The
packaging scripts copy it into the portable ZIP and the installer (`licenses\` next to `FrameKeeper.exe`) and
into the macOS app (`FrameKeeper.app/Contents/Resources/licenses/`); `tools/release/check-package.py` fails a
package that lacks a file listed here.

| Component | Version | Licence | Text |
| --- | --- | --- | --- |
| libtiff | 4.7.2 | libtiff licence | `libtiff-LICENSE.md` |
| LittleCMS 2 | 2.19.1 | MIT | `lcms2-LICENSE.txt` |
| libpng | 1.6.59 | PNG Reference Library License v2 | `libpng-LICENSE.txt` |
| libjpeg-turbo | 3.2.1 | IJG, BSD-3-Clause, zlib | `libjpeg-turbo-LICENSE.md`, `libjpeg-turbo-README.ijg.txt` |
| zlib | 1.3.1 | zlib | `zlib-LICENSE.txt` |
| nlohmann/json | 3.12.0 | MIT | `nlohmann-json-LICENSE.MIT.txt` |
| Qt | 6.8.3 | LGPL-3.0-only | `Qt-LGPL-3.0-only.txt`, `Qt-GPL-3.0-only.txt` (the LGPL is a set of additional permissions on top of the GPL) |
| Components inside Qt | (per component) | (per component) | `Qt-third-party.md` |

The texts of the six libraries are copied unchanged from their sources at the pinned versions of
`cmake/deps/CMakeLists.txt`; `Qt-third-party.md` is generated from Qt's attribution files by
`tools/release/qt-attributions.py`. Update them together with a version change (`tools/check-licenses.py`
compares the versions).

This software is based in part on the work of the Independent JPEG Group.

## Qt (LGPL-3.0)

FrameKeeper uses Qt under the GNU Lesser General Public License v3. Qt is linked dynamically: the Qt libraries
are separate files in the package (Windows: the `Qt6*.dll` files and plugin folders next to `FrameKeeper.exe`;
macOS: the frameworks in `FrameKeeper.app/Contents/Frameworks` and the plugins in `Contents/PlugIns`), and you
may replace them with your own build of the same Qt version. On macOS the app is ad-hoc signed; after replacing
a framework, sign it again (`codesign --force --deep --sign - FrameKeeper.app`).

The corresponding Qt source code of this version (unmodified Qt 6.8.3):
<https://download.qt.io/archive/qt/6.8/6.8.3/single/>. FrameKeeper's own source:
<https://github.com/fengk623/polaroid-autocrop>.

## Windows runtime and installer

- Microsoft Visual C++ runtime (`msvcp140*.dll`, `vcruntime140*.dll`, `concrt140.dll`), shipped next to the
  app: redistributed under the Microsoft Visual Studio licence terms for distributable code.
- The installer is built with Inno Setup (Inno Setup License); the installer and uninstaller program are not
  part of the app.
