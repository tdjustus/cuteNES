cmake_minimum_required(VERSION 3.21)

project(cutenes VERSION 0.1.0 LANGUAGES CXX)

# Emulation core library (feature 001).
add_subdirectory(core)

# The command API over the core (feature 002): the layer the API server,
# the MCP adapter and the GUI host share.
option(CUTENES_BUILD_API "Build the cutenes-api library" ON)
if(CUTENES_BUILD_API)
    add_subdirectory(api)
    add_subdirectory(mcp)
    add_subdirectory(server)
endif()

# The Qt desktop frontend (feature 003): the cutenes-qt library and the
# cutenes application. AUTO builds it when Qt 6.8 or later is found; CI
# passes ON, so a missing Qt fails the build instead of skipping the GUI.
set(CUTENES_BUILD_GUI AUTO CACHE STRING "Build the Qt frontend: AUTO, ON or OFF")
set_property(CACHE CUTENES_BUILD_GUI PROPERTY STRINGS AUTO ON OFF)
if(NOT CUTENES_BUILD_GUI STREQUAL "OFF")
    if(NOT CUTENES_BUILD_API)
        if(CUTENES_BUILD_GUI STREQUAL "ON")
            message(FATAL_ERROR "CUTENES_BUILD_GUI=ON needs CUTENES_BUILD_API=ON")
        endif()
    elseif(CUTENES_BUILD_GUI STREQUAL "ON")
        find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
    else()
        find_package(Qt6 6.8 QUIET COMPONENTS Widgets)
    endif()
    if(CUTENES_BUILD_API AND Qt6_FOUND)
        add_subdirectory(qt)
        add_subdirectory(app)
    else()
        message(STATUS "cuteNES: Qt 6.8 or later not found; the GUI is not "
            "built (point CMAKE_PREFIX_PATH at a Qt installation)")
    endif()
endif()

# Dev/standalone-only targets. Consumers who add_subdirectory() or
# FetchContent() this repo never download test dependencies.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    option(CUTENES_BUILD_TESTS "Build cutenes-core tests" ON)
    if(CUTENES_BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()
