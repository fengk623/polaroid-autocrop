# PROGRESS

Read this first after any interruption. Newest entry on top.

## Current state
- Phase: 1 (specs) → starting Phase 2 (core)
- Branch: `phase-0-audit` → merging to `main`
- Last full test run: none yet (no product code)
- Blockers: none. GitHub remote `fengk623/polaroid-autocrop` connected (push works).

## Log
### 2026-10-28 — Phase 0 + Phase 1
- Cloned PhotoFrame (02807a0) and SpektraLab (fd891c6) outside the repo; audit notes → `docs/reference-audit.md`, `docs/ui-reference.md`.
- Dev sandbox facts: apt/pip/npm blocked; GitHub git + release downloads work → dependency superbuild from pinned tags; host Qt 6.8.3 built from GitHub sources (minimal modules) for local UI tests.
- Wrote PRD overview + F01–F15, ADR-0001…0014, RFC-0001…0016, API-SPEC-core, CONTRACT-ui-core, TEST-PLAN, PERFORMANCE-BUDGET, RELEASE-CHECKLIST, SECURITY-PRIVACY, THIRD-PARTY-LICENSES, docs/*.
- Next: CMake skeleton + CI skeleton (Linux/macOS/Windows build an empty core), then Phase 2.
