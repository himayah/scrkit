# Windows-only target: the actual .scr screensaver. Only ever configured
# when WIN32 is true (see the guard in the top-level CMakeLists.txt), so
# this file assumes an MSVC or MinGW-w64 Windows toolchain is present.

enable_language(RC)

set(WIN32_SOURCES
    AppController.cpp
    AppPaths.cpp
    ConfigDialogWin32.cpp
    FileLogSink.cpp
    ImageLoader.cpp
    OpenGLContext.cpp
    Renderer.cpp
    SaverWindow.cpp
    TextRenderer.cpp
    WallpaperProvider.cpp
    WinFileIO.cpp
    WinMain.cpp
    ${CMAKE_SOURCE_DIR}/resources/ScreenSaver.rc
)

add_executable(SpiralSuctionSaver WIN32 ${WIN32_SOURCES})

target_include_directories(SpiralSuctionSaver PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/resources
)

# The code consistently calls the *W (wide-char) Win32 APIs directly, but
# also uses charset-generic macros (LoadCursor, MAKEINTRESOURCE, ...) that
# only expand to their W form when UNICODE/_UNICODE are defined -- without
# this they'd silently resolve to the A (ANSI) form and fail to compile
# against wide-char arguments.
target_compile_definitions(SpiralSuctionSaver PRIVATE UNICODE _UNICODE)

if(MINGW)
    # windres's own built-in preprocessor does not predefine _WIN32/_WIN64,
    # which <windows.h>/_mingw.h require ("#error Only Win32 target is
    # supported!") -- and even with that fixed it still doesn't know the
    # toolchain's default header search path. Using the matching g++ as the
    # preprocessor instead sidesteps both problems in one go.
    set(CMAKE_RC_FLAGS
        "${CMAKE_RC_FLAGS} --preprocessor=${CMAKE_CXX_COMPILER} --preprocessor-arg=-E --preprocessor-arg=-xc-header --preprocessor-arg=-DRC_INVOKED")
endif()

target_link_libraries(SpiralSuctionSaver PRIVATE
    spiral_core
    opengl32
    gdi32
    user32
    shell32
    comdlg32
    ole32
    windowscodecs
)

# A screensaver is an ordinary Win32 GUI executable, just renamed with a
# .scr extension -- Windows recognizes and launches it identically to a
# .exe (要件.txt §1: 出力は単一の .scr ファイルとしてビルド可能にする).
set_target_properties(SpiralSuctionSaver PROPERTIES
    OUTPUT_NAME "SpiralSuctionSaver"
    SUFFIX ".scr"
)
