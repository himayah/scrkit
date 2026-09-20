# Windows-only target: the actual .scr screensaver. Only ever configured
# when WIN32 is true (see the guard in the top-level CMakeLists.txt), so
# this file assumes an MSVC or MinGW-w64 Windows toolchain is present.

enable_language(RC)

set(WIN32_SOURCES
    AppController.cpp
    AppPaths.cpp
    ConfigDialogWin32.cpp
    FileLogSink.cpp
    HueRingBuilder.cpp
    ImageLoader.cpp
    OpenGLContext.cpp
    Renderer.cpp
    SaverWindow.cpp
    ScrApiHost.cpp
    ScrApiPipe.cpp
    ScreenCapture.cpp
    WallpaperProvider.cpp
    WinFileIO.cpp
    WinMain.cpp
    WindowRects.cpp
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
#
# NOMINMAX must be defined here (as a compiler flag, not just inside a
# header) so it takes effect before the *first* <windows.h> inclusion in
# every translation unit, however that file happens to reach it -- a header
# guard means windows.h's own min/max macros, once defined by an earlier
# unguarded include, cannot be "undone" by a later #define NOMINMAX. Without
# this, `std::max`/`std::min` calls anywhere in the code get silently
# mangled by those macros (MSVC reports it as a confusing
# "illegal token on right side of '::'" syntax error).
target_compile_definitions(SpiralSuctionSaver PRIVATE UNICODE _UNICODE NOMINMAX)

if(MINGW)
    # windres's own built-in preprocessor does not predefine _WIN32/_WIN64,
    # which <windows.h>/_mingw.h require ("#error Only Win32 target is
    # supported!") -- and even with that fixed it still doesn't know the
    # toolchain's default header search path. Using the matching g++ as the
    # preprocessor instead sidesteps both problems in one go.
    set(CMAKE_RC_FLAGS
        "${CMAKE_RC_FLAGS} --preprocessor=${CMAKE_CXX_COMPILER} --preprocessor-arg=-E --preprocessor-arg=-xc-header --preprocessor-arg=-DRC_INVOKED")

    # By default g++ links libstdc++/libgcc/libwinpthread dynamically, so the
    # resulting .scr depends on libstdc++-6.dll / libgcc_s_seh-1.dll /
    # libwinpthread-1.dll being present on the target machine -- they
    # normally live next to the compiler, not on a plain end-user Windows
    # install, which is exactly what produces
    # "libstdc++-6.dll が見つからないため、コードの実行を続行できません" at
    # runtime. Statically linking them in keeps the .scr a single
    # self-contained file (要件.txt §1: 単一の .scr ファイルとしてビルド可能に
    # する), which matters especially here since a screensaver's /c and /p
    # invocations are launched by explorer.exe/the Settings app, not by the
    # user directly, so a missing-DLL failure there can show no visible error
    # at all instead of the loader dialog seen when double-clicking the file.
    target_link_options(SpiralSuctionSaver PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

target_link_libraries(SpiralSuctionSaver PRIVATE
    spiral_core
    opengl32
    gdi32
    user32
    shell32
    comdlg32
    comctl32
    ole32
    windowscodecs
    advapi32
    dwmapi
)

# A screensaver is an ordinary Win32 GUI executable, just renamed with a
# .scr extension -- Windows recognizes and launches it identically to a
# .exe (要件.txt §1: 出力は単一の .scr ファイルとしてビルド可能にする).
set_target_properties(SpiralSuctionSaver PROPERTIES
    OUTPUT_NAME "SpiralSuctionSaver"
    SUFFIX ".scr"
)
