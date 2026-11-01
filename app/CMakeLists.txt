# cutenes: the desktop application (specs/003-qt-frontend). Everything but
# main() is a library, so the gui tests drive the real window in process.

include(FetchContent)

add_library(cutenes-app-lib STATIC
    mainwindow.cpp
    about_dialog.cpp
    cli.cpp
    dialogs/key_bindings_page.cpp
    dialogs/settings_dialog.cpp
    dialogs/cheats_window.cpp
    dialogs/debugger_window.cpp
    dialogs/memory_viewer.cpp
    dialogs/ppu_viewer.cpp
    dialogs/trace_logger.cpp
    dialogs/user_guide.cpp
    debug/ppu_render.cpp
    host/gui_host.cpp
    host/servers.cpp
    library/game_identity.cpp
    log/app_log.cpp
    library/library.cpp
    library/library_browser.cpp
    library/library_index.cpp
    media/wav_writer.cpp
    media/palettes.cpp
    media/movie.cpp
    cheats/cheat_list.cpp
    cheats/cheat_search.cpp
    persistence/battery_saver.cpp
    persistence/rewind_buffer.cpp
    persistence/state_slots.cpp
    settings/app_settings.cpp
    settings/key_bindings.cpp
    update/update_feed.cpp
    update/update_check.cpp
    update/update_prompt.cpp
    update/update_install.cpp
    update/update_download.cpp)
target_include_directories(cutenes-app-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cutenes-app-lib PUBLIC cutenes-qt cutenes-mcp Qt6::Widgets Qt6::Concurrent Qt6::Network)
# Which release package this build is (research R16): the CI package job
# sets macos-zip, windows-zip or appimage, and only those can install
# updates. Every other build (a developer's, a distribution's) is none.
set(CUTENES_PACKAGE "none" CACHE STRING "Release package type: none, macos-zip, windows-zip or appimage")
set_property(CACHE CUTENES_PACKAGE PROPERTY STRINGS none macos-zip windows-zip appimage)
target_compile_definitions(cutenes-app-lib PUBLIC
    CUTENES_APP_VERSION="${PROJECT_VERSION}"
    CUTENES_PACKAGE="${CUTENES_PACKAGE}")
set_target_properties(cutenes-app-lib PROPERTIES AUTOMOC ON)

# The license texts About shows (constitution: Qt under LGPL-3.0 with its
# notices; research R1). Dependencies fetched at configure time supply their
# own files.
FetchContent_GetProperties(nlohmann_json SOURCE_DIR json_dir)
FetchContent_GetProperties(httplib SOURCE_DIR httplib_dir)
set(license_files
    ${PROJECT_SOURCE_DIR}/LICENSE
    ${PROJECT_SOURCE_DIR}/third_party/qt/LGPL-3.0.txt
    ${PROJECT_SOURCE_DIR}/third_party/qt/GPL-3.0.txt
    ${PROJECT_SOURCE_DIR}/third_party/sdl3/LICENSE.txt
    ${json_dir}/LICENSE.MIT
    ${httplib_dir}/LICENSE)
set(license_aliases cutenes.txt qt-lgpl-3.0.txt qt-gpl-3.0.txt sdl3.txt
    nlohmann-json.txt cpp-httplib.txt)
foreach(file alias IN ZIP_LISTS license_files license_aliases)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${alias})
endforeach()
qt_add_resources(cutenes-app-lib licenses PREFIX /licenses FILES ${license_files})

# Help > User Guide shows docs/user-guide.md, built in so it works offline
# and always matches the version that shows it.
set_source_files_properties(${PROJECT_SOURCE_DIR}/docs/user-guide.md PROPERTIES QT_RESOURCE_ALIAS user-guide.md)
qt_add_resources(cutenes-app-lib docs PREFIX /docs FILES ${PROJECT_SOURCE_DIR}/docs/user-guide.md)

