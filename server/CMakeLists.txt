# cutenes-server: the headless executable (feature 002, transports.md).
# The run loop is a small library so the tests can drive it in process.

add_library(cutenes-server-lib STATIC server.cpp)
target_include_directories(cutenes-server-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cutenes-server-lib PUBLIC cutenes-api cutenes-mcp)

add_executable(cutenes-server main.cpp)
target_link_libraries(cutenes-server PRIVATE cutenes-server-lib)

if(MSVC)
    target_compile_options(cutenes-server-lib PRIVATE /permissive- /W4 /WX)
    target_compile_options(cutenes-server PRIVATE /permissive- /W4 /WX)
else()
    target_compile_options(cutenes-server-lib PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror)
    target_compile_options(cutenes-server PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror)
endif()
