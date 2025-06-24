# CMake toolchain file for MinGW-w64 cross-compilation
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)

# Specify the cross compiler
set(CMAKE_C_COMPILER i686-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER i686-w64-mingw32-g++)

# Where to find libraries and headers
set(CMAKE_FIND_ROOT_PATH /usr/i686-w64-mingw32)

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Search for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Set flags for 32-bit compilation
set(CMAKE_C_FLAGS "-m32")
set(CMAKE_CXX_FLAGS "-m32")
set(CMAKE_EXE_LINKER_FLAGS "-m32 -static")
set(CMAKE_SHARED_LINKER_FLAGS "-m32 -static")

# Explicitly set the generator and make program to ensure CMake can build
set(CMAKE_GENERATOR "Unix Makefiles" CACHE INTERNAL "")
set(CMAKE_MAKE_PROGRAM "/usr/bin/make" CACHE FILEPATH "") 