# Mobile portability

ADR-0007. Status: direction only. The core is UI-free and builds as a static library; nothing has been built or
run for iOS or Android, and no capability profile, C ABI or mobile CI job exists yet (planned, FK-013). Mobile
compatibility is not verified.

| Concern | Desktop | Future iOS | Future Android |
| --- | --- | --- | --- |
| Core build | static lib, CMake | CMake iOS toolchain (Xcode generator) → static lib/XCFramework | CMake + NDK (arm64-v8a) → static/shared lib |
| Dependencies | libtiff, lcms2, libpng, libjpeg-turbo, zlib, nlohmann/json | all build with CMake for iOS (no SIMD asm in libjpeg-turbo by our flags) | all build with NDK |
| UI | Qt Quick | Swift/SwiftUI over the C ABI (recommended for App Store licensing) or Qt commercial | Kotlin/Compose over JNI + C ABI, or Qt |
| Files | std::filesystem | security-scoped URLs; app copies/streams into sandbox | Storage Access Framework; file descriptors |
| Memory | budgets in PERFORMANCE-BUDGET | `mobile_lite`: 1.5 GB hard limit; OS kills on overrun | same |
| Disabled in `mobile_lite` | — | BigTIFF output, batch > 8, folder watch, DNG output, large caches | same |
| Formats | project/sidecar/template schemas identical | identical | identical |

Licensing: the project is GPL-3.0-only (ADR-0011, 2026-11-12); its dependencies are permissive. The App Store's
terms add restrictions that the GPL-3.0 does not allow for software others distribute; an App Store build is
possible only from code whose copyright holders grant other terms (with outside contributions, all of them). Qt LGPL
static linking on iOS is a further risk; native UI over the C ABI avoids that part.

CI guard (planned, not built): a job `core-mobile-lite` would run the core tests with `FK_CAPABILITIES=mobile_lite` and the accounted
memory limit enforced (allocations beyond the limit fail with `memory_budget`).
