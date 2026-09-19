# ScrViewer: the SCRAPI viewer (docs/DESIGN_VIEWER.md §C). Windows-only, like the .scr;
# only configured from the top-level CMakeLists.txt when WIN32 is true.

add_executable(ScrViewer WIN32
    ViewerMain.cpp
    ControlPanel.cpp
    ScrLauncher.cpp
    # The pipe transport is shared with the .scr (server mode is used here).
    ${CMAKE_SOURCE_DIR}/src/platform/win32/ScrApiPipe.cpp
)

target_include_directories(ScrViewer PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ScrViewer PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(ScrViewer PRIVATE scrapi user32 gdi32 comctl32 comdlg32 shell32 ole32)

if(MINGW)
    target_link_options(ScrViewer PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
