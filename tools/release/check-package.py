#!/usr/bin/env python3
"""FK-014 (U028): checks what a package contains, as the user receives it.

  check-package.py windows <folder> <version> <commit>   unpacked portable ZIP (its FrameKeeper folder) or the
                                                         folder the installer installed into
  check-package.py macos-app <FrameKeeper.app> <version> <commit>
  check-package.py dmg <mounted volume> <version> <commit>

Checks: the app; LICENSE, THIRD-PARTY-LICENSES.md and every licence text of licenses/README.md, identical to
the repository's; BUILDINFO.txt naming this version and commit; the version the app declares to the system
(macOS Info.plist); the Visual C++ runtime next to the app (Windows); and nothing that must not ship — debug
symbols, import libraries, object files, test programs, interpreter caches, scans, projects, settings, reports.
Prints one line per problem ("::error::") and exits 1 when there is any.
"""
import plistlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FORBIDDEN = re.compile(r"\.(pdb|ilk|lib|exp|obj|o|a|pyc|tif|tiff|dng|fkproj|fkrecovery|ini)$|(^|/)(test_\w+|fkcli|fkfixture)"
                       r"(\.exe)?$|(^|/)report\.json$|__pycache__", re.IGNORECASE)


def licence_files() -> list[str]:
    table = (ROOT / "licenses" / "README.md").read_text(encoding="utf-8")
    names = sorted(set(re.findall(r"`([\w.+-]+\.(?:txt|md))`", table)))
    return [n for n in names if (ROOT / "licenses" / n).is_file()]


def check_common(base: Path, problems: list[str], version: str, commit: str, what: str) -> None:
    for name in ("LICENSE", "THIRD-PARTY-LICENSES.md"):
        p = base / name
        if not p.is_file():
            problems.append(f"{what}: {name} missing")
        elif p.read_bytes().replace(b"\r\n", b"\n") != (ROOT / name).read_bytes().replace(b"\r\n", b"\n"):
            problems.append(f"{what}: {name} differs from the repository's")
    for name in licence_files():
        p = base / "licenses" / name
        if not p.is_file():
            problems.append(f"{what}: licenses/{name} missing")
        elif p.read_bytes() != (ROOT / "licenses" / name).read_bytes():
            problems.append(f"{what}: licenses/{name} differs from the repository's")
    info = base / "BUILDINFO.txt"
    if not info.is_file():
        problems.append(f"{what}: BUILDINFO.txt missing")
    else:
        text = info.read_text(encoding="utf-8", errors="replace")
        got_ver = re.search(r"^Version:\s*(\S+)", text, re.M)
        got_sha = re.search(r"^Commit:\s*(\S+)", text, re.M)
        if not got_ver or got_ver.group(1) != version:
            problems.append(f"{what}: BUILDINFO.txt version {got_ver.group(1) if got_ver else None!r}, expected {version!r}")
        if not got_sha or got_sha.group(1) != commit:
            problems.append(f"{what}: BUILDINFO.txt commit {got_sha.group(1) if got_sha else None!r}, expected {commit!r}")


def check_forbidden(base: Path, problems: list[str], what: str, allow=()) -> int:
    n = 0
    for p in base.rglob("*"):
        if p.is_symlink() or not p.is_file():
            continue
        n += 1
        rel = p.relative_to(base).as_posix()
        if FORBIDDEN.search(rel) and rel not in allow:
            problems.append(f"{what}: {rel} must not ship")
    return n


def check_windows(folder: Path, version: str, commit: str, problems: list[str]) -> str:
    what = f"Windows package {folder}"
    if not (folder / "FrameKeeper.exe").is_file():
        problems.append(f"{what}: FrameKeeper.exe missing")
    for dll in ("Qt6Core.dll", "Qt6Gui.dll", "Qt6Qml.dll", "Qt6Quick.dll", "msvcp140.dll", "vcruntime140.dll",
                "vcruntime140_1.dll"):
        if not (folder / dll).is_file():
            problems.append(f"{what}: {dll} missing")
    if not (folder / "platforms" / "qwindows.dll").is_file():
        problems.append(f"{what}: platforms/qwindows.dll missing")
    check_common(folder, problems, version, commit, what)
    n = check_forbidden(folder, problems, what)
    return f"{what}: {n} files"


def check_macos_app(app: Path, version: str, commit: str, problems: list[str]) -> str:
    what = f"macOS app {app.name}"
    c = app / "Contents"
    if not (c / "MacOS" / "FrameKeeper").is_file():
        problems.append(f"{what}: Contents/MacOS/FrameKeeper missing")
    try:
        plist = plistlib.loads((c / "Info.plist").read_bytes())
    except (OSError, plistlib.InvalidFileException) as e:
        plist = {}
        problems.append(f"{what}: Info.plist unreadable ({e})")
    numeric = version.split("-")[0]
    for key in ("CFBundleShortVersionString", "CFBundleVersion"):
        if plist.get(key) != numeric:
            problems.append(f"{what}: {key} {plist.get(key)!r}, expected {numeric!r}")
    if plist.get("CFBundleExecutable") != "FrameKeeper":
        problems.append(f"{what}: CFBundleExecutable {plist.get('CFBundleExecutable')!r}")
    for fw in ("QtCore", "QtGui", "QtQml", "QtQuick"):
        if not (c / "Frameworks" / f"{fw}.framework").is_dir():
            problems.append(f"{what}: Frameworks/{fw}.framework missing")
    if not (c / "PlugIns" / "platforms" / "libqcocoa.dylib").is_file():
        problems.append(f"{what}: PlugIns/platforms/libqcocoa.dylib missing")
    check_common(c / "Resources", problems, version, commit, what)
    n = check_forbidden(app, problems, what)
    return f"{what}: {n} files, Info.plist {plist.get('CFBundleShortVersionString')}"


def check_dmg(volume: Path, version: str, commit: str, problems: list[str]) -> str:
    what = f"DMG volume {volume}"
    apps = volume / "Applications"
    if not apps.is_symlink():
        problems.append(f"{what}: the Applications shortcut is missing")
    for name in ("READ ME FIRST.md", "BUILDINFO.txt"):
        if not (volume / name).is_file():
            problems.append(f"{what}: {name} missing")
    info = volume / "BUILDINFO.txt"
    if info.is_file() and f"Commit:    {commit}" not in info.read_text(encoding="utf-8", errors="replace"):
        problems.append(f"{what}: BUILDINFO.txt does not name commit {commit}")
    extra = sorted(p.name for p in volume.iterdir()
                   if p.name not in ("FrameKeeper.app", "Applications", "READ ME FIRST.md", "BUILDINFO.txt")
                   and not p.name.startswith("."))
    if extra:
        problems.append(f"{what}: unexpected files {extra}")
    return check_macos_app(volume / "FrameKeeper.app", version, commit, problems)


def main() -> int:
    if len(sys.argv) != 5 or sys.argv[1] not in ("windows", "macos-app", "dmg"):
        print(__doc__)
        return 2
    kind, path, version, commit = sys.argv[1], Path(sys.argv[2]), sys.argv[3], sys.argv[4]
    problems: list[str] = []
    summary = {"windows": check_windows, "macos-app": check_macos_app, "dmg": check_dmg}[kind](
        path, version, commit, problems)
    print(f"check-package: {summary}; {len(licence_files())} licence texts expected; {len(problems)} problem(s)")
    for p in problems:
        print(f"::error::{p}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
