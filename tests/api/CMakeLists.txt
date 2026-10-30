# cutenes-api tests (specs/002-ai-mcp-server/tasks.md P1). Assertions come
# from the contracts and from test ROMs' documented verdicts.

add_executable(cutenes-api-tests
    test_registry.cpp
    test_schema.cpp
    test_media.cpp
    test_core_commands.cpp
    test_branching.cpp
    test_memory.cpp
    test_errors.cpp
    test_parity.cpp
    test_concurrency.cpp
    test_determinism.cpp
    test_host.cpp
    test_catalog_golden.cpp)

target_link_libraries(cutenes-api-tests PRIVATE cutenes-api gtest_main)
# Internal headers (media helpers) for the encoding tests.
target_include_directories(cutenes-api-tests PRIVATE ${PROJECT_SOURCE_DIR}/api/src)
target_compile_definitions(cutenes-api-tests PRIVATE
    CUTENES_TEST_ROMS="${PROJECT_SOURCE_DIR}/tests/roms"
    CUTENES_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

gtest_discover_tests(cutenes-api-tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "api")
