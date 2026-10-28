# ADR-0011 — Licensing

Status: Accepted (pending owner confirmation of MIT) · Date: 2026-10-28

## Decision
- Project licence: MIT.
- Core dependencies are permissive and statically linked: libtiff (libtiff licence, BSD-like),
  LittleCMS (MIT), libpng (PNG Reference Library License v2), libjpeg-turbo (IJG + BSD-3 + zlib),
  zlib (zlib), nlohmann/json (MIT). Test-only: GoogleTest (BSD-3).
- Qt 6 is used under LGPL-3.0 with **dynamic** linking (shipped as separate frameworks/DLLs that users may
  replace); LGPL text, Qt source offer/link and our relinking notes are shipped in the app bundle.
- No GPL dependency (Exiv2 excluded; no SpektraLab code reused — SpektraLab is GPL-3.0-or-later and is
  referenced for UI and process only).
- PhotoFrame has no licence file (all rights reserved): only observable behaviour is referenced, no assets or text.
- Mobile: iOS Qt usually links statically, which complicates LGPL compliance; a future iOS app either uses a
  native UI over the C ABI or a commercial Qt licence. The core stays MIT-compatible either way.
- CI `license-check` fails when a dependency is not listed in THIRD-PARTY-LICENSES.md.
