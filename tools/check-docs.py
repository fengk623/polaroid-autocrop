#!/usr/bin/env python3
"""Checks that relative links in Markdown files point to existing files (spec drift guard)."""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"\]\(([^)#\s]+)(#[^)]*)?\)")
bad = []
for md in ROOT.rglob("*.md"):
    if any(part.startswith(("build", ".git")) for part in md.relative_to(ROOT).parts):
        continue
    # licence texts copied verbatim from upstream keep their own links (licenses/README.md is ours)
    if md.parent == ROOT / "licenses" and md.name != "README.md":
        continue
    for m in LINK.finditer(md.read_text(encoding="utf-8")):
        target = m.group(1)
        if "://" in target or target.startswith("mailto:"):
            continue
        if not (md.parent / target).exists():
            bad.append(f"{md.relative_to(ROOT)} -> {target}")
for b in bad:
    print("broken link:", b)
sys.exit(1 if bad else 0)