# The icon (packaging/icons, from cutenes.svg by make-icons.sh): the window's
# from a resource, the bundle's (.icns) on macOS, the executable's (.ico)
# on Windows.
set(icon_dir ${PROJECT_SOURCE_DIR}/packaging/icons)
set_source_files_properties(${icon_dir}/cutenes.png PROPERTIES QT_RESOURCE_ALIAS cutenes.png)
qt_add_resources(cutenes-app-lib icons PREFIX /icons FILES ${icon_dir}/cutenes.png)
set(app_sources main.cpp)
if(WIN32)
    set(CUTENES_ICO ${icon_dir}/cutenes.ico)
    configure_file(cutenes.rc.in ${CMAKE_CURRENT_BINARY_DIR}/cutenes.rc @ONLY)
    list(APPEND app_sources ${CMAKE_CURRENT_BINARY_DIR}/cutenes.rc)
elseif(APPLE)
    set_source_files_properties(${icon_dir}/cutenes.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND app_sources ${icon_dir}/cutenes.icns)
endif()

add_executable(cutenes ${app_sources})
target_link_libraries(cutenes PRIVATE cutenes-app-lib)
set_target_properties(cutenes PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_ICON_FILE cutenes.icns
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME cuteNES
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.tdjustus.cutenes
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

foreach(target cutenes-app-lib cutenes)
    if(MSVC)
        target_compile_options(${target} PRIVATE /permissive- /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wconversion -Wshadow -Werror)
    endif()
endforeach()

# Installing (research R13): `cmake --install build --component app
# --prefix <stage>` lays out a runnable package, Qt included, which the CI
# package job zips (or, on Linux, turns into an AppImage with linuxdeploy).
# The component leaves out the core's and test libraries' own install rules
# (headers, static libraries). Qt's deploy script runs windeployqt or
# macdeployqt; on Linux linuxdeploy does that work. cutenes-server, for MCP
# clients (`cutenes-server --attach`), goes beside the app: inside the
# bundle on macOS. The licenses go with them.
if(WIN32)
    # One flat folder: Windows finds DLLs beside the executable (not in a
    # bin/ under it), and Qt's deploy script puts Qt, qt.conf and plugins/
    # relative to CMAKE_INSTALL_BINDIR. With the default bin/, Qt landed
    # in bin/ and cutenes.exe at the top, and it started only where Qt was
    # on PATH, as on the CI runner.
    set(CMAKE_INSTALL_BINDIR . CACHE PATH "" FORCE)
endif()
include(GNUInstallDirs)
if(APPLE)
    # A Qt from Qt's own installer (as CI uses) names its frameworks
    # @rpath/..., and installing drops the build tree's rpath: the bundle
    # looks in its own Frameworks folder, where macdeployqt puts them.
    set_target_properties(cutenes PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
    install(TARGETS cutenes BUNDLE DESTINATION . COMPONENT app)
    install(TARGETS cutenes-server RUNTIME DESTINATION cutenes.app/Contents/MacOS COMPONENT app)
    set(license_destination cutenes.app/Contents/Resources/licenses)
elseif(WIN32)
    install(TARGETS cutenes cutenes-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT app)
    # The MSVC runtime DLLs beside the executables, so the zip runs on a
    # machine without the Visual C++ redistributable.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT app)
    include(InstallRequiredSystemLibraries)
    set(license_destination licenses)
else()
    install(TARGETS cutenes cutenes-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT app)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/io.github.tdjustus.cutenes.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT app)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/cutenes-nes.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages COMPONENT app)
    install(FILES ${icon_dir}/cutenes.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps COMPONENT app)
    set(license_destination ${CMAKE_INSTALL_DATADIR}/licenses/cutenes)
endif()
foreach(file alias IN ZIP_LISTS license_files license_aliases)
    install(FILES ${file} DESTINATION ${license_destination} RENAME ${alias} COMPONENT app)
endforeach()
if(APPLE OR WIN32)
    # -libpath: with the build tree's rpath gone, macdeployqt is told where
    # the @rpath frameworks it copies come from.
    set(deploy_options "")
    if(APPLE)
        set(deploy_options DEPLOY_TOOL_OPTIONS "-libpath=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}")
    else()
        # The runtime DLLs are installed above; windeployqt would add the
        # 18 MB vc_redist installer as well.
        set(deploy_options DEPLOY_TOOL_OPTIONS "--no-compiler-runtime")
    endif()
    qt_generate_deploy_app_script(TARGET cutenes
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        ${deploy_options})
    install(SCRIPT ${deploy_script} COMPONENT app)
endif()
