#!/usr/bin/env bash
# Writes BUILDINFO.txt for a package (FK-014): the exact source, build and dependencies of the files beside it.
# Usage: tools/release/buildinfo.sh <out-file> <platform>   (env: FK_SOURCE_SHA, FK_BUILD_ID, QT_VERSION)
set -euo pipefail
OUT=$1; PLATFORM=$2
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VER=$(tr -d ' \r\n' < "$ROOT/VERSION")
{
  echo "Product:   FrameKeeper"
  echo "Version:   $VER"
  echo "Commit:    ${FK_SOURCE_SHA:-unknown}"
  echo "Build:     ${FK_BUILD_ID:-local}"
  echo "Platform:  $PLATFORM"
  echo "Built:     $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "Qt:        ${QT_VERSION:-unknown}"
  echo "Signing:   not code-signed, not notarised (docs/INSTALL.md)"
  echo "Source:    https://github.com/fengk623/polaroid-autocrop/tree/${FK_SOURCE_SHA:-main}"
  echo
  echo "Bundled libraries (pinned tags, cmake/deps/CMakeLists.txt):"
  grep -E '^set\(FK_[A-Z0-9]+_TAG' "$ROOT/cmake/deps/CMakeLists.txt" | grep -v GTEST | sed -E 's/^set\(FK_([A-Z0-9]+)_TAG +"([^"]+)"\)/  \1 \2/' || true
} > "$OUT"
echo "wrote $OUT"
