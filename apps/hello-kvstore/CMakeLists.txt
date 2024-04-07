#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(hello_kvstore C CXX)

# Use arm arch source files on RISCV
set(arch ${KernelArch})
if(KernelArchRiscV)
    set(arch arm)
endif()

file(
    GLOB
        deps
        src/*.c
)

add_executable(hello_kvstore EXCLUDE_FROM_ALL ${deps})
target_include_directories(
    hello_kvstore
    PRIVATE include 
)

# Add custom linker script
set(linkerScript "${CMAKE_CURRENT_LIST_DIR}/linker.lds")

set_target_properties(
    hello_kvstore
   PROPERTIES LINK_DEPENDS ${linkerScript}
)
add_dependencies(elfloader elfloader_linker)
set_property(
   TARGET hello_kvstore
   APPEND_STRING
   PROPERTY
       LINK_FLAGS
       " -Wl,-T ${linkerScript} -static -Wl,--build-id=none"
)

target_link_libraries(
    hello_kvstore
    PUBLIC
        sel4_autoconf
        muslc
        sel4bench
        sel4
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
        kvstore_client
    PRIVATE sel4test-driver_Config
)
