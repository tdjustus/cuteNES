# cutenes-qt: the embeddable Qt layer (specs/003-qt-frontend, plan.md;
# specs/001-emulation-core/ui-embeddable-design.md). It holds the emulation
# view, audio output, frame clock and input, and uses only the public core
# and API headers.

include(FetchContent)

# SDL 3.4.18 (2026-10-02, zlib) for audio output and gamepads (research R4,
# R5). It is built static with only the subsystems those need.
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
foreach(subsystem VIDEO GPU RENDER CAMERA DIALOG TRAY)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
# Without video SDL needs no X11 or Wayland headers. Its configure still
# insists on them unless told that this is a console build.
set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

add_library(cutenes-qt STATIC
    src/media_runtime.cpp)
add_library(cutenes-qt::cutenes-qt ALIAS cutenes-qt)
target_include_directories(cutenes-qt PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(cutenes-qt
    PUBLIC cutenes-api Qt6::Widgets
    PRIVATE SDL3::SDL3-static)
set_target_properties(cutenes-qt PROPERTIES AUTOMOC ON)

if(MSVC)
    target_compile_options(cutenes-qt PRIVATE /permissive- /W4 /WX)
else()
    target_compile_options(cutenes-qt PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror)
endif()
