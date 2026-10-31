# RFC-0001 — Core API and C ABI

Status: Accepted · Owners: core · Spec: `API-SPEC-core.md` (normative), `CONTRACT-ui-core.md`

## Goal
One portable library (`fkcore`, C++20, no Qt) exposing every product operation; the desktop UI, the
CLI (`fkcli`), tests and future mobile front ends call only this API.

## Shape
- Headers under `core/include/fk/`; one header per module (`image_io.hpp`, `geometry.hpp`, `detection.hpp`,
  `export.hpp`, `relink.hpp`, `composite.hpp`, `project.hpp`, `color.hpp`, `metadata.hpp`, `cache.hpp`,
  `jobs.hpp`, `diagnostics.hpp`) plus `fk/fk.hpp` (umbrella) and `fk/fk_c.h` (C ABI).
- Errors: `fk::Result<T>` (value or `fk::Error{code, message, detail}`); no exceptions cross the API.
  Error codes are a closed, versioned enum (`API-SPEC-core.md §2`).
- Long operations take `const fk::CancelToken&` and `fk::ProgressFn` and are synchronous; threading is the
  caller's choice (RFC-0010 provides `JobQueue`).
- Pixels never cross the API as JSON; images are `fk::ImageBuffer` (owning, interleaved, 8 or 16 bit) or
  streamed through `ImageSource` / `ImageSink`.
- Versioning: `FK_API_VERSION_MAJOR/MINOR`; additive changes bump MINOR; anything else bumps MAJOR and
  needs a migration note. The C ABI is versioned by `fk_api_version()`.

## C ABI (subset, stable)
`fk_api_version`, `fk_last_error`, `fk_scan_open/close/info_json`, `fk_detect_json`,
`fk_export_item`, `fk_composite_item`, `fk_project_load/save`, `fk_free`. JSON strings carry metadata
only; outputs are files. A C test compiling as C11 against the header is planned with the C ABI (not built).

## Compatibility tests
`tests/core/test_api_contract.cpp` pins enum values, signatures and stable names (U027); the JSON of projects, sidecars and templates is pinned by the `Project.*`, `ExportRecords.*` and `Template.*` round trips; the C ABI JSON is planned with the C ABI.
