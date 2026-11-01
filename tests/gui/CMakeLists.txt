# Qt frontend tests (specs/003-qt-frontend, tasks.md), label "gui". They run
# offscreen with SDL's dummy audio driver, so CI needs no display or sound.

add_executable(cutenes-gui-tests
    gui_main.cpp
    test_audio.cpp
    test_battery.cpp
    test_gamepads.cpp
    test_input.cpp
    test_key_bindings.cpp
    test_library.cpp
    test_library_index.cpp
    test_library_window.cpp
    test_menu_bar.cpp
    test_play.cpp
    test_qt_parts.cpp
    test_rewind.cpp
    test_rewind_buffer.cpp
    test_servers.cpp
    test_skeleton.cpp
    test_states.cpp
    test_tools.cpp
    test_video.cpp
    test_region_palette.cpp)
target_link_libraries(cutenes-gui-tests PRIVATE cutenes-app-lib cutenes-httplib gtest)
target_compile_definitions(cutenes-gui-tests PRIVATE
    CUTENES_TEST_ROMS="${PROJECT_SOURCE_DIR}/tests/roms")

gtest_discover_tests(cutenes-gui-tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "gui")
