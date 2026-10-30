#!/usr/bin/env bash
# Packages build/<dir>/bin/FrameKeeper.exe: portable ZIP + Inno Setup installer (unsigned). Run in Git Bash
# inside an MSVC developer environment. Usage: tools/release/package-windows.sh <build-dir> <out-dir> <version>
set -euo pipefail
BUILD=$(cd "$1" && pwd); VER=$3
mkdir -p "$2"; OUT=$(cd "$2" && pwd)  # absolute: tools such as ISCC resolve relative paths elsewhere
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
QT="${QT_ROOT_DIR:?QT_ROOT_DIR not set}"
STAGE="$OUT/stage/FrameKeeper"
rm -rf "$OUT/stage"
mkdir -p "$STAGE"
cp "$BUILD/bin/FrameKeeper.exe" "$STAGE/"
"$QT/bin/windeployqt.exe" --release --qmldir "$ROOT/app/qml" --no-translations --no-system-d3d-compiler \
  --no-opengl-sw "$STAGE/FrameKeeper.exe"
mkdir -p "$STAGE/platforms"
cp "$QT/plugins/platforms/qoffscreen.dll" "$STAGE/platforms/" || true
# app-local Visual C++ runtime (no separate redistributable install needed)
CRT=$(cygpath -u "${VCToolsRedistDir:?run inside the MSVC environment}")x64/Microsoft.VC143.CRT
cp "$CRT"/msvcp140*.dll "$CRT"/vcruntime140*.dll "$CRT"/concrt140.dll "$STAGE/" 2>/dev/null || cp "$CRT"/*.dll "$STAGE/"
cp "$ROOT/LICENSE" "$ROOT/THIRD-PARTY-LICENSES.md" "$STAGE/"
cp "$ROOT/docs/INSTALL.md" "$STAGE/README.md"
"$ROOT/tools/release/buildinfo.sh" "$STAGE/BUILDINFO.txt" "Windows x64"
cp "$STAGE/BUILDINFO.txt" "$OUT/BUILDINFO-windows.txt"
(cd "$OUT/stage" && 7z a -tzip -mx=9 "../FrameKeeper-$VER-Windows-x64-portable.zip" FrameKeeper > /dev/null)
ISCC="/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
if [ ! -x "$ISCC" ]; then choco install innosetup -y --no-progress > /dev/null; fi
"$ISCC" //Q "//DAppVersion=$VER" "//DSourceDir=$(cygpath -w "$STAGE")" "//DOutputDir=$(cygpath -w "$OUT")" \
  "//DRepoRoot=$(cygpath -w "$ROOT")" "$(cygpath -w "$ROOT/app/packaging/windows/installer.iss")"
ls -la "$OUT"
