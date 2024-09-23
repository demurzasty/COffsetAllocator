cmake_minimum_required (VERSION 3.16)

add_executable (Example "Source/Main.c")
target_link_libraries (Example PUBLIC COffsetAllocator)
