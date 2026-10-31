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
    # QRhi (the view's renderer) is semi-public API in GuiPrivate; Qt keeps
    # it compatible within a minor release (research R2).
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    set(qt_components Widgets Concurrent ShaderTools GuiPrivate)
    if(NOT CUTENES_BUILD_API)
        if(CUTENES_BUILD_GUI STREQUAL "ON")
            message(FATAL_ERROR "CUTENES_BUILD_GUI=ON needs CUTENES_BUILD_API=ON")
        endif()
    elseif(CUTENES_BUILD_GUI STREQUAL "ON")
        find_package(Qt6 6.8 REQUIRED COMPONENTS ${qt_components})
    else()
        find_package(Qt6 6.8 QUIET COMPONENTS ${qt_components})
    endif()
    if(CUTENES_BUILD_API AND Qt6_FOUND)
        # Qt before 6.9 links Apple's AGL framework through WrapOpenGL, and
        # current macOS SDKs no longer ship it ("ld: framework 'AGL' not
        # found"). Nothing here uses AGL, so drop it from the link.
        if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
            get_target_property(gl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
            if(gl_libs)
                list(FILTER gl_libs EXCLUDE REGEX "AGL")
                set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES
                    INTERFACE_LINK_LIBRARIES "${gl_libs}")
            endif()
        endif()
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
