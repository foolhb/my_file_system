cmake_minimum_required(VERSION 3.12)
project(assignment3 C)

set(CMAKE_C_STANDARD 11)

include_directories(src)

add_executable(assignment3
        src/block.c
        src/block.h
        src/config.h
        src/fuse.h
        src/log.c
        src/log.h
        src/params.h
        src/sfs.c
        src/sfs.h
        src/sfs_helper_functions.c
        src/sfs_helper_functions.h)
