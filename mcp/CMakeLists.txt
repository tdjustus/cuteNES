# cutenes-mcp: the MCP adapter over the cutenes-api registry (feature 002,
# research R2): MCP 2026-07-28 with 2025-11-25 compatibility, over stdio and
# Streamable HTTP.

add_library(cutenes-mcp
    src/server.cpp
    src/stdio.cpp
    src/http.cpp
    src/bridge.cpp)

add_library(cutenes-mcp::cutenes-mcp ALIAS cutenes-mcp)

target_compile_features(cutenes-mcp PUBLIC cxx_std_20)
target_include_directories(cutenes-mcp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(cutenes-mcp PUBLIC cutenes-api)
target_link_libraries(cutenes-mcp PRIVATE cutenes-httplib)

if(MSVC)
    target_compile_options(cutenes-mcp PRIVATE /permissive- /W4 /WX)
else()
    target_compile_options(cutenes-mcp PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror)
    # GCC 13 reports a false -Wmaybe-uninitialized inside libstdc++'s
    # <regex>, which httplib's client uses (GCC bug 105562; cutenes-api
    # has the same exception).
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(cutenes-mcp PRIVATE -Wno-maybe-uninitialized)
    endif()
endif()
