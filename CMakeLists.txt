cmake_minimum_required(VERSION 3.10)
project(DOOM VERSION 1.10 LANGUAGES C)

# Set C standard (use 90 for better compatibility)
set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_C_EXTENSIONS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Add subdirectory containing the game
add_subdirectory(linuxdoom-1.10)
