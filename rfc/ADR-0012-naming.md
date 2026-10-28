# ADR-0012 — Product naming

Status: Accepted · Date: 2026-10-28

## Decision
Product name **FrameKeeper** ("keeps the real frame"). Identifiers derived from one source of truth,
`cmake/ProductInfo.cmake`:
`FK_PRODUCT_NAME=FrameKeeper`, executable `FrameKeeper`, CLI `fkcli`, C++ namespace `fk`, bundle id
`io.github.fengk623.framekeeper`, project extension `.fkproj`, sidecar suffix `.fk.json`, template
schema `fk.template/1`, XMP namespace `https://github.com/fengk623/polaroid-autocrop/ns/1.0/` (prefix `fk`).
Repository name `polaroid-autocrop` was chosen by the owner and is kept; it describes what the tool works on.
"Polaroid" / "Instax" appear only descriptively ("for scans of Polaroid and Instax prints").
A quick web search on 2026-10-28 found no identical photo app; this is not a trademark clearance.

## Consequences
Renaming = edit `cmake/ProductInfo.cmake` (+ icon); code uses the generated header.
