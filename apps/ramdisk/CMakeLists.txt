#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(ramdisk C CXX)

# Use arm arch source files on RISCV
set(arch ${KernelArch})
if(KernelArchRiscV)
    set(arch arm)
endif()

file(GLOB client_deps src/ramdisk_client/*.c)
list(SORT client_deps)
add_library(ramdisk_client STATIC EXCLUDE_FROM_ALL ${client_deps})
target_include_directories(ramdisk_client PUBLIC include/ramdisk_client include/shared)
target_link_libraries(ramdisk_client muslc sel4 utils sel4test sel4gpi)

file(GLOB server_lib_deps src/ramdisk_server/*.c)
list(SORT server_lib_deps)
add_library(ramdisk_server_lib STATIC EXCLUDE_FROM_ALL ${server_lib_deps})
target_include_directories(ramdisk_server_lib PUBLIC include/shared include/ramdisk_server)
target_link_libraries(
    ramdisk_server_lib
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
    PRIVATE sel4test-driver_Config
)

file(GLOB server_deps src/ramdisk_server/*.c)
list(SORT server_deps)
add_executable(ramdisk_server EXCLUDE_FROM_ALL ${server_deps})
set_target_properties(
    ramdisk_server
    PROPERTIES
    COMPILE_DEFINITIONS RAMDISK_EXECUTABLE=1)
#add_executable(ramdisk_server EXCLUDE_FROM_ALL src/ramdisk_server/main.c src/ramdisk_server/ramdisk_server.c)
target_include_directories(ramdisk_server PRIVATE include/shared include/ramdisk_server)
target_link_libraries(
    ramdisk_server
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
    PRIVATE sel4test-driver_Config
)