# cutenes-core: the NES emulation core library (constitution Principle II:
# standalone, headless, zero runtime dependencies beyond the C++20 stdlib).

add_library(cutenes-core
    src/cutenes/version.cpp
    src/cutenes/system/region.cpp
    src/cutenes/system/scheduler.cpp
    src/cutenes/system/machine.cpp
    src/cutenes/bus/bus.cpp
    src/cutenes/serialize/save_state.cpp
    src/cutenes/cartridge/ines.cpp
    src/cutenes/cartridge/cart.cpp
    src/cutenes/cpu/cpu.cpp
    src/cutenes/cpu/trace.cpp
    src/cutenes/ppu/ppu.cpp
    src/cutenes/apu/apu.cpp
    src/cutenes/mappers/mapper.cpp
    src/cutenes/mappers/nrom.cpp
    src/cutenes/mappers/discrete.cpp
    src/cutenes/mappers/action53.cpp
    src/cutenes/mappers/mmc5.cpp
    src/cutenes/mappers/mmc1.cpp
    src/cutenes/mappers/uxrom_cnrom.cpp
    src/cutenes/mappers/mmc3.cpp)

add_library(cutenes-core::cutenes-core ALIAS cutenes-core)

target_compile_features(cutenes-core PUBLIC cxx_std_20)

# Included early: CMAKE_INSTALL_INCLUDEDIR must be defined when the
# INSTALL_INTERFACE generator expression below is parsed.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

target_include_directories(cutenes-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(cutenes-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Dev-only INTERFACE target exposing internal headers (src/ tree) to the
# test suite. Never installed, never linked by consumers.
add_library(cutenes-core-internal INTERFACE)
target_include_directories(cutenes-core-internal INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)

# Determinism safety net (research.md D7): integer-only emulation state is the
# mechanism; these flags keep any stray FP strictly ordered/contraction-free.
if(MSVC)
    target_compile_options(cutenes-core PRIVATE /fp:strict /permissive- /W4 /WX)
else()
    target_compile_options(cutenes-core PRIVATE
        -ffp-contract=off -Wall -Wextra -Wconversion -Wshadow -Werror)
endif()

# Export packaging so the Qt frontend and MCP server can consume this target
# via FetchContent or find_package with the identical name (plan.md D6).
install(TARGETS cutenes-core EXPORT cutenes-core-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cutenes-coreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cutenes-coreConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cutenes-core)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/cutenes-core-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(EXPORT cutenes-core-targets
    NAMESPACE cutenes-core::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cutenes-core)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/cutenes-coreConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cutenes-core-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cutenes-core)
