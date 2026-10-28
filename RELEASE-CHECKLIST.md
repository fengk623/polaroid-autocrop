# RELEASE-CHECKLIST

Automated by `tools/release/release.sh` (Linux/macOS) and `tools/release/release.ps1` (Windows) and
`.github/workflows/release.yml`. A release is only tagged when every box is ticked with evidence.

- [ ] `VERSION` bumped (single source); `CHANGELOG.md` entry; Release Notes generated (`tools/release/notes.py`).
- [ ] CI green on Linux, macOS (arm64), Windows (x64) for the tagged commit.
- [ ] Full test suite: executed/skipped/duration report attached; zero unexpected skips.
- [ ] Detection eval on the CI corpus meets TEST-PLAN thresholds; numbers in Release Notes.
- [ ] Performance table updated for the release commit (`PERFORMANCE-BUDGET.md §3`).
- [ ] Licence check passes; `THIRD-PARTY-LICENSES.md` matches shipped libraries; licence texts inside packages.
- [ ] macOS: `.app` and `.dmg`; arch check (`lipo -archs`); minimum macOS (`LSMinimumSystemVersion`, `vtool`/`otool -l`);
      no non-system dylib outside the bundle (`otool -L`); codesign (ad-hoc unless Developer ID present); notarisation only with credentials.
- [ ] Windows: installer (Inno Setup) + portable ZIP; `dumpbin /dependents` shows only bundled DLLs + system DLLs + VC++ runtime (bundled);
      Authenticode signing only with a certificate; uninstall keeps user projects.
- [ ] SHA-256 checksums file.
- [ ] Smoke test on clean runners: launch with `--self-test`, open generated fixture, detect, export, composite; report attached.
- [ ] `docs/HUMAN-VERIFICATION.md` items reviewed; anything not verified is listed in Release Notes as "not yet verified".
- [ ] Unsigned builds are labelled unsigned with instructions to open them.
