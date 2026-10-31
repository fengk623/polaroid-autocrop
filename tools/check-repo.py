#!/usr/bin/env python3
"""Fails if tracked files include large binaries, scans, signing material or build output (AGENTS.md §4)."""
import subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
files = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True, check=True).stdout.split("\n")
FORBIDDEN_EXT = {".p12", ".pfx", ".cer", ".mobileprovision", ".pem", ".dmg", ".msi"}
bad = []
for f in filter(None, files):
    p = ROOT / f
    if not p.exists():
        continue
    size = p.stat().st_size
    ext = p.suffix.lower()
    if ext in FORBIDDEN_EXT:
        bad.append(f"{f}: forbidden file type")
    if ext in {".pyc", ".pyo"} or "__pycache__/" in f:
        bad.append(f"{f}: interpreter cache (build output)")
    if ext in {".tif", ".tiff", ".dng"} and not f.startswith("tests/data/tiny/"):
        bad.append(f"{f}: scans/TIFFs must not be committed (generate fixtures instead)")
    if size > 1_000_000 and not f.startswith("app/resources/"):
        bad.append(f"{f}: {size} bytes > 1 MB")
for b in bad:
    print("repo check:", b)
sys.exit(1 if bad else 0)
