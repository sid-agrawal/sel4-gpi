#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(fs C CXX)

# Use arm arch source files on RISCV
set(arch ${KernelArch})
if(KernelArchRiscV)
    set(arch arm)
endif()

file(GLOB client_deps src/fs_client/*.c)
list(SORT client_deps)
add_library(fs_client STATIC EXCLUDE_FROM_ALL ${client_deps})
target_include_directories(fs_client PUBLIC include/fs_client include/shared)
target_link_libraries(fs_client muslc sel4 utils sel4test sel4gpi sel4bench ramdisk_client)

file(GLOB server_deps src/fs_server/*.c)
list(SORT server_deps)
add_executable(fs_server EXCLUDE_FROM_ALL ${server_deps})
set_target_properties(
    fs_server
    PROPERTIES
    COMPILE_DEFINITIONS FS_EXECUTABLE=1)
target_include_directories(fs_server PRIVATE include/shared include/fs_server)
target_link_libraries(
    fs_server
    PUBLIC
        sel4_autoconf
        muslc
        sel4bench
        sel4
        utils
        sel4runtime
        sel4allocman
        sel4vka
        sel4utils
        sel4rpc
        sel4test
        sel4sync
        sel4muslcsys
        sel4testsupport
        sel4gpi
        sel4serialserver_tests
        ramdisk_client
    PRIVATE sel4test-driver_Config
)