#!/usr/bin/env bash
# FK-014 (U028): the macOS DMG as a user meets it — mount it, check what it holds and its signature, copy the app
# to an Applications folder and run the copy; replace the newest published release's app with it (upgrade), go
# back (rollback) and forward again, with the user's data (settings, templates, recovery files, a project)
# checked byte for byte after every step; "uninstalling" (deleting the app) leaves that data alone.
# Prints PASS/FAIL lines (tools/ci-annotate.py) and exits 1 when anything failed.
#   tools/release/test-macos-dmg.sh <dmg> <version> <commit> <scan.tif>
# env: GH_TOKEN (downloads the newest published release's DMG; none = upgrade not tested).
set -uo pipefail
DMG=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); VER=$2; SHA=$3; SCAN=$(cd "$(dirname "$4")" && pwd)/$(basename "$4")
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${FK_TEST_WORK:-$(mktemp -d)} # logs and reports stay here (CI: build/install-test)
mkdir -p "$WORK"
APPS="$WORK/Applications" # stands in for /Applications (the runner's real one is not touched)
FAILED=0
MOUNTS=()
PASSED=0 NFAILED=0 SKIPPED=0
pass() { echo "PASS FK-014 $*"; PASSED=$((PASSED + 1)); }
fail() { echo "FAIL FK-014 $*"; FAILED=1; NFAILED=$((NFAILED + 1)); }
skipped() { echo "SKIP FK-014 $*"; SKIPPED=$((SKIPPED + 1)); }
# the counts as a CI annotation, so that a green run's result can be read back too (FK-014 evidence)
finish() { echo "::notice title=FK-014 macOS DMG::$PASSED passed, $NFAILED failed, $SKIPPED skipped"; exit $FAILED; }
check() { local what=$1; shift; if "$@"; then pass "$what"; else fail "$what"; fi; }
attach() { # <dmg> <mount point>
  local i
  mkdir -p "$2"
  for i in 1 2 3 4 5; do # hdiutil is sometimes busy on CI runners (AGENTS T19)
    hdiutil attach -readonly -nobrowse -noautoopen -mountpoint "$2" "$1" > /dev/null && { MOUNTS+=("$2"); return 0; }
    sleep 5
  done
  return 1
}
cleanup() { local m; for m in "${MOUNTS[@]}"; do hdiutil detach -force "$m" > /dev/null 2>&1; done; }
trap cleanup EXIT
tree() { (cd "$1" && find . -type f -print0 | sort -z | xargs -0 shasum -a 256) }
same_app() { [ "$(tree "$1" | shasum -a 256)" = "$(tree "$2" | shasum -a 256)" ]; }
replace_app() { rm -rf "$APPS/FrameKeeper.app" && ditto "$1" "$APPS/FrameKeeper.app"; } # what Finder's "Replace" does
selftest() { # <app> <dir>
  QT_QPA_PLATFORM=offscreen FK_SETTINGS_DIR="$2-settings" "$1/Contents/MacOS/FrameKeeper" \
    --self-test "$2" --scan "$SCAN" > "$2.log" 2>&1
  grep -E '^(FAIL|self-test)' "$2.log" | head -20
  python3 "$ROOT/tools/release/check-report.py" "$2/report.json" "$VER" "$SHA"
}
seed_data() {
  # Qt's locations for this app: settings (IniFormat, user scope) and the app data folder
  CONF="$HOME/.config/FrameKeeper"; DATA="$HOME/Library/Application Support/FrameKeeper/FrameKeeper"
  DOCS="$HOME/Documents/FrameKeeper install test"
  mkdir -p "$CONF" "$DATA/templates" "$DATA/recovery" "$DOCS"
  printf '[app]\nlanguage=zh_CN\nrecentProjects=%s\n' "$DOCS/My scans.fkproj" > "$CONF/FrameKeeper.ini"
  cp "$ROOT/tests/data/compat/v0.6.0-alpha.1/jig.fktemplate" "$DATA/templates/Two-up jig.fktemplate"
  echo '{"schema":"fk.recovery/1"}' > "$DATA/recovery/8b0c7c36.fkrecovery"
  cp "$ROOT/tests/data/compat/rollback-v0.6.0-alpha.1/before.fkproj" "$DOCS/My scans.fkproj"
  (find "$CONF" "$DATA" "$DOCS" -type f -print0 | sort -z | xargs -0 shasum -a 256) > "$WORK/data.sha"
}
data_unchanged() { [ -s "$WORK/data.sha" ] && shasum -a 256 -c --quiet "$WORK/data.sha"; }

mkdir -p "$APPS"
NEW="$WORK/mnt-new"
check "the DMG mounts" attach "$DMG" "$NEW" || exit 1
check "DMG contents: app, Applications shortcut, READ ME FIRST, BUILDINFO, licences, Info.plist version" \
  python3 "$ROOT/tools/release/check-package.py" dmg "$NEW" "$VER" "$SHA"
