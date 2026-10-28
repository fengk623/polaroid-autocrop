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
for b in bad:
    print("license check:", b)
sys.exit(1 if bad else 0)
