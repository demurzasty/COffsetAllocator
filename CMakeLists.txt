cmake_minimum_required (VERSION 3.16)

project (COffsetAllocator)

add_library (COffsetAllocator STATIC "Source/COffsetAllocator.c")
target_include_directories (COffsetAllocator PUBLIC "Include")

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory ("Example")
endif ()
