# Cross-compiling for 64-bit Windows with MinGW-w64 (tools/build_windows.sh).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TSW_MINGW x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${TSW_MINGW}-gcc)
set(CMAKE_RC_COMPILER ${TSW_MINGW}-windres)

# Libraries and packages only from the roots, in order: the ones given
# with -DCMAKE_FIND_ROOT_PATH (the SDL archives, build_windows.sh) before
# the MinGW system folder, so the package gets the archives' DLLs.
list(APPEND CMAKE_FIND_ROOT_PATH /usr/${TSW_MINGW})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ctest runs the tests under wine when it is installed.
find_program(TSW_WINE wine)
if(TSW_WINE)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${TSW_WINE})
endif()
