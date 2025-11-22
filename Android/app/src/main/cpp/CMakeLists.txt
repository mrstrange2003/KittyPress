cmake_minimum_required(VERSION 3.18)
project("kittypress")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-O2")

# Collect all .cpp files
file(GLOB SRC_FILES "${CMAKE_SOURCE_DIR}/*.cpp")

# Create shared library
add_library(kittypress SHARED ${SRC_FILES})

# Include headers
target_include_directories(kittypress PRIVATE ${CMAKE_SOURCE_DIR})

# Android log library
find_library(log-lib log)

# --- FIXED: Find proper zlib library ---
find_library(ZLIB_LIB zlib)

if (NOT ZLIB_LIB)
    message(FATAL_ERROR "Zlib not found in NDK!")
endif()

# Link everything
target_link_libraries(
        kittypress
        PRIVATE
        ${log-lib}
        ${ZLIB_LIB}
)
