#!/usr/bin/env python3
"""Checks a packaged app's self-test report (FK-014): the app must report the VERSION file's version and the
commit the package was built from, and the self-test must have passed.
Usage: check-report.py <report.json> <expected-version> <expected-commit-sha>"""
import json
import sys


def main() -> int:
    path, ver, sha = sys.argv[1], sys.argv[2], sys.argv[3]
    r = json.load(open(path, encoding="utf-8"))
    problems = []
    if r.get("version") != ver:
        problems.append(f"app reports version {r.get('version')!r}, package is {ver!r}")
    if r.get("commit") != sha:
        problems.append(f"app reports commit {r.get('commit')!r}, package source is {sha!r}")
    if not r.get("ok"):
        problems.append("self-test did not pass")
    known = [k for k in r.get("knownIssues", []) if k.get("status") == "reproduced"]
    print(f"{path}: version {r.get('version')}, commit {r.get('commit')}, build {r.get('build')}, "
          f"{len(r.get('checks', []))} checks, {len(known)} known issues reproduced")
    for p in problems:
        print(f"::error::{p}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
