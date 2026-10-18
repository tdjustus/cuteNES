# Tests for cutenes-core. This directory only builds in standalone/dev mode
# (root CMakeLists guard), so consumers never download GoogleTest.

include(FetchContent)

FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) # MSVC MTd/MDd mismatch fix
FetchContent_MakeAvailable(googletest)

add_executable(cutenes-core-tests
    unit/test_version.cpp
    unit/test_scheduler.cpp
    unit/test_bus.cpp
    unit/test_ines.cpp
    unit/test_cpu.cpp
    unit/test_ppu.cpp
    unit/test_apu.cpp)

target_link_libraries(cutenes-core-tests PRIVATE
    cutenes-core::cutenes-core cutenes-core-internal gtest_main)

include(GoogleTest)
gtest_discover_tests(cutenes-core-tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "unit")

# Headless verification driver (quickstart.md scenarios).
add_executable(rom_harness
    harness/main.cpp
    harness/nestest.cpp)

target_link_libraries(rom_harness PRIVATE
    cutenes-core::cutenes-core cutenes-core-internal)

# Tier-1 acceptance gate (constitution Principle III): nestest golden log.
# Registered only when the test-ROM submodule is populated.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/roms/other/nestest.nes")
    add_test(NAME nestest-golden-log
        COMMAND rom_harness nestest
                --rom ${CMAKE_CURRENT_SOURCE_DIR}/roms/other/nestest.nes
                --log ${CMAKE_CURRENT_SOURCE_DIR}/roms/other/nestest.log)
    set_tests_properties(nestest-golden-log PROPERTIES LABELS "tier1")
endif()
