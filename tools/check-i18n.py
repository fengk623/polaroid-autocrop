#!/usr/bin/env python3
"""Checks that every user-visible string has a Simplified Chinese translation (app/i18n/zh_CN.json).

Sources: qsTr("…") in app/qml, tr("…") in app/src, and core error messages (makeError(Errc::…, "…"))
which the app shows through QCoreApplication::translate("Errors", …).
Usage: tools/check-i18n.py [--update]   (--update adds missing keys with empty values for translators)
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = ROOT / "app" / "i18n" / "zh_CN.json"
STR = r'"((?:[^"\\]|\\.)*)"'
# adjacent C++ string literals ("a" "b") are one string to tr(): capture the whole run
STRS = r'((?:"(?:[^"\\]|\\.)*"\s*)+)'
LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')


def join_literals(run: str) -> str:
    return "".join(LIT.findall(run))


def unescape(s: str) -> str:
    return bytes(s, "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")


def collect() -> list[str]:
    found = []
    for f in sorted((ROOT / "app" / "qml").glob("*.qml")):
        found += re.findall(r"qsTr\(\s*" + STR, f.read_text(encoding="utf-8"))
    for f in sorted((ROOT / "app" / "src").glob("*.cpp")):
        text = f.read_text(encoding="utf-8")
        found += [join_literals(r) for r in re.findall(r"(?<![A-Za-z_])tr\(\s*" + STRS, text)]
    for f in sorted((ROOT / "core" / "src").rglob("*.cpp")):
        text = f.read_text(encoding="utf-8")
        found += re.findall(r"makeError\(\s*Errc::\w+,\s*" + STR, text)
    out, seen = [], set()
    for s in found:
        s = unescape(s)
        if s and s not in seen:
            seen.add(s)
            out.append(s)
    return out


def main() -> int:
    strings = collect()
    table = json.loads(TABLE.read_text(encoding="utf-8")) if TABLE.exists() else {}
    flat = table.setdefault("*", {})
    missing = [s for s in strings if not flat.get(s)]
    if "--update" in sys.argv:
        for s in missing:
            flat.setdefault(s, "")
        TABLE.write_text(json.dumps(table, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    if missing:
        print(f"check-i18n: {len(missing)} of {len(strings)} strings lack a zh_CN translation:")
        for s in missing[:200]:
            print("  -", s)
        return 1
    print(f"check-i18n: {len(strings)} strings translated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
