#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(kvstore C CXX)

# Use arm arch source files on RISCV
set(arch ${KernelArch})
if(KernelArchRiscV)
    set(arch arm)
endif()

file(GLOB client_deps src/client/*.c src/server/kvstore_server.c)
list(SORT client_deps)
add_library(kvstore_client STATIC EXCLUDE_FROM_ALL ${client_deps})
target_include_directories(kvstore_client PUBLIC include/client include/shared PRIVATE include/server)
target_link_libraries(kvstore_client muslc sel4 utils sel4test sel4gpi sqlite3 fs_client)

file(GLOB server_deps src/server/*.c)
list(SORT server_deps)
add_executable(kvstore_server EXCLUDE_FROM_ALL ${server_deps})

target_include_directories(kvstore_server PRIVATE include/shared include/server)
target_link_libraries(
    kvstore_server
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
        sqlite3
        fs_client
    PRIVATE sel4test-driver_Config
)