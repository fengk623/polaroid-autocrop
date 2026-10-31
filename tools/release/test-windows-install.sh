#!/usr/bin/env bash
# FK-014 (U028): the Windows installer on a clean CI runner — install, upgrade from every published release,
# rollback to the newest one, upgrade again and uninstall — with the user's data (settings, templates, recovery
# files under %APPDATA%\FrameKeeper, a project in Documents) checked byte for byte after every step.
# Prints PASS/FAIL lines (tools/ci-annotate.py) and exits 1 when anything failed. Run in Git Bash after
# tools/release/package-windows.sh:
#   tools/release/test-windows-install.sh <dist-dir> <version> <commit> <scan.tif>
# env: GH_TOKEN (downloads the published releases' installers and portable ZIPs; none = upgrade not tested).
set -uo pipefail
DIST=$(cd "$1" && pwd); VER=$2; SHA=$3; SCAN=$(cd "$(dirname "$4")" && pwd)/$(basename "$4")
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SETUP="$DIST/FrameKeeper-$VER-Windows-x64-Setup.exe"
STAGE="$DIST/stage/FrameKeeper"
KEY='HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\{6A4E6C2B-1F0B-4C51-9D7A-3B2E8F4D9C11}_is1'
ASSOC='HKCU\Software\Classes\.fkproj'
WORK=${FK_TEST_WORK:-$(mktemp -d)} # logs and reports stay here (CI: build/install-test)
mkdir -p "$WORK"; WORK=$(cygpath -u "$WORK")
noconv() { MSYS2_ARG_CONV_EXCL='*' "$@"; } # Inno Setup and reg.exe options start with "/": no path conversion
FAILED=0
pass() { echo "PASS FK-014 $*"; }
fail() { echo "FAIL FK-014 $*"; FAILED=1; }
check() { local what=$1; shift; if "$@"; then pass "$what"; else fail "$what"; fi; }

regval() { noconv reg query "$1" /v "$2" 2> /dev/null | tr -d '\r' | sed -n "s/^ *$2 *REG_[A-Z_]* *//p"; }
haskey() { noconv reg query "$1" > /dev/null 2>&1; }
appdir() { local l; l=$(regval "$KEY" InstallLocation); [ -n "$l" ] && cygpath -u "$l" | sed 's#/$##'; }
assoc_ok() {
  noconv reg query "$ASSOC" /ve 2> /dev/null | grep -q 'FrameKeeper.Project' &&
    noconv reg query 'HKCU\Software\Classes\FrameKeeper.Project\shell\open\command' /ve 2> /dev/null | grep -qi 'FrameKeeper.exe'
}
# the extension key may stay (other programs may list themselves under it); FrameKeeper's value and its file
# type go
no_assoc() {
  ! (noconv reg query "$ASSOC" /ve 2> /dev/null | grep -q 'FrameKeeper.Project') &&
    ! haskey 'HKCU\Software\Classes\FrameKeeper.Project'
}
shortcut() { echo "$(cygpath -u "$APPDATA")/Microsoft/Windows/Start Menu/Programs/FrameKeeper.lnk"; }

install() { # <setup.exe> <log name> <expected version>
  noconv "$1" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCANCEL "/LOG=$(cygpath -w "$WORK/$2.log")"
  local rc=$? i
  for i in $(seq 1 60); do [ "$(regval "$KEY" DisplayVersion)" = "$3" ] && return 0; sleep 2; done
  echo "installer $2 exit code $rc; registered version '$(regval "$KEY" DisplayVersion)'"
  tail -20 "$WORK/$2.log" 2> /dev/null
  return 1
}
uninstall() {
  local dir; dir=$(appdir)
  [ -n "$dir" ] && [ -x "$dir/unins000.exe" ] || { echo "no uninstaller in '$dir'"; return 1; }
  noconv "$dir/unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
  local i # the uninstaller runs a copy of itself and returns at once
  for i in $(seq 1 90); do
    if ! haskey "$KEY" && [ ! -e "$dir/FrameKeeper.exe" ]; then return 0; fi
    sleep 2
  done
  echo "still installed after 180 s: key $(haskey "$KEY" && echo present || echo gone), dir $(ls "$dir" 2> /dev/null | head -5)"
  return 1
}
tree() { (cd "$1" && find . -type f ! -name 'unins000.*' -print0 | sort -z | xargs -0 sha256sum) }
same_tree() { # <installed dir> <package dir> <label> [leftovers-ok]: every package file installed unchanged; no
  # stale file where Qt loads plugins (an earlier release's installer cannot clean up after a later one: with
  # "leftovers-ok" those are listed, not failed)
  tree "$2" > "$WORK/want.txt"; tree "$1" > "$WORK/got.txt"
  local missing extra
  missing=$(comm -23 <(sort "$WORK/want.txt") <(sort "$WORK/got.txt") | wc -l)
  extra=$(comm -13 <(cut -c67- "$WORK/want.txt" | sort) <(cut -c67- "$WORK/got.txt" | sort))
  [ "$missing" = 0 ] || { echo "$3: $missing files missing or different"; comm -23 <(sort "$WORK/want.txt") <(sort "$WORK/got.txt") | head -5; }
  if [ -n "$extra" ]; then echo "$3: files left from another version: $(echo "$extra" | head -8 | tr '\n' ' ')"; fi
  # a stale file in a plugin or QML folder would be loaded by Qt
  local stale; stale=$(echo "$extra" | grep -E '^\./(qml|platforms|imageformats|iconengines|styles|generic|networkinformation|tls)/' || true)
  [ "$missing" = 0 ] && { [ -z "$stale" ] || [ "${4:-}" = leftovers-ok ]; }
}
oldrun() { # <exe> <dir> <version>: an earlier release starts and reports its own version (its own checks may
  # differ from this version's, so only the report's version is required)
  PATH="/c/Windows/system32:/c/Windows" QT_QPA_PLATFORM=offscreen FK_SETTINGS_DIR="$(cygpath -w "$2-settings")" \
    timeout 1200 "$1" --self-test "$(cygpath -w "$2")" --scan "$(cygpath -w "$SCAN")" > "$2.log" 2>&1
  grep -E '^self-test' "$2.log" | head -3
  python -c "import json,sys; r=json.load(open(sys.argv[1])); print('report:', r.get('version'), 'ok' if r.get('ok') else 'not ok'); sys.exit(r.get('version') != sys.argv[2])" "$2/report.json" "$3"
}
seed_data() { # the user's own files, which no install step may touch
  DATA="$(cygpath -u "$APPDATA")/FrameKeeper"; DOCS="$(cygpath -u "$USERPROFILE")/Documents/FrameKeeper install test"
  mkdir -p "$DATA/FrameKeeper/templates" "$DATA/FrameKeeper/recovery" "$DOCS"
  printf '[app]\nlanguage=zh_CN\nrecentProjects=%s\n' "$(cygpath -w "$DOCS/My scans.fkproj" | sed 's#\\#/#g')" > "$DATA/FrameKeeper.ini"
  cp "$ROOT/tests/data/compat/v0.6.0-alpha.1/jig.fktemplate" "$DATA/FrameKeeper/templates/Two-up jig.fktemplate"
  echo '{"schema":"fk.recovery/1"}' > "$DATA/FrameKeeper/recovery/8b0c7c36.fkrecovery"
  cp "$ROOT/tests/data/compat/rollback-v0.6.0-alpha.1/before.fkproj" "$DOCS/My scans.fkproj"
  (find "$DATA" "$DOCS" -type f -print0 | sort -z | xargs -0 sha256sum) > "$WORK/data.sha"
}
data_unchanged() { [ -s "$WORK/data.sha" ] && sha256sum -c --quiet "$WORK/data.sha"; }
selftest() { # <exe> <dir>: the installed app runs and reports this package's version and commit
  PATH="/c/Windows/system32:/c/Windows" QT_QPA_PLATFORM=offscreen FK_SETTINGS_DIR="$(cygpath -w "$2-settings")" \
    "$1" --self-test "$(cygpath -w "$2")" --scan "$(cygpath -w "$SCAN")" > "$2.log" 2>&1
  grep -E '^(FAIL|self-test)' "$2.log" | head -20
  python "$ROOT/tools/release/check-report.py" "$2/report.json" "$VER" "$SHA"
}

[ -x "$SETUP" ] || { fail "installer $SETUP missing"; exit 1; }
if haskey "$KEY"; then fail "the runner is not clean: FrameKeeper is already installed"; exit 1; fi

# 1. clean system: install, run, uninstall
echo "== fresh install of $VER"
check "fresh install registers version $VER" install "$SETUP" fresh "$VER"
DIR=$(appdir)
check "fresh install: every packaged file installed unchanged, nothing else ($DIR)" same_tree "$DIR" "$STAGE" fresh
check "fresh install: package contents (licences, BUILDINFO, runtime)" \
  python "$ROOT/tools/release/check-package.py" windows "$DIR" "$VER" "$SHA"
check "fresh install: .fkproj files open with FrameKeeper" assoc_ok
check "fresh install: Start menu shortcut" test -f "$(shortcut)"
check "fresh install: the installed app's self-test passes and reports $VER, commit $SHA" selftest "$DIR/FrameKeeper.exe" "$WORK/st-fresh"
check "uninstall removes the program, its registration and the shortcut" uninstall
check "uninstall removes the .fkproj association" no_assoc
check "uninstall: Start menu shortcut removed" test ! -e "$(shortcut)"

# 2. upgrade from every published release; rollback to the newest; upgrade again; uninstall keeps user data
if [ -z "${GH_TOKEN:-}" ]; then
  echo "SKIP FK-014 upgrade and rollback: no GH_TOKEN to download the published releases"
  exit $FAILED
fi
REPO=${GITHUB_REPOSITORY:-fengk623/polaroid-autocrop}
PREVIOUS=$(gh release list -R "$REPO" --exclude-drafts --limit 20 --json tagName,createdAt \
  --jq 'sort_by(.createdAt) | reverse | .[].tagName' | grep -vx "v$VER" || true)
[ -n "$PREVIOUS" ] || { echo "SKIP FK-014 upgrade: no earlier published release"; exit $FAILED; }
NEWEST=$(echo "$PREVIOUS" | head -1)
for tag in $PREVIOUS; do
  pv=${tag#v}; d="$WORK/rel-$tag"; mkdir -p "$d"
  if ! gh release download "$tag" -R "$REPO" -D "$d" -p '*Setup.exe' -p '*portable.zip' -p 'SHA256SUMS.txt' > /dev/null; then
    fail "download of release $tag"; continue
  fi
  check "release $tag: downloaded files match its SHA256SUMS" bash -c "cd '$d' && grep -E 'Setup.exe|portable.zip' SHA256SUMS.txt | sha256sum -c --quiet"
  (cd "$d" && 7z x -y -o"zip" ./*portable.zip > /dev/null)
  echo "== upgrade $tag → $VER"
  check "install release $tag" install "$d"/*Setup.exe "prev-$tag" "$pv"
  [ "$tag" = "$NEWEST" ] && seed_data
  check "upgrade $tag → $VER registers $VER" install "$SETUP" "up-$tag" "$VER"
  check "upgrade $tag → $VER: the installed files are this package's ($(appdir))" same_tree "$(appdir)" "$STAGE" "upgrade from $tag"
  if [ "$tag" = "$NEWEST" ]; then
    check "upgrade $tag → $VER: the user's settings, templates, recovery files and projects unchanged" data_unchanged
    echo "== rollback $VER → $tag"
    check "rollback $VER → $tag registers $pv" install "$d"/*Setup.exe "back-$tag" "$pv"
    check "rollback $VER → $tag: the installed files are release $tag's" same_tree "$(appdir)" "$d/zip/FrameKeeper" "rollback to $tag" leftovers-ok
    check "rollback $VER → $tag: release $tag starts and reports $pv" oldrun "$(appdir)/FrameKeeper.exe" "$WORK/st-rollback" "$pv"
    check "rollback $VER → $tag: the user's data unchanged" data_unchanged
    echo "== upgrade again $tag → $VER"
    check "upgrade again $tag → $VER" install "$SETUP" "again-$tag" "$VER"
    check "upgrade again: the installed files are this package's" same_tree "$(appdir)" "$STAGE" "upgrade again"
    check "upgrade again: the installed app's self-test passes and reports $VER" selftest "$(appdir)/FrameKeeper.exe" "$WORK/st-upgraded"
  fi
  check "uninstall after the upgrade from $tag" uninstall
  [ "$tag" = "$NEWEST" ] && check "uninstall keeps the user's settings, templates, recovery files and projects" data_unchanged
done
exit $FAILED
