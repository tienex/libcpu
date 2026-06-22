# MinGW-w64 cross toolchain for building libnix (and its host-op conformance test) for win32,
# to validate the win32 host backend under Wine without a Windows machine. Usage:
#
#   cmake -DCMAKE_TOOLCHAIN_FILE=CMake/mingw-w64.cmake <libnix-src-dir> && cmake --build .
#
# then run nixtest.exe under wine (see run-libnix-wine.sh). Requires x86_64-w64-mingw32-gcc on PATH.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Static-link the MinGW runtime (libgcc / libwinpthread / CRT) into the test executable so it is
# self-contained under Wine -- no runtime-DLL hunting as the nix surface pulls in more of them.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
