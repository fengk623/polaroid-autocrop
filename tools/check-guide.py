#!/usr/bin/env python3
"""U029 (UX-12): every keyboard shortcut the app defines is in the user guide's shortcut table.

Reads the shortcuts of app/qml/*.qml (`shortcut:` of menu items and actions, `sequence:` / `sequences:` of
Shortcut items, standard keys) and looks for each, written as the guide writes it, in §10 of
docs/v1/USER-GUIDE.zh-CN.md. Exit 1 when one is missing (a shortcut added without documenting it).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STANDARD = {"New": "Ctrl+N", "Open": "Ctrl+O", "Save": "Ctrl+S", "Close": "Ctrl+W", "Undo": "Ctrl+Z",
            "Redo": "Ctrl+Shift+Z", "SelectAll": "Ctrl+A", "Quit": "Ctrl+Q", "ZoomIn": "Ctrl+=",
            "ZoomOut": "Ctrl+−"}
# how the guide writes keys (Windows column; macOS-only shortcuts in the macOS column)
SPELLING = [("Escape", "Esc"), ("Ctrl+Return", "Ctrl+Enter"), ("Return", "Enter"), ("Shift+Left", "Shift+←"), ("Shift+Right", "Shift+→"),
            ("Left", "←"), ("Right", "→"), ("Ctrl+-", "Ctrl+−"), ("Meta+Ctrl+F", "⌃⌘F"), ("Ctrl+?", "⌘?")]


def shortcuts() -> set[str]:
    found = set()
    for f in sorted((ROOT / "app" / "qml").glob("*.qml")):
        text = f.read_text(encoding="utf-8")
        for m in re.finditer(r"\b(?:shortcut|sequence)s?\s*:\s*(\[[^\]]*\]|[^;}\n]+)", text):
            for part in re.findall(r'StandardKey\.(\w+)|"((?:[^"\\]|\\.)+)"', m.group(1)):
                std, lit = part
                key = STANDARD.get(std, f"StandardKey.{std}") if std else lit.replace("\\\\", "\\")
                found.add(key)
    return found


def main() -> int:
    guide = (ROOT / "docs" / "v1" / "USER-GUIDE.zh-CN.md").read_text(encoding="utf-8")
    m = re.search(r"^## 10\..*?(?=^## 11\.)", guide, re.S | re.M)
    section = m.group(0) if m else ""
    missing = []
    for key in sorted(shortcuts()):
        written = key
        for a, b in SPELLING:
            if written == a or written.startswith(a + "+"):
                written = b + written[len(a):]
                break
        if not re.search(r"(?<![\w+])" + re.escape(written) + r"(?![\w+])", section):
            missing.append(f"{key} (written {written})")
    for k in missing:
        print("guide check: shortcut not in the user guide §10:", k)
    print(f"check-guide: {len(shortcuts())} shortcuts, {len(missing)} missing from the guide")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
