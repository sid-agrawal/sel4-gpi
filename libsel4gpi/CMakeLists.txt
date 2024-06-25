#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(libsel4gpi C)

include(nanopb)

SEL4_GENERATE_PROTOBUF(proto_srcs proto_hdrs ${CMAKE_CURRENT_SOURCE_DIR}/pd_rpc.proto)

file(GLOB deps src/*.c)

list(SORT deps)

add_library(sel4gpi STATIC EXCLUDE_FROM_ALL ${deps} ${proto_srcs})
target_include_directories(sel4gpi PUBLIC include ${CMAKE_CURRENT_BINARY_DIR} )
target_link_libraries(sel4gpi 
    PUBLIC
        muslc
        sel4
        utils
        sel4test
        sel4serialserver
        sel4allocman
        sel4bench
        sel4nanopb
)
