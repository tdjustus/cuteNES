# cutenes-api: the command registry, session and command set that the API
# server and the MCP adapter both serve (specs/002-ai-mcp-server, plan.md).
# Unlike the core it may use third-party libraries and do I/O.

include(FetchContent)

FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# cpp-httplib v0.61.0 (2026-10-14), header-only. SOURCE_SUBDIR points at a
# directory that does not exist, so its CMakeLists (which would look for
# OpenSSL) never runs: only the header is used.
FetchContent_Declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.61.0.tar.gz
    URL_HASH SHA256=f03bdb0bee4911d6db0335bc103f0f441ae70c527e993c1f4b3f177586b042d3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR header-only)
FetchContent_MakeAvailable(httplib)
add_library(cutenes-httplib INTERFACE)
target_include_directories(cutenes-httplib SYSTEM INTERFACE ${httplib_SOURCE_DIR})
if(WIN32)
    target_link_libraries(cutenes-httplib INTERFACE ws2_32)
endif()
# MinGW debug objects that include httplib and nlohmann/json exceed COFF's
# 32767-section limit; the big-object format lifts it.
if(MINGW)
    target_compile_options(cutenes-httplib INTERFACE -Wa,-mbig-obj)
endif()

add_library(cutenes-api
    src/registry.cpp
    src/schema.cpp
    src/session.cpp
    src/core_commands.cpp
    src/session_commands.cpp
    src/media.cpp
    src/stb_impl.cpp
    src/token.cpp
    src/jsonrpc.cpp
    src/http_server.cpp)

add_library(cutenes-api::cutenes-api ALIAS cutenes-api)

target_compile_features(cutenes-api PUBLIC cxx_std_20)
target_include_directories(cutenes-api PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_include_directories(cutenes-api SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/stb)

# nlohmann/json is part of the public headers; consumers see it as a
# system include so its code never trips their warning flags.
get_target_property(_json_includes nlohmann_json::nlohmann_json
    INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(cutenes-api SYSTEM PUBLIC ${_json_includes})

find_package(Threads REQUIRED)
target_link_libraries(cutenes-api PUBLIC cutenes-core::cutenes-core Threads::Threads)
target_link_libraries(cutenes-api PRIVATE cutenes-httplib)
if(MINGW)
    # The big-object format for every target that includes nlohmann/json.
    target_compile_options(cutenes-api PUBLIC -Wa,-mbig-obj)
endif()
if(WIN32)
    # BCryptGenRandom and the token file's owner-only DACL.
    target_link_libraries(cutenes-api PRIVATE bcrypt advapi32)
endif()

if(MSVC)
    target_compile_options(cutenes-api PRIVATE /permissive- /W4 /WX)
    set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
    target_compile_options(cutenes-api PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror)
    set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
    # GCC 13 reports a false -Wmaybe-uninitialized inside libstdc++'s
    # <regex> (std::function in _StateT, GCC bug 105562).
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(cutenes-api PRIVATE -Wno-maybe-uninitialized)
    endif()
endif()
