cmake_minimum_required(VERSION 3.21)

project(cutenes VERSION 0.1.0 LANGUAGES CXX)

# Emulation core library — the only buildable target of feature 001.
# Apps (Qt GUI, MCP server) arrive under apps/ in later features.
add_subdirectory(core)

# The command API over the core (feature 002): the layer the API server,
# the MCP adapter and the GUI host share.
option(CUTENES_BUILD_API "Build the cutenes-api library" ON)
if(CUTENES_BUILD_API)
    add_subdirectory(api)
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
