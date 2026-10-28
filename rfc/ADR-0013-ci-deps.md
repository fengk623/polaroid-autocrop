# ADR-0013 — CI and dependency management

Status: Accepted · Date: 2026-10-28

## Decision
- Dependencies (except Qt) are built from pinned git tags by the superbuild `cmake/deps` on every
  platform, cached in CI by the hash of `cmake/deps/**`. Same versions locally, in CI and in releases.
- Qt 6.8.3 comes from `jurplel/install-qt-action` (aqtinstall, open-source binaries) in CI; locally any Qt ≥ 6.8 via `CMAKE_PREFIX_PATH`.
- GitHub Actions: `ci.yml` (push/PR: Linux core + Qt app, macOS arm64, Windows x64 MSVC; tests, clang-format
  check, license check, no-network check, mobile_lite job), `nightly.yml` (perf + large fixtures),
  `release.yml` (tag: packages, smoke tests, checksums, draft release).
- vcpkg was considered; rejected for now because the superbuild needs only seven small libraries and gives
  identical flags on all platforms with no extra tool.
