# SECURITY-PRIVACY

- **Offline.** No network code in v1 (no Qt Network module linked, no sockets in core). CI check `check-no-network` inspects linked libraries and symbols.
- **No accounts, no telemetry, no crash upload.**
- **Files.** Source scans opened read-only. Outputs written via temp + rename into folders the user chose. Temp files in a private per-user subfolder, removed after each job and at start.
- **Logs.** Local only, size-capped and rotated; no pixel data; file paths can be anonymised (always anonymised in diagnostic bundles).
- **Diagnostic bundle.** Created only on request, saved where the user chooses, never sent.
- **Metadata privacy.** Location and identity tags are listed for the user before export; presets keep all safe / remove location / remove identity.
- **Parsing untrusted files.** TIFF/PNG/JPEG decoding uses maintained libraries at pinned versions; integrity checks run before full decoding; sizes are validated against memory budgets before allocation (no unbounded allocation from header fields).
- **Supply chain.** Dependencies pinned to tags in `cmake/deps/CMakeLists.txt`; changes reviewed with licence updates.
- **Reporting.** Security issues: open a private GitHub security advisory on the repository.
