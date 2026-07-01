cmake_minimum_required(VERSION 3.16)
project(Sc64Gui VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick SerialPort)

qt_standard_project_setup()

# Vendored FatFs (R0.15a) compiled as C; matches the SC64 firmware/menu config.
add_library(fatfs STATIC
    third_party/fatfs/ff.c
    third_party/fatfs/ffsystem.c
    third_party/fatfs/ffunicode.c
)
target_include_directories(fatfs PUBLIC third_party/fatfs)
set_target_properties(fatfs PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

qt_add_executable(sc64gui
    src/main.cpp
    src/sc64device.cpp
    src/sc64controller.cpp
    src/sc64filesystem.cpp
    src/fatfs_glue.cpp
)

qt_add_qml_module(sc64gui
    URI Sc64Gui
    VERSION 1.0
    RESOURCE_PREFIX "/"
    QML_FILES qml/Main.qml
)

target_link_libraries(sc64gui PRIVATE Qt6::Quick Qt6::SerialPort fatfs)

# On Windows, build as a GUI app so no console window appears (Qt supplies WinMain).
if(WIN32)
    set_target_properties(sc64gui PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ---- Packaging / install rules (used by Flatpak and `cmake --install`) ----
set(APP_ID "io.github.example.Sc64Gui" CACHE STRING "Reverse-DNS app id")
include(GNUInstallDirs)

install(TARGETS sc64gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Desktop integration files are Linux-only (Flatpak/AppImage/native installs).
if(UNIX AND NOT APPLE)
    configure_file(${CMAKE_SOURCE_DIR}/packaging/sc64gui.desktop.in
                   ${CMAKE_BINARY_DIR}/${APP_ID}.desktop @ONLY)
    install(FILES ${CMAKE_BINARY_DIR}/${APP_ID}.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

    configure_file(${CMAKE_SOURCE_DIR}/packaging/sc64gui.metainfo.xml.in
                   ${CMAKE_BINARY_DIR}/${APP_ID}.metainfo.xml @ONLY)
    install(FILES ${CMAKE_BINARY_DIR}/${APP_ID}.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

    install(FILES ${CMAKE_SOURCE_DIR}/packaging/sc64gui.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME ${APP_ID}.svg)
endif()
