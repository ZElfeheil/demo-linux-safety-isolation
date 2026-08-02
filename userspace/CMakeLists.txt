# SPDX-License-Identifier: MIT
# CMakeLists.txt — ARM64 Linux 6.6 Safety Isolation Demonstration System (Userspace)

cmake_minimum_required(VERSION 3.22)
project(safety_demo_userspace CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link binaries statically for initramfs standalone execution
set(CMAKE_EXE_LINKER_FLAGS "-static")

# Global Compiler Warning Flags
add_compile_options(-Wall -Wextra -Werror -Wpedantic)

# Binaries destination directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Common headers library target
add_library(common INTERFACE)
target_include_directories(common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Executable: devmem
add_executable(devmem
    devmem/main.cpp
    devmem/phys_view.cpp
)
target_include_directories(devmem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(devmem PRIVATE common)

# Executable: analysis
add_executable(analysis
    analysis/main.cpp
)
target_include_directories(analysis PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(analysis PRIVATE common)

# Executable: monitor
add_executable(monitor
    monitor/main.cpp
    monitor/renderer.cpp
)
target_include_directories(monitor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(monitor PRIVATE common)

# Executable: harness
add_executable(harness
    harness/main.cpp
    harness/interactive.cpp
    harness/module_loader.cpp
    harness/scenarios/scenario_b.cpp
    harness/scenarios/scenario_d.cpp
    harness/scenarios/scenario_f.cpp
    harness/scenarios/scenario_g.cpp
)
target_include_directories(harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(harness PRIVATE common)
