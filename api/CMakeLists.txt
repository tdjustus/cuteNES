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

add_library(cutenes-api
    src/registry.cpp
    src/schema.cpp
    src/session.cpp
    src/core_commands.cpp
    src/session_commands.cpp
    src/media.cpp
    src/stb_impl.cpp)

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

if(MSVC)
    target_compile_options(cutenes-api PRIVATE /permissive- /W4 /WX)
    set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
    target_compile_options(cutenes-api PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror)
    set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
