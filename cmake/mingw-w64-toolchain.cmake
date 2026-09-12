# Optional CMake toolchain file for cross-compiling the .scr with
# MinGW-w64 from Linux/WSL2 (要件.txt §1: フリーのコンパイラでビルド可能にする).
#
# Usage:
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
#   cmake --build build-win
#
# Assumes the standard Ubuntu/Debian package names below are on PATH
# (`sudo apt install g++-mingw-w64-x86-64`) or MINGW_PREFIX_BIN is set to
# wherever the x86_64-w64-mingw32-* binaries live (e.g. a MinGW-w64 toolchain
# extracted without installing it system-wide).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_mingw_triplet x86_64-w64-mingw32)

if(NOT DEFINED MINGW_PREFIX_BIN)
    set(MINGW_PREFIX_BIN "")
endif()

find_program(CMAKE_C_COMPILER NAMES ${_mingw_triplet}-gcc PATHS ${MINGW_PREFIX_BIN})
find_program(CMAKE_CXX_COMPILER NAMES ${_mingw_triplet}-g++ PATHS ${MINGW_PREFIX_BIN})
find_program(CMAKE_RC_COMPILER NAMES ${_mingw_triplet}-windres PATHS ${MINGW_PREFIX_BIN})
find_program(CMAKE_AR NAMES ${_mingw_triplet}-ar PATHS ${MINGW_PREFIX_BIN})
find_program(CMAKE_RANLIB NAMES ${_mingw_triplet}-ranlib PATHS ${MINGW_PREFIX_BIN})

set(CMAKE_FIND_ROOT_PATH /usr/${_mingw_triplet})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
