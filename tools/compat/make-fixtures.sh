#!/usr/bin/env bash
# FK-014 (U028): regenerates tests/data/compat/ — projects, export records and templates written by earlier
# releases, and what a rollback round trip through them keeps (tests/data/compat/README.md).
# Usage: tools/compat/make-fixtures.sh <deps-prefix> <work-dir>
#   <deps-prefix>  the install prefix of cmake/deps (libtiff, lcms2, libpng, libjpeg, zlib)
#   <work-dir>     scratch folder for the release worktrees and builds (removed by hand afterwards)
# The fixtures name their files under /Users/example (invented paths), so the projects are written there:
# run it where /Users/example may be created (a container or a throwaway machine), never on a real account.
set -euo pipefail
DEPS=$(cd "$1" && pwd); mkdir -p "$2"; WORK=$(cd "$2" && pwd)
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/tests/data/compat"
EXAMPLE=/Users/example/Projects
RELEASES="v0.5.0 v0.6.0-alpha.1"
LIBS="$DEPS/lib/libtiff.a $DEPS/lib/liblcms2.a $DEPS/lib/libpng16.a $DEPS/lib/libjpeg.a $DEPS/lib/libz.a -lpthread"
mkdir -p "$EXAMPLE"
build() { # <source-tree> <build-dir> <binary> [defines]
  cmake -S "$1" -B "$2" -G Ninja -DCMAKE_BUILD_TYPE=Release -DFK_BUILD_APP=OFF -DFK_BUILD_TOOLS=OFF \
    -DFK_BUILD_TESTS=OFF "-DCMAKE_PREFIX_PATH=$DEPS" > "$2.log"
  cmake --build "$2" --target fkcore >> "$2.log"
  c++ -std=c++20 -O1 ${4:-} -I"$1/core/include" -I"$2/core/generated" -I"$DEPS/include" \
    "$ROOT/tools/compat/fixtures.cpp" "$2/core/libfkcore.a" $LIBS -o "$3"
}
for tag in $RELEASES; do
  [ -d "$WORK/src-$tag" ] || git -C "$ROOT" worktree add -q --detach "$WORK/src-$tag" "$tag"
  build "$WORK/src-$tag" "$WORK/build-$tag" "$WORK/gen-$tag"
  rm -rf "$EXAMPLE/$tag"
  "$WORK/gen-$tag" write "$EXAMPLE/$tag"
  mkdir -p "$OUT/$tag"
  cp "$EXAMPLE/$tag/project.fkproj" "$EXAMPLE/$tag/export_01.fk.json" "$EXAMPLE/$tag/jig.fktemplate" "$OUT/$tag/"
done
# what this version writes, and the same project after the newest earlier release opened and saved it
build "$ROOT" "$WORK/build-current" "$WORK/gen-current" -DFK_FIXTURE_CURRENT
rm -rf "$EXAMPLE/current"
"$WORK/gen-current" write "$EXAMPLE/current"
last=${RELEASES##* }
mkdir -p "$OUT/rollback-$last"
cp "$EXAMPLE/current/project.fkproj" "$OUT/rollback-$last/before.fkproj"
"$WORK/gen-$last" roundtrip "$EXAMPLE/current/project.fkproj" "$EXAMPLE/current/after.fkproj"
cp "$EXAMPLE/current/after.fkproj" "$OUT/rollback-$last/after.fkproj"
echo "fixtures written to $OUT (worktrees in $WORK: remove with git worktree remove)"
