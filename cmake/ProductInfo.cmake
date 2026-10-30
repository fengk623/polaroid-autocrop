# Single source of truth for product identity and version (ADR-0012).
# Code reads these through the generated header fk/product_info.hpp.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" FK_VERSION LIMIT_COUNT 1)
# re-run CMake when VERSION changes, so an existing build directory never keeps a stale version
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../VERSION")
string(STRIP "${FK_VERSION}" FK_VERSION)
set(FK_PRODUCT_NAME "FrameKeeper")
set(FK_PRODUCT_ID "framekeeper")
set(FK_EXECUTABLE_NAME "FrameKeeper")
set(FK_CLI_NAME "fkcli")
set(FK_BUNDLE_ID "io.github.fengk623.framekeeper")
set(FK_ORGANIZATION "FrameKeeper")
set(FK_PROJECT_EXT "fkproj")
set(FK_SIDECAR_SUFFIX ".fk.json")
set(FK_TEMPLATE_SUFFIX ".fktemplate.json")
set(FK_XMP_NAMESPACE "https://github.com/fengk623/polaroid-autocrop/ns/1.0/")
set(FK_REPO_URL "https://github.com/fengk623/polaroid-autocrop")
set(FK_MACOS_MIN "12.0")
