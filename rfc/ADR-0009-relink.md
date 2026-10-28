# ADR-0009 — External-edit relink mechanism

Status: Accepted · Date: 2026-10-28

## Decision
Four matching sources in order — embedded XMP IDs, sidecar JSON, file-name pattern, manual — followed by a
content check that does not depend on metadata surviving: a 64×64 rank-normalised luminance signature
stored at export time and compared with Pearson correlation (threshold 0.80, measured on fixtures; tone
curves and saturation changes do not alter ranks). Editors often drop or rewrite XMP, so XMP is first
but never the only evidence.

## Consequences
A file that matches by name but fails the content check is shown, not silently accepted.
