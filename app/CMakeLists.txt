# cutenes: the desktop application (specs/003-qt-frontend). Everything but
# main() is a library, so the gui tests drive the real window in process.

include(FetchContent)

add_library(cutenes-app-lib STATIC
    mainwindow.cpp
    about_dialog.cpp
    cli.cpp
    dialogs/key_bindings_page.cpp
    dialogs/settings_dialog.cpp
    host/gui_host.cpp
    host/servers.cpp
    library/game_identity.cpp
    library/library.cpp
    library/library_browser.cpp
    library/library_index.cpp
    persistence/battery_saver.cpp
    persistence/rewind_buffer.cpp
    persistence/state_slots.cpp
    settings/app_settings.cpp
    settings/key_bindings.cpp)
target_include_directories(cutenes-app-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cutenes-app-lib PUBLIC cutenes-qt cutenes-mcp Qt6::Widgets Qt6::Concurrent)
target_compile_definitions(cutenes-app-lib PUBLIC
    CUTENES_APP_VERSION="${PROJECT_VERSION}")
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

add_executable(cutenes main.cpp)
target_link_libraries(cutenes PRIVATE cutenes-app-lib)
set_target_properties(cutenes PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
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