check "the app's signature is intact (ad-hoc, not notarised)" codesign --verify --deep --strict "$NEW/FrameKeeper.app"
echo "signature: $(codesign -dv "$NEW/FrameKeeper.app" 2>&1 | grep -E '^(Signature|TeamIdentifier)' | tr '\n' ' ')"
echo "Gatekeeper: $(spctl --assess --type execute "$NEW/FrameKeeper.app" 2>&1 | tr '\n' ' ') (not notarised: docs/INSTALL.md)"
replace_app "$NEW/FrameKeeper.app"
check "install: the app copied to Applications is the DMG's" same_app "$APPS/FrameKeeper.app" "$NEW/FrameKeeper.app"
check "the installed app (arm64) passes its self-test and reports $VER, commit $SHA" selftest "$APPS/FrameKeeper.app" "$WORK/st-installed"
if arch -x86_64 /usr/bin/true 2> /dev/null; then
  QT_QPA_PLATFORM=offscreen FK_SETTINGS_DIR="$WORK/st-x64-settings" arch -x86_64 "$APPS/FrameKeeper.app/Contents/MacOS/FrameKeeper" \
    --self-test "$WORK/st-x64" --scan "$SCAN" > "$WORK/st-x64.log" 2>&1
  check "the installed app (x86_64 under Rosetta) passes its self-test" \
    python3 "$ROOT/tools/release/check-report.py" "$WORK/st-x64/report.json" "$VER" "$SHA"
else
  skipped "x86_64 slice: Rosetta not available on this runner"
fi

if [ -z "${GH_TOKEN:-}" ]; then
  skipped "upgrade and rollback: no GH_TOKEN to download the published releases"
  finish
fi
REPO=${GITHUB_REPOSITORY:-fengk623/polaroid-autocrop}
TAG=$(gh release list -R "$REPO" --exclude-drafts --limit 20 --json tagName,createdAt \
  --jq 'sort_by(.createdAt) | reverse | .[].tagName' | grep -vx "v$VER" | head -1 || true)
[ -n "$TAG" ] || { skipped "upgrade: no earlier published release"; finish; }
PV=${TAG#v}
mkdir -p "$WORK/rel"
if ! gh release download "$TAG" -R "$REPO" -D "$WORK/rel" -p '*.dmg' -p 'SHA256SUMS.txt' > /dev/null; then
  fail "download of release $TAG"; exit 1
fi
check "release $TAG: the DMG matches its SHA256SUMS" bash -c "cd '$WORK/rel' && grep '\.dmg' SHA256SUMS.txt | shasum -a 256 -c --quiet"
OLD="$WORK/mnt-old"
check "release $TAG's DMG mounts" attach "$WORK/rel"/*.dmg "$OLD" || exit 1
old_version() { /usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APPS/FrameKeeper.app/Contents/Info.plist"; }
replace_app "$OLD/FrameKeeper.app"
check "release $TAG installed" same_app "$APPS/FrameKeeper.app" "$OLD/FrameKeeper.app"
seed_data
echo "== upgrade $TAG → $VER"
replace_app "$NEW/FrameKeeper.app"
check "upgrade $TAG → $VER: Applications holds this version's app" same_app "$APPS/FrameKeeper.app" "$NEW/FrameKeeper.app"
check "upgrade $TAG → $VER: the user's settings, templates, recovery files and projects unchanged" data_unchanged
echo "== rollback $VER → $TAG"
replace_app "$OLD/FrameKeeper.app"
check "rollback $VER → $TAG: Applications holds release $TAG's app ($(old_version))" same_app "$APPS/FrameKeeper.app" "$OLD/FrameKeeper.app"
check "rollback $VER → $TAG: release $TAG starts and reports its version" bash -c \
  "QT_QPA_PLATFORM=offscreen '$APPS/FrameKeeper.app/Contents/MacOS/FrameKeeper' --version 2>&1 | tee /dev/stderr | grep -q '$PV'"
check "rollback $VER → $TAG: the user's data unchanged" data_unchanged
echo "== upgrade again $TAG → $VER"
replace_app "$NEW/FrameKeeper.app"
check "upgrade again: the app runs and reports $VER" bash -c \
  "QT_QPA_PLATFORM=offscreen '$APPS/FrameKeeper.app/Contents/MacOS/FrameKeeper' --version 2>&1 | tee /dev/stderr | grep -q '$VER'"
check "upgrade again: the user's data unchanged" data_unchanged
rm -rf "$APPS/FrameKeeper.app"
check "deleting the app (macOS uninstall) leaves the user's data" data_unchanged
finish
