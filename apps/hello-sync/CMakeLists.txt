#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(hello_sync C CXX)

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

add_executable(hello_sync EXCLUDE_FROM_ALL ${deps})


target_link_libraries(
    hello_sync
    PUBLIC
        muslc
        sel4
        sel4runtime
        sel4utils
        sel4muslcsys
        sel4gpi
        sel4sync
    PRIVATE sel4_autoconf
)
