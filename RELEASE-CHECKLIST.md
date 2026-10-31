# RELEASE-CHECKLIST

Automated by `.github/workflows/release.yml` (packaging: `tools/release/package-macos.sh`,
`tools/release/package-windows.sh`; package checks: `tools/release/check-package.py`,
`tools/release/test-macos-dmg.sh`, `tools/release/test-windows-install.sh`). Every pull request into `main` or a
unit branch builds and checks test packages of its head commit. A manual run on `main` with "publish" creates the
tag `v<VERSION>` and the GitHub release (pre-release for a pre-release version), refuses to overwrite an existing
tag or release, and verifies the published files against `SHA256SUMS.txt`. (Until 0.7.0-alpha.15 this paragraph
said the run made a draft for the owner to publish — the workflow had already changed.) A release is only
published when every box is ticked with evidence.

- [ ] `VERSION` bumped (single source); `CHANGELOG.md` entry; release notes in `docs/release-notes/v<VERSION>.md`.
- [ ] CI green on Linux, macOS (arm64), Windows (x64) for the tagged commit.
- [ ] Full test suite: executed/skipped/duration report attached; zero unexpected skips.
- [ ] Detection eval on the CI corpus meets TEST-PLAN thresholds; numbers in Release Notes.
- [ ] Performance table updated for the release commit (`PERFORMANCE-BUDGET.md §3`).
- [ ] Licence check passes (`tools/check-licenses.py`); `THIRD-PARTY-LICENSES.md` matches shipped libraries; the
      full licence texts (`licenses/`) inside every package — checked in the unpacked ZIP, the installed folder and
      the DMG's app (`tools/release/check-package.py`).
- [ ] macOS: `.app` and `.dmg`; arch check (`lipo -archs`); minimum macOS (`LSMinimumSystemVersion`, `vtool`/`otool -l`);
      no non-system dylib outside the bundle (`otool -L`); codesign (ad-hoc unless Developer ID present); notarisation only with credentials.
- [ ] Windows: installer (Inno Setup) + portable ZIP; `dumpbin /dependents` shows only bundled DLLs + system DLLs + VC++ runtime (bundled);
      Authenticode signing only with a certificate. Automated on the clean runner
      (`tools/release/test-windows-install.sh`): fresh install, run and uninstall; upgrade from every published
      release; rollback to the newest one and upgrade again; the user's settings, templates, recovery files and
      projects byte-identical after every step and after uninstalling; no stale plugin files. Real machines: HV-08.
- [ ] SHA-256 checksums file.
- [ ] Smoke test on clean runners as the user receives the packages: the unpacked ZIP, the installed app and the
      app copied out of the mounted DMG (arm64 and x86_64) run `--self-test` on a generated fixture and report the
      package's version and commit (`tools/release/check-report.py`); macOS upgrade/rollback by replacing the app
      (`tools/release/test-macos-dmg.sh`). Reports attached.
- [ ] Signing status stated in the release notes and `docs/INSTALL.md`: Windows not code-signed; macOS ad-hoc
      signed, not notarised (signing needs certificates the owner decides on).
- [ ] `docs/HUMAN-VERIFICATION.md` items reviewed; anything not verified is listed in Release Notes as "not yet verified".
- [ ] Unsigned builds are labelled unsigned with instructions to open them.
