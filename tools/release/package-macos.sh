#!/usr/bin/env bash
# Packages build/<dir>/bin/FrameKeeper.app into a drag-to-install DMG (unsigned, ad-hoc signed so that it
# runs on Apple Silicon; Gatekeeper still asks — see docs/INSTALL.md). Usage:
#   tools/release/package-macos.sh <build-dir> <out-dir> <version>
set -euo pipefail
BUILD=$(cd "$1" && pwd); VER=$3
mkdir -p "$2"; OUT=$(cd "$2" && pwd)  # absolute: tools such as ISCC resolve relative paths elsewhere
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
APP="$BUILD/bin/FrameKeeper.app"
QT_BIN="${QT_ROOT_DIR:?QT_ROOT_DIR not set}/bin"
mkdir -p "$OUT"
"$QT_BIN/macdeployqt" "$APP" -qmldir="$ROOT/app/qml" -always-overwrite -verbose=1
# headless platform plugin for the packaged self-test (CI smoke test)
mkdir -p "$APP/Contents/PlugIns/platforms"
cp "$QT_ROOT_DIR/plugins/platforms/libqoffscreen.dylib" "$APP/Contents/PlugIns/platforms/" || true
cp "$ROOT/LICENSE" "$ROOT/THIRD-PARTY-LICENSES.md" "$APP/Contents/Resources/"
rm -rf "$APP/Contents/Resources/licenses"
cp -R "$ROOT/licenses" "$APP/Contents/Resources/licenses"  # full licence texts (FK-014, licenses/README.md)
"$ROOT/tools/release/buildinfo.sh" "$APP/Contents/Resources/BUILDINFO.txt" "macOS universal (arm64, x86_64)"
cp "$APP/Contents/Resources/BUILDINFO.txt" "$OUT/BUILDINFO-macos.txt"
codesign --force --deep --sign - "$APP"
STAGE=$(mktemp -d)
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
cp "$ROOT/docs/INSTALL.md" "$STAGE/READ ME FIRST.md"
cp "$OUT/BUILDINFO-macos.txt" "$STAGE/BUILDINFO.txt"
DMG="$OUT/FrameKeeper-$VER-macOS-universal.dmg"
rm -f "$DMG"
# hdiutil intermittently fails with "Resource busy" on CI runners (AGENTS T19): retry a few times
for attempt in 1 2 3 4 5; do
  if hdiutil create -volname "FrameKeeper $VER" -srcfolder "$STAGE" -ov -format UDZO "$DMG"; then break; fi
  if [ "$attempt" = 5 ]; then echo "hdiutil failed 5 times" >&2; exit 1; fi
  echo "hdiutil failed (attempt $attempt), retrying in 10 s" >&2
  sleep 10
done
echo "$DMG"
