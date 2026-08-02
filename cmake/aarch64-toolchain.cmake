# ==============================================================================
# CMake Toolchain File for GCC ARM64 Cross-Compilation
# Target Architecture: aarch64-linux-gnu
# ==============================================================================

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Specify cross-compiler executables
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR           aarch64-linux-gnu-ar CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB       aarch64-linux-gnu-ranlib CACHE FILEPATH "Ranlib")
set(CMAKE_STRIP        aarch64-linux-gnu-strip CACHE FILEPATH "Strip")

# Target sysroot search path
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)

# Search mode policies
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Enforce C++20 globally across target builds
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
