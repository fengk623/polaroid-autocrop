# ADR-0007 — Mobile architecture

Status: Accepted · Date: 2026-10-28

## Decision
- One core source tree (`core/`), no Qt, no OS UI APIs; builds as a static library for any platform.
- Capability profile chosen at runtime (`fk::Capabilities`), with compile-time defaults:
  `desktop_full` (all features) and `mobile_lite` (no BigTIFF output, batch ≤ 8 scans, no folder watch,
  no DNG output, cache ≤ 256 MB, tile cache ≤ 32 MB, hard memory limit 1.5 GB).
  Disabled features return `Errc::capability`; formats and schemas are identical.
- A stable C ABI (`core/include/fk/fk_c.h`, RFC-0001) wraps the main flows for future Swift/Kotlin bindings.
- CI job `core-mobile-lite` runs the core test suite with the `mobile_lite` profile and the 1.5 GB memory limit enforced by the core's allocator accounting.

## Consequences
A mobile UI will be new (touch-first) but reuses all geometry, detection, export, relink, compositing and file formats.
