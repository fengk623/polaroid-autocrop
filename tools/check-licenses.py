#!/usr/bin/env python3
"""Every pinned dependency in cmake/deps and every linked Qt module must be listed in THIRD-PARTY-LICENSES.md."""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
deps = (ROOT / "cmake/deps/CMakeLists.txt").read_text(encoding="utf-8")
tpl = (ROOT / "THIRD-PARTY-LICENSES.md").read_text(encoding="utf-8").lower()
names = {"zlib": "zlib", "libpng": "libpng", "libjpeg": "libjpeg-turbo", "libtiff": "libtiff",
         "lcms2": "littlecms", "nlohmann_json": "nlohmann/json", "googletest": "googletest"}
bad = []
for target in re.findall(r"ExternalProject_Add\((\w+)", deps):
    want = names.get(target, target).lower()
    if want not in tpl:
        bad.append(f"dependency '{target}' ({want}) missing from THIRD-PARTY-LICENSES.md")
for tag, ver in re.findall(r'set\(FK_(\w+)_TAG\s+"v?(?:lcms)?([\d.]+)"\)', deps):
    if ver.strip(".") and ver.strip(".") not in tpl:
        bad.append(f"version {ver} of {tag} not recorded in THIRD-PARTY-LICENSES.md")
app = ROOT / "app/CMakeLists.txt"
if app.exists():
    for mod in sorted(set(re.findall(r"Qt6::(\w+)", app.read_text(encoding="utf-8")))):
        if mod.lower() not in tpl:
            bad.append(f"Qt module {mod} missing from THIRD-PARTY-LICENSES.md")
# FK-014 (U028): the packages carry the full licence texts (licenses/, copied by tools/release/package-*.sh and
# checked in every package by tools/release/check-package.py): each text listed exists, and the versions named
# match what is built (cmake/deps tags, QT_VERSION of the workflows)
lic = ROOT / "licenses"
table = (lic / "README.md").read_text(encoding="utf-8") if (lic / "README.md").exists() else ""
if not table:
    bad.append("licenses/README.md missing (the index of the licence texts shipped in the packages)")
for name in sorted(set(re.findall(r"`([\w.+-]+\.(?:txt|md))`", table))):
    if not (lic / name).is_file() and not (ROOT / name).is_file():
        bad.append(f"licenses/{name} listed in licenses/README.md but missing")
rows = {m.group(1).strip().lower(): m.group(2).strip() for m in re.finditer(r"^\| ([^|]+) \| ([^|]+) \|", table, re.M)}
for tag_name, comp in (("ZLIB", "zlib"), ("LIBPNG", "libpng"), ("LIBJPEG", "libjpeg-turbo"), ("LIBTIFF", "libtiff"),
                       ("LCMS2", "littlecms 2"), ("JSON", "nlohmann/json")):
    m = re.search(rf'set\(FK_{tag_name}_TAG\s+"v?(?:lcms)?([\d.]+)"\)', deps)
    if m and rows.get(comp) != m.group(1).strip("."):
        bad.append(f"licenses/README.md: {comp} {rows.get(comp)!r}, cmake/deps builds {m.group(1)!r}")
qt_versions = set()
for wf in (ROOT / ".github" / "workflows").glob("*.yml"):
    qt_versions |= set(re.findall(r'QT_VERSION:\s*"([\d.]+)"', wf.read_text(encoding="utf-8")))
attributions = (lic / "Qt-third-party.md").read_text(encoding="utf-8") if (lic / "Qt-third-party.md").exists() else ""
for qv in sorted(qt_versions):
    if rows.get("qt") != qv:
        bad.append(f"licenses/README.md: Qt {rows.get('qt')!r}, the workflows build Qt {qv}")
    if not attributions.startswith(f"# Third-party components in Qt {qv}\n"):
        bad.append(f"licenses/Qt-third-party.md is not for Qt {qv} (regenerate: tools/release/qt-attributions.py)")
if "licenses/" not in (ROOT / "THIRD-PARTY-LICENSES.md").read_text(encoding="utf-8"):
    bad.append("THIRD-PARTY-LICENSES.md does not point to the licence texts in licenses/")
for script in ("package-windows.sh", "package-macos.sh"):
    if '"$ROOT/licenses"' not in (ROOT / "tools" / "release" / script).read_text(encoding="utf-8"):
        bad.append(f"tools/release/{script} does not copy licenses/ into the package")
for b in bad:
    print("license check:", b)
print(f"check-licenses: {len(bad)} problem(s)")
sys.exit(1 if bad else 0)
