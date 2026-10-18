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
    unit/test_apu.cpp
    unit/test_controllers.cpp
    unit/test_save_state.cpp)

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

# Tier-1 acceptance gate (constitution Principle III). Every ROM listed
# here is green as of its registration; the set grows as remaining suites
# pass (oam_stress, sprdma, remaining apu_reset, cpu_interrupts_v2 5).
set(TIER1_ROMS
    instr_test-v5/official_only.nes
    instr_test-v5/all_instrs.nes
    instr_timing/instr_timing.nes
    ppu_vbl_nmi/ppu_vbl_nmi.nes
    cpu_reset/ram_after_reset.nes
    oam_read/oam_read.nes
    apu_test/apu_test.nes
    apu_reset/4015_cleared.nes
    apu_reset/irq_flag_cleared.nes)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/roms/other/nestest.nes")
    add_test(NAME nestest-golden-log
        COMMAND rom_harness nestest
                --rom ${CMAKE_CURRENT_SOURCE_DIR}/roms/other/nestest.nes
                --log ${CMAKE_CURRENT_SOURCE_DIR}/roms/other/nestest.log)
    set_tests_properties(nestest-golden-log PROPERTIES LABELS "tier1")
    foreach(rom IN LISTS TIER1_ROMS)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/roms/${rom}")
            get_filename_component(name "${rom}" NAME_WE)
            add_test(NAME "tier1-${name}"
                COMMAND rom_harness suite --rom
                        ${CMAKE_CURRENT_SOURCE_DIR}/roms/${rom})
            set_tests_properties("tier1-${name}" PROPERTIES LABELS "tier1")
        endif()
    endforeach()
endif()
