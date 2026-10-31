#!/usr/bin/env python3
"""Turns a failing build log or JUnit report into GitHub Actions error annotations.

Annotations are readable through the checks API even where raw job logs are not; each annotation carries up to
30 lines (multi-line via %0A), at most 10 per step. Usage: ci-annotate.py <build.log | test-report.xml>
"""
import re
import sys
import xml.etree.ElementTree as ET

# "FAIL <check>" is a self-test check that failed (the summary line only says "self-test FAILED")
PATTERN = re.compile(r"(^FAIL\b|error|Error|FAILED|fatal|undefined reference|unresolved external|LNK\d{4}|C\d{4}:)")


def emit(title, lines):
    for i in range(0, min(len(lines), 300), 30):
        chunk = lines[i:i + 30]
        msg = "%0A".join(l.replace("%", "%25").replace("\r", "").replace("\n", " ")[:1500] for l in chunk)
        print(f"::error title={title} ({i // 30 + 1})::{msg}")


def from_log(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()
    hits = []
    for i, line in enumerate(lines):
        if PATTERN.search(line):
            hits.extend(lines[max(0, i - 1):i + 3])
            if len(hits) > 300:
                break
    emit("build", hits or lines[-60:])


def from_junit(path):
    root = ET.parse(path).getroot()
    out = []
    for case in root.iter("testcase"):
        for tag in ("failure", "error"):
            for f in case.findall(tag):
                out.append(f"{case.get('classname', '')}.{case.get('name', '')}")
                out.extend((f.get("message") or "").splitlines()[:6])
                out.extend((f.text or "").splitlines()[:12])
    emit("tests", out or ["no failing test cases in the report"])


if __name__ == "__main__":
    p = sys.argv[1]
    try:
        from_junit(p) if p.endswith(".xml") else from_log(p)
    except (OSError, ET.ParseError) as e:
        print(f"::error title=annotate::cannot read {p}: {e}")
