cmake_minimum_required(VERSION 3.15)
project(SpiralSuctionSaver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# When ON (default off), configure only the platform-independent core library
# and its unit tests. This is what this project's Linux dev/CI environment
# builds and runs via ctest, since the Win32/OpenGL target below only makes
# sense on Windows.
option(BUILD_CORE_TESTS_ONLY "Build only src/core and its unit tests (no Win32 target)" OFF)

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/core/*.cpp
    ${CMAKE_SOURCE_DIR}/src/core/effects/*.cpp
    ${CMAKE_SOURCE_DIR}/src/core/effects/fg/*.cpp
    ${CMAKE_SOURCE_DIR}/src/core/effects/bg/*.cpp)
add_library(spiral_core STATIC ${CORE_SOURCES})
target_include_directories(spiral_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

# SCRAPI (docs/SCRAPI_SPEC.md): the saver-agnostic control-channel library, shared by
# the saver (server side) and the viewer (client side). Platform-independent like core.
file(GLOB SCRAPI_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/scrapi/*.cpp)
add_library(scrapi STATIC ${SCRAPI_SOURCES})
target_include_directories(scrapi PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(spiral_core PUBLIC scrapi)

# The viewer's platform-independent UI logic (docs/DESIGN_VIEWER_UI.md §5): layout, display
# list and input handling of the custom-drawn controller panel. Unit-tested on Linux.
file(GLOB VIEWER_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/viewer/core/*.cpp)
add_library(viewer_core STATIC ${VIEWER_CORE_SOURCES})
target_include_directories(viewer_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(viewer_core PUBLIC scrapi)

enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
add_executable(core_tests ${TEST_SOURCES})
target_link_libraries(core_tests PRIVATE spiral_core viewer_core)
target_include_directories(core_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
add_test(NAME core_tests COMMAND core_tests)

if(NOT BUILD_CORE_TESTS_ONLY AND WIN32)
    add_subdirectory(src/platform/win32)
    add_subdirectory(src/viewer/win32)
endif()
