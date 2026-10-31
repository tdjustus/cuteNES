# cutenes-mcp tests (specs/002-ai-mcp-server/tasks.md P3), label "mcp".
# Assertions come from the MCP specification and its pinned schema.json
# (third_party/mcp-schema), the transports contract, and test ROMs' own
# verdicts.

add_executable(cutenes-mcp-tests
    test_discovery.cpp
    test_schema.cpp
    test_playloop.cpp
    test_parity.cpp
    test_disconnect.cpp
    test_streamable_http.cpp)

target_link_libraries(cutenes-mcp-tests PRIVATE
    cutenes-mcp cutenes-server-lib cutenes-httplib gtest_main)
# The api tests' helpers (support.hpp, blargg.hpp) and the media helpers
# they include.
target_include_directories(cutenes-mcp-tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/api ${PROJECT_SOURCE_DIR}/api/src)
target_compile_definitions(cutenes-mcp-tests PRIVATE
    CUTENES_TEST_ROMS="${PROJECT_SOURCE_DIR}/tests/roms"
    CUTENES_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    CUTENES_SERVER_EXE="$<TARGET_FILE:cutenes-server>")
# The stdio tests launch the real executable.
add_dependencies(cutenes-mcp-tests cutenes-server)

gtest_discover_tests(cutenes-mcp-tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "mcp")
