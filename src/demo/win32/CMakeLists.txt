# ScrApiDemo: a reference SCRAPI implementation independent of ScrKit.scr (docs/SCRAPI_V2_DRAFT.md).
# Windows-only, like ScrKit.scr and ScrViewer; only configured from the top-level CMakeLists.txt
# when WIN32 is true.

add_executable(ScrApiDemo WIN32
    DemoMain.cpp
    DemoHost.cpp
    DemoRenderer.cpp
    # Shared with ScrKit.scr (both need the same named-pipe transport and wide-path file I/O --
    # this is exactly the saver-agnostic reuse SCRAPI's design is meant to enable).
    ${CMAKE_SOURCE_DIR}/src/platform/win32/ScrApiPipe.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/win32/WinFileIO.cpp
)

target_include_directories(ScrApiDemo PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ScrApiDemo PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(ScrApiDemo PRIVATE spiral_core user32 gdi32 shell32)

if(MINGW)
    target_link_options(ScrApiDemo PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

set_target_properties(ScrApiDemo PROPERTIES
    OUTPUT_NAME "ScrApiDemo"
    SUFFIX ".scr"
)
