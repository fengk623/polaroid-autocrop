# FrameKeeper desktop app (RFC-0011): Qt Quick, Basic style + own theme, one QML module.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(FrameKeeper WIN32 MACOSX_BUNDLE src/main.cpp)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(FrameKeeper
  URI FrameKeeper
  VERSION 1.0
  QML_FILES
    qml/Main.qml qml/Theme.qml qml/Icon.qml qml/FkButton.qml qml/ToolBtn.qml qml/Segmented.qml
    qml/Section.qml qml/FieldRow.qml qml/NumField.qml qml/FkCombo.qml qml/FkCheck.qml qml/Badge.qml
    qml/TopBar.qml qml/LeftRail.qml qml/RightRail.qml qml/Filmstrip.qml qml/CanvasArea.qml qml/RailGrip.qml
    qml/FkDialog.qml qml/ExportDialog.qml qml/RelinkDialog.qml qml/FinalDialog.qml qml/SettingsDialog.qml
    qml/AboutDialog.qml qml/NameDialog.qml qml/Toasts.qml qml/AppMenuBar.qml qml/MacMenuBar.qml qml/FkSpin.qml
  SOURCES
    src/AppController.hpp src/AppController.cpp src/Models.hpp src/Models.cpp src/Jobs.hpp src/Jobs.cpp
    src/Display.hpp src/Display.cpp src/ScanCanvas.hpp src/ScanCanvas.cpp src/ImageProvider.hpp
    src/ImageProvider.cpp src/Translator.hpp src/Translator.cpp src/SelfTest.hpp src/SelfTest.cpp
    src/util.hpp src/util.cpp src/ShiftTap.hpp src/ShiftTap.cpp
  RESOURCES
    resources/icon.png
    i18n/zh_CN.json
)

target_include_directories(FrameKeeper PRIVATE src)
# core headers use names such as `slots`: keep Qt's lowercase keywords off (Q_SIGNALS / Q_EMIT)
target_compile_definitions(FrameKeeper PRIVATE QT_NO_KEYWORDS)
target_link_libraries(FrameKeeper PRIVATE fk::core Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)

set_target_properties(FrameKeeper PROPERTIES
  OUTPUT_NAME "${FK_EXECUTABLE_NAME}"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER "${FK_BUNDLE_ID}"
  MACOSX_BUNDLE_BUNDLE_NAME "${FK_PRODUCT_NAME}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${FK_VERSION_NUMERIC}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${FK_VERSION_NUMERIC}"
  MACOSX_BUNDLE_ICON_FILE "FrameKeeper.icns"
)
if(APPLE)
  set(_icns "${CMAKE_CURRENT_SOURCE_DIR}/resources/FrameKeeper.icns")
  target_sources(FrameKeeper PRIVATE "${_icns}")
  set_source_files_properties("${_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
elseif(WIN32)
  configure_file(packaging/windows/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
  target_sources(FrameKeeper PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

install(TARGETS FrameKeeper BUNDLE DESTINATION . RUNTIME DESTINATION .)
