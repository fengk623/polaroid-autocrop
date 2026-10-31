#!/usr/bin/env python3
"""Summarise a CTest JUnit report by outcome (FK-013): passed, failed, skipped, not started, blocked.

- passed / failed: the test ran.
- skipped: the test started and skipped itself (GTEST_SKIP). Allowed only when listed in tests/allowed-skips.txt
  with a reason; any other skip fails the report (a missing fixture must not look like a pass, AGENTS.md T1).
- not started: CTest could not start it (status "notrun": missing executable, missing permission, …) or it is
  disabled. Never counted as passed; fails the report.
- blocked: listed in tests/allowed-skips.txt with "blocked:" in its reason (an environment the CI cannot provide);
  reported apart, not counted as passed.

Usage: test_report.py <junit.xml> <allowed-skips.txt> <wall seconds>. Exit 1 on failed, not started or unexpected
skips, or when nothing ran.
"""
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def main() -> int:
    junit, allowed_file, wall = sys.argv[1], sys.argv[2], sys.argv[3]
    allowed, blocked_names = set(), set()
    p = Path(allowed_file)
    if p.exists():
        reason = ""
        for line in p.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                reason = line
                continue
            allowed.add(line)
            if "blocked:" in reason.lower():
                blocked_names.add(line)
    root = ET.parse(junit).getroot()
    passed, failed, skipped, not_started, blocked, unexpected = [], [], [], [], [], []
    for c in root.iter("testcase"):
        name = c.get("name", "?")
        status = c.get("status", "")
        if status in ("notrun", "disabled"):
            (blocked if name in blocked_names else not_started).append(name)
        elif c.find("failure") is not None or c.find("error") is not None:
            failed.append(name)
        elif c.find("skipped") is not None:
            if name in blocked_names:
                blocked.append(name)
            else:
                skipped.append(name)
                if name not in allowed:
                    unexpected.append(name)
        else:
            passed.append(name)
    print(f"TEST REPORT: passed={len(passed)} failed={len(failed)} skipped={len(skipped)} "
          f"not_started={len(not_started)} blocked={len(blocked)} wall_seconds={wall}")
    for title, names in (("FAILED", failed), ("NOT STARTED (never counted as passed)", not_started),
                         ("BLOCKED (environment; not counted as passed)", blocked),
                         ("UNEXPECTED SKIPS (fixture missing? see AGENTS.md T1)", unexpected)):
        if names:
            print(title + ":")
            for n in names:
                print("  -", n)
    if not passed:
        print("NO TESTS PASSED")
        return 1
    return 1 if (failed or not_started or unexpected) else 0


if __name__ == "__main__":
    sys.exit(main())
