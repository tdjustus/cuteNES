# Tests for cutenes-core. This directory only builds in standalone/dev mode
# (root CMakeLists guard), so consumers never download GoogleTest.

include(FetchContent)

FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) # MSVC MTd/MDd mismatch fix
FetchContent_MakeAvailable(googletest)

add_executable(cutenes-core-tests
    unit/test_version.cpp)

target_link_libraries(cutenes-core-tests PRIVATE
    cutenes-core::cutenes-core gtest_main)

include(GoogleTest)
gtest_discover_tests(cutenes-core-tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "unit")

# rom_harness (tests/harness/) lands in Phase 2 (task T012); tier-1 ROM
# tests register under the "tier1" label there.
