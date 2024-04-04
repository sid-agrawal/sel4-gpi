#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(hello_benchmark C CXX)

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

add_executable(hello_benchmark EXCLUDE_FROM_ALL ${deps})
target_include_directories(
    hello_benchmark
    PRIVATE include 
)

target_link_libraries(
    hello_benchmark
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
    PRIVATE sel4test-driver_Config
)
