#!/usr/bin/env python3
"""FK-005: table of `FrameKeeper --self-test DIR --scenario memory` results (scenario-memory.json files).

Usage: memory-summary.py <scenario-memory.json>... [--markdown-out FILE]
Prints one row per stage and file: RSS, private bytes, accounted buffers, cache (budget), what the cache holds.
Measurement only: the pass/fail checks are the self-test's "FK-005 …" checks.
"""
import json
import sys


def main(argv):
    out = None
    files = []
    i = 0
    while i < len(argv):
        if argv[i] == "--markdown-out" and i + 1 < len(argv):
            out = argv[i + 1]
            i += 2
            continue
        files.append(argv[i])
        i += 1
    lines = [
        "| run | stage | scans | prints | RSS MB | private MB | accounted MB | cache / budget MB | proxies | sources | "
        "previews | thumbs MB | evictions |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for f in files:
        try:
            d = json.load(open(f, encoding="utf-8"))
        except (OSError, ValueError) as e:
            lines.append(f"| {f} | unreadable: {e} | | | | | | | | | | | |")
            continue
        for o in d.get("observations", []):
            lines.append(
                "| {run} | {name} | {scans} | {prints} | {rss:.0f} | {priv:.0f} | {acc:.0f} | {cache:.0f} / {budget:.0f} | "
                "{prox} | {src} | {prev} | {thumb:.1f} | {ev} |".format(
                    run=f.replace("\\", "/").split("/")[-2] if "/" in f.replace("\\", "/") else f,
                    name=o.get("name", "?"),
                    scans=o.get("scans", ""),
                    prints=o.get("prints", ""),
                    rss=o.get("residentMB", 0),
                    priv=o.get("privateMB", 0),
                    acc=o.get("accountedMB", 0),
                    cache=o.get("cacheMB", 0),
                    budget=o.get("cacheBudgetMB", 0),
                    prox=o.get("displaysHeld", ""),
                    src=o.get("sourcesHeld", ""),
                    prev=o.get("compositesHeld", ""),
                    thumb=o.get("thumbMB", 0),
                    ev=o.get("evictions", ""),
                )
            )
    text = "\n".join(lines) + "\n"
    sys.stdout.write(text)
    if out:
        with open(out, "a", encoding="utf-8") as fh:
            fh.write("### Memory batch (FK-005)\n\n" + text + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
