# Tests for cutenes-core. This directory only builds in standalone/dev mode
# (root CMakeLists guard), so consumers never download GoogleTest.

include(FetchContent)

FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
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
    unit/test_save_state.cpp
    unit/test_full_control.cpp
    unit/test_mappers.cpp
    unit/test_mappers_discrete.cpp)

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
# pass.
# The ppu_vbl_nmi and apu_test entries are the standalone sub-test ROMs:
# the multi-test builds can mask sub-test failures behind their bank layout.
set(TIER1_ROMS
    instr_test-v5/official_only.nes
    instr_test-v5/all_instrs.nes
    instr_timing/instr_timing.nes
    ppu_vbl_nmi/rom_singles/01-vbl_basics.nes
    ppu_vbl_nmi/rom_singles/02-vbl_set_time.nes
    ppu_vbl_nmi/rom_singles/04-nmi_control.nes
    ppu_vbl_nmi/rom_singles/05-nmi_timing.nes
    ppu_vbl_nmi/rom_singles/06-suppression.nes
    ppu_vbl_nmi/rom_singles/07-nmi_on_timing.nes
    ppu_vbl_nmi/rom_singles/08-nmi_off_timing.nes
    ppu_vbl_nmi/rom_singles/09-even_odd_frames.nes
    ppu_vbl_nmi/rom_singles/10-even_odd_timing.nes
    cpu_reset/ram_after_reset.nes
    cpu_interrupts_v2/rom_singles/1-cli_latency.nes
    cpu_interrupts_v2/rom_singles/2-nmi_and_brk.nes
    cpu_interrupts_v2/rom_singles/3-nmi_and_irq.nes
    cpu_interrupts_v2/rom_singles/4-irq_and_dma.nes
    cpu_interrupts_v2/rom_singles/5-branch_delays_irq.nes
    oam_read/oam_read.nes
    oam_stress/oam_stress.nes
    apu_test/rom_singles/2-len_table.nes
    apu_test/rom_singles/3-irq_flag.nes
    apu_test/rom_singles/4-jitter.nes
    apu_test/rom_singles/6-irq_flag_timing.nes
    apu_reset/4015_cleared.nes
    apu_reset/irq_flag_cleared.nes
    apu_reset/4017_written.nes
    apu_reset/4017_timing.nes
    apu_reset/len_ctrs_enabled.nes
    apu_reset/works_immediately.nes
    ppu_vbl_nmi/rom_singles/03-vbl_clear_time.nes
    apu_test/rom_singles/7-dmc_basics.nes
    apu_test/rom_singles/8-dmc_rates.nes
    apu_test/rom_singles/1-len_ctr.nes
    apu_test/rom_singles/5-len_timing.nes
    sprdma_and_dmc_dma/sprdma_and_dmc_dma.nes
    sprdma_and_dmc_dma/sprdma_and_dmc_dma_512.nes
    ppu_open_bus/ppu_open_bus.nes
    cpu_exec_space/test_cpu_exec_space_apu.nes
    cpu_exec_space/test_cpu_exec_space_ppuio.nes
    ppu_read_buffer/test_ppu_read_buffer.nes
    mmc3_test_2/rom_singles/4-scanline_timing.nes)

# All formerly known-failing ROMs have been fixed and promoted to tier 1
# (see specs/001-emulation-core/analysis/FIX_ATTEMPTS.md).
set(KNOWN_FAILING_ROMS)
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
    foreach(rom IN LISTS KNOWN_FAILING_ROMS)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/roms/${rom}")
            get_filename_component(name "${rom}" NAME_WE)
            add_test(NAME "known-failing-${name}"
                COMMAND rom_harness suite --rom
                        ${CMAKE_CURRENT_SOURCE_DIR}/roms/${rom})
            set_tests_properties("known-failing-${name}" PROPERTIES
                LABELS "known-failing")
        endif()
    endforeach()
endif()

# Cross-platform golden hashes (T065): vendored ROMs only, so CI (which
# has no roms-local/) can compare every OS against the checked-in baseline.
add_test(NAME golden-hashes
    COMMAND rom_harness golden --list ${CMAKE_CURRENT_SOURCE_DIR}/golden/roms.txt
            --check ${CMAKE_CURRENT_SOURCE_DIR}/golden/baseline.txt)
set_tests_properties(golden-hashes PROPERTIES LABELS "tier1")

# Behavioral guarantees, checked end to end on vendored ROMs that cover each
# supported mapper family (NROM, MMC1, MMC3, MMC5, VRC2a). Vendored only, so
# these run in CI too.
#   determinism - two fresh runs are byte-identical (video and audio)
#   savestate   - restore reproduces the uninterrupted run's video, audio
#                 and final machine state
#   step-equiv  - per-instruction stepping yields the same frames as runFrames
set(BEHAVIOR_ROMS
    nestest:other/nestest.nes
    mmc1:MMC1_A12/mmc1_a12.nes
    mmc3:mmc3_test/1-clocking.nes
    mmc5:mmc5test/mmc5test.nes
    mmc5_exram:exram/mmc5exram.nes
    vrc2a:m22chrbankingtest/0-127.nes)
foreach(entry IN LISTS BEHAVIOR_ROMS)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 tag)
    list(GET parts 1 rel)
    set(rom ${CMAKE_CURRENT_SOURCE_DIR}/roms/${rel})
    if(EXISTS "${rom}")
        add_test(NAME "determinism-${tag}"
            COMMAND rom_harness determinism --rom ${rom} --frames 300)
        add_test(NAME "savestate-${tag}"
            COMMAND rom_harness savestate --rom ${rom} --at 120 --rerun-to 300)
        add_test(NAME "step-equiv-${tag}"
            COMMAND rom_harness step-equiv ${rom} 60)
        set_tests_properties("determinism-${tag}" "savestate-${tag}"
            "step-equiv-${tag}" PROPERTIES LABELS "behavior")
    endif()
endforeach()

# Scripted checks (T059/T046/T058), one ctest test per playtest entry so a
# failure names the entry and ctest -j parallelizes them. Entries whose
# commercial ROM is absent from roms-local/ exit 77 and show as Skipped.
function(register_playtests list_file labels)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${list_file})
    file(STRINGS ${list_file} entry_lines REGEX "^entry ")
    get_filename_component(list_name ${list_file} NAME_WE)
    foreach(line IN LISTS entry_lines)
        string(REGEX REPLACE "^entry[ \t]+([^ \t]+).*" "\\1" entry_name "${line}")
        # Test-name prefix keeps known-failing entries distinct from passing ones.
        if("known-failing" IN_LIST labels)
            set(test_name "known-failing-${entry_name}")
        else()
            set(test_name "playtest-${entry_name}")
        endif()
        add_test(NAME "${test_name}"
            COMMAND rom_harness playtest --list ${list_file} --entry ${entry_name})
        set_tests_properties("${test_name}" PROPERTIES
            LABELS "${labels}" SKIP_RETURN_CODE 77)
    endforeach()
endfunction()

register_playtests(${CMAKE_CURRENT_SOURCE_DIR}/playtest/playtest.txt "playtest")
# Holy Mapperel: tier-2 mapper verification asserting the result screen text.
register_playtests(${CMAKE_CURRENT_SOURCE_DIR}/playtest/mapperel.txt "playtest;tier2")
