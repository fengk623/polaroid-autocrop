#!/usr/bin/env python3
"""Summarise a CTest JUnit report: executed, skipped, failed, duration. Exit 1 on unexpected skips."""
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

def main() -> int:
    junit, allowed_file, wall = sys.argv[1], sys.argv[2], sys.argv[3]
    allowed = set()
    p = Path(allowed_file)
    if p.exists():
        allowed = {l.strip() for l in p.read_text(encoding="utf-8").splitlines() if l.strip() and not l.startswith("#")}
    root = ET.parse(junit).getroot()
    cases = root.iter("testcase")
    executed = skipped = failed = 0
    unexpected = []
    for c in cases:
        name = c.get("name", "?")
        if c.find("skipped") is not None or c.get("status") in ("notrun", "disabled"):
            skipped += 1
            if name not in allowed:
                unexpected.append(name)
            continue
        executed += 1
        if c.find("failure") is not None or c.find("error") is not None:
            failed += 1
    print(f"TEST REPORT: executed={executed} skipped={skipped} failed={failed} wall_seconds={wall}")
    if unexpected:
        print("UNEXPECTED SKIPS (fixture missing? see AGENTS.md T1):")
        for n in unexpected:
            print("  -", n)
        return 1
    if executed == 0:
        print("NO TESTS EXECUTED")
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
