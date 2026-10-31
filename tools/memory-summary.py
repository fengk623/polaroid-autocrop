#!/usr/bin/env python3
"""FK-005: table of `FrameKeeper --self-test DIR --scenario memory` results (scenario-memory.json files).

Usage: memory-summary.py <scenario-memory.json>... [--markdown-out FILE] [--annotate LABEL]
Prints one row per stage and file: RSS, private bytes, accounted buffers, cache (budget), what the cache holds.
--annotate: also GitHub notices for the main stages (readable through the checks API, unlike job logs).
Measurement only: the pass/fail checks are the self-test's "FK-005 …" checks.
"""
import json
import sys


def main(argv):
    out = None
    annotate = None
    files = []
    i = 0
    while i < len(argv):
        if argv[i] == "--markdown-out" and i + 1 < len(argv):
            out = argv[i + 1]
            i += 2
            continue
        if argv[i] == "--annotate" and i + 1 < len(argv):
            annotate = argv[i + 1]
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
        run = f.replace("\\", "/").split("/")[-2] if "/" in f.replace("\\", "/") else f
        for o in d.get("observations", []):
            if annotate and o.get("name") in ("1-detected", "3-full-resolution-each", "6-steady", "7-steady-after-malloc-trim"):
                print(
                    "::notice title=FK-005 memory {lab} {run} {name}::RSS {rss:.0f} MB, private {priv:.0f} MB, "
                    "accounted {acc:.0f} MB, cache {cache:.0f} / {budget:.0f} MB, proxies {prox}, sources {src}, "
                    "previews {prev}, thumbs {thumb:.1f} MB, scans {scans}, prints {prints}".format(
                        lab=annotate,
                        run=run,
                        name=o.get("name"),
                        rss=o.get("residentMB", 0),
                        priv=o.get("privateMB", 0),
                        acc=o.get("accountedMB", 0),
                        cache=o.get("cacheMB", 0),
                        budget=o.get("cacheBudgetMB", 0),
                        prox=o.get("displaysHeld", ""),
                        src=o.get("sourcesHeld", ""),
                        prev=o.get("compositesHeld", ""),
                        thumb=o.get("thumbMB", 0),
                        scans=o.get("scans", ""),
                        prints=o.get("prints", ""),
                    )
                )
            lines.append(
                "| {run} | {name} | {scans} | {prints} | {rss:.0f} | {priv:.0f} | {acc:.0f} | {cache:.0f} / {budget:.0f} | "
                "{prox} | {src} | {prev} | {thumb:.1f} | {ev} |".format(
                    run=run,
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
