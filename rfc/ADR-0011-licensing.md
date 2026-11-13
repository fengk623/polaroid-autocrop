# ADR-0011 — Licensing

Status: Accepted · Date: 2026-10-28 · Project licence decided by the owner on 2026-11-12: **GPL-3.0-only**

## Decision
- Project licence: the GNU General Public License, version 3 only (SPDX `GPL-3.0-only`; `LICENSE` is the FSF text
  unchanged). The owner chose it on 2026-11-12 ("使用 GPL-3.0"); "only" is the literal reading — "or any later
  version" (`GPL-3.0-or-later`) would let others use later GPL versions and is the owner's to choose instead.
- Versions up to 0.7.0-alpha.16 carried an MIT `LICENSE` (proposed 2026-10-28, never confirmed). Copies obtained
  under MIT keep that licence; every version after it is GPL-3.0-only.
- Core dependencies are permissive and statically linked: libtiff (libtiff licence, BSD-like),
  LittleCMS (MIT), libpng (PNG Reference Library License v2), libjpeg-turbo (IJG + BSD-3 + zlib),
  zlib (zlib), nlohmann/json (MIT). Test-only: GoogleTest (BSD-3). All are compatible with the GPL-3.0: their
  notices are kept (`licenses/`), nothing else is required of the combined work.
- Qt 6 is used under LGPL-3.0 with **dynamic** linking (shipped as separate frameworks/DLLs that users may
  replace); LGPL text, Qt source offer/link and our relinking notes are shipped in the app bundle. The LGPL-3.0
  is compatible with the GPL-3.0.
- Source availability (GPL-3.0 §6): every binary release is published together with its complete source — the
  tagged tree and the source archives GitHub attaches to the release page. `licenses/README.md` in each package
  says where.
- No SpektraLab code is reused. SpektraLab is GPL-3.0-or-later, so copying would no longer be a licence conflict,
  but the owner's instruction stands (no code, brands, icons, images or text from SpektraLab or PhotoFrame); both
  are referenced for UI and process only.
- PhotoFrame has no licence file (all rights reserved): only observable behaviour is referenced, no assets or text.
- Mobile: the App Store's terms add restrictions the GPL-3.0 does not allow for software that others distribute;
  an App Store build needs the copyright holders' permission (with outside contributions, all of theirs). iOS Qt
  usually links statically, which complicates LGPL compliance; a native UI over the C ABI avoids that part.
- CI `license-check` fails when a dependency is not listed in THIRD-PARTY-LICENSES.md.
