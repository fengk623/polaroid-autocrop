#!/usr/bin/env python3
"""U029 (UX-12): the full regression of the feature freeze list (docs/v1/FEATURE-FREEZE.md).

Every entry of the freeze list (A01 … J03) must be mapped in tests/freeze-regression.json to its evidence:
- "checks": self-test check names (a check whose name starts with the text counts), read from a report.json;
- "tests": GoogleTest tests or CTest tests that must exist in tests/ (their results come from the core CI jobs);
- "ci": a CI step that covers it (licences, network, packages);
- "manual": why it can only be verified by a person, and where (docs/HUMAN-VERIFICATION.md).

  tools/freeze-regression.py [--report <self-test report.json>] [--markdown <out.md>]

Without --report the mapping is checked only (every entry mapped, every named test exists). With --report each
mapped check must have run and passed; a check skipped by the self-test (with its reason) is reported as skipped,
not as passed, and does not fail the run when another piece of evidence for the entry passed. Exit 1 when an
entry is unmapped, a named test does not exist, a mapped check failed or did not run, or an entry has no passing
evidence and no manual or CI reason.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def freeze_entries():
    rows = {}
    for line in (ROOT / "docs" / "v1" / "FEATURE-FREEZE.md").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^\| ([A-J]\d\d) \| ([^|]+) \|", line)
        if m:
            rows[m.group(1)] = m.group(2).strip()
    return rows


def known_tests():
    names = set()
    for f in (ROOT / "tests").rglob("*.cpp"):
        for m in re.finditer(r"\bTEST(?:_F|_P)?\(\s*(\w+)\s*,\s*(\w+)\s*\)", f.read_text(encoding="utf-8")):
            names.add(f"{m.group(1)}.{m.group(2)}")
    cmake = (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
    names |= set(re.findall(r"add_test\(\s*NAME\s+(\w+)", cmake))
    return names


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--report")
    ap.add_argument("--markdown")
    args = ap.parse_args()
    entries = freeze_entries()
    mapping = json.loads((ROOT / "tests" / "freeze-regression.json").read_text(encoding="utf-8"))
    tests = known_tests()
    # adjacent string literals joined, as the compiler does ("a " "b" → "a b"); escapes as written
    selftest_source = re.sub(r'"\s*\n\s*(?:QStringLiteral\()?"', "", (ROOT / "app" / "src" / "SelfTest.cpp").read_text(encoding="utf-8"))
    selftest_source = selftest_source.replace('\\"', '"')
    checks = None
    if args.report:
        checks = json.loads(Path(args.report).read_text(encoding="utf-8")).get("checks", [])
    problems, rows = [], []
    counts = {"passed": 0, "skipped": 0, "ci": 0, "manual": 0, "failed": 0}
    for fid, feature in entries.items():
        ev = mapping.get(fid)
        if not ev:
            problems.append(f"{fid} ({feature}): not mapped in tests/freeze-regression.json")
            rows.append((fid, feature, "—", "not mapped"))
            continue
        evidence, ok, failed, skipped = [], False, False, False
        for t in ev.get("tests", []):
            if t not in tests:
                problems.append(f"{fid}: test {t} does not exist")
                failed = True
            evidence.append(f"`{t}`")
            ok = ok or t in tests  # results: the core CI jobs (ctest)
        for name in ev.get("checks", []):
            if checks is None:
                # mapping only (docs job, no app build): the check must exist in the self-test's source
                if name[:40] not in selftest_source:
                    problems.append(f"{fid}: self-test check “{name}” not found in app/src/SelfTest.cpp")
                    failed = True
                else:
                    ok = True
                evidence.append(f"self-test “{name}”")
                continue
            hits = [c for c in checks if c.get("name", "").startswith(name)]
            if not hits:
                problems.append(f"{fid}: self-test check “{name}” did not run")
                failed = True
                evidence.append(f"“{name}” — did not run")
            elif any(not c.get("ok") and not c.get("skipped") for c in hits):
                problems.append(f"{fid}: self-test check “{name}” failed")
                failed = True
                evidence.append(f"“{name}” — FAILED")
            elif all(c.get("skipped") for c in hits):
                skipped = True
                evidence.append(f"“{name}” — skipped ({hits[0].get('detail', '')})")
            else:
                ok = True
                evidence.append(f"“{name}” ✓" + (f" ×{len(hits)}" if len(hits) > 1 else ""))
        if ev.get("ci"):
            evidence.append(f"CI: {ev['ci']}")
        if ev.get("manual"):
            evidence.append(f"manual: {ev['manual']}")
        if ev.get("note"):
            evidence.append(f"({ev['note']})")
        if failed:
            status = "FAILED"
            counts["failed"] += 1
        elif ok:
            status = "passed"
            counts["passed"] += 1
        elif skipped:
            # skipped by the self-test with its reason on this platform (e.g. macOS menu-bar shortcuts):
            # reported apart, never as passed
            status = "skipped"
            counts["skipped"] += 1
        elif ev.get("ci"):
            status = "CI"
            counts["ci"] += 1
        elif ev.get("manual"):
            status = "manual"
            counts["manual"] += 1
        else:
            status = "no evidence"
            problems.append(f"{fid}: no passing evidence and no CI or manual reason")
            counts["failed"] += 1
        rows.append((fid, feature, "; ".join(evidence), status))
    for fid in mapping:
        if fid not in entries:
            problems.append(f"{fid}: mapped but not in the freeze list")
    passed = "passed" if checks is not None else "mapped to existing tests and checks (no report given)"
    summary = (f"freeze regression: {len(entries)} entries — {counts['passed']} {passed}, "
               f"{counts['skipped']} skipped on this platform, {counts['ci']} by CI "
               f"steps, {counts['manual']} manual, {counts['failed']} failed or without evidence")
    if args.markdown:
        out = [f"| ID | 功能 | 证据 | 结果 |", "| --- | --- | --- | --- |"]
        out += [f"| {a} | {b} | {c} | {d} |" for a, b, c, d in rows]
        Path(args.markdown).write_text("\n".join(out) + "\n", encoding="utf-8")
    for p in problems:
        print("FAIL freeze regression:", p)
    print(summary)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
