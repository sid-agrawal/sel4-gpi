#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(hello_cleanup C CXX)

include(nanopb)

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

SEL4_GENERATE_PROTOBUF(proto_srcs proto_hdrs ${CMAKE_CURRENT_SOURCE_DIR}/basic_rpc.proto)

add_executable(hello_cleanup EXCLUDE_FROM_ALL ${deps} ${proto_srcs})

target_include_directories(
    hello_cleanup
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        include 
)

# (XXX) Arya: probably not all of these are needed
target_link_libraries(
    hello_cleanup
    PUBLIC
        sel4_autoconf
        muslc
        sel4bench
        sel4
        sel4runtime
        sel4allocman
        sel4vka
        sel4utils
        sel4muslcsys
        sel4gpi
        sel4nanopb
    PRIVATE sel4test-driver_Config
)
