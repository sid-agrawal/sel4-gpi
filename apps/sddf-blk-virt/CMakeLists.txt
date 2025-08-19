#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(sddf_blk C CXX)

include(nanopb)

# Use arm arch source files on RISCV
set(arch ${KernelArch})
if(KernelArchRiscV)
    set(arch arm)
endif()

SEL4_GENERATE_PROTOBUF(proto_srcs proto_hdrs ${CMAKE_CURRENT_SOURCE_DIR}/sddf_blk_rpc.proto)

file(GLOB client_deps src/sddf_blk_client/*.c)
list(SORT client_deps)
add_library(sddf_blk_client STATIC EXCLUDE_FROM_ALL ${client_deps} ${proto_srcs})
target_include_directories(sddf_blk_client PUBLIC ${CMAKE_CURRENT_BINARY_DIR} include/sddf_blk_client include/shared)
target_link_libraries(sddf_blk_client muslc sel4 utils sel4test sel4gpi sel4nanopb)

file(GLOB server_deps src/sddf_blk_server/*.c)
list(SORT server_deps)
add_executable(sddf_blk_server EXCLUDE_FROM_ALL ${server_deps} ${proto_srcs})

target_include_directories(sddf_blk_server PRIVATE ${CMAKE_CURRENT_BINARY_DIR} include/shared include/sddf_blk_server)
target_link_libraries(
    sddf_blk_server
    PUBLIC
        muslc
        sel4
        utils
        sel4runtime
        sel4utils
        sel4muslcsys
        sel4gpi
        sel4nanopb
    PRIVATE sel4_autoconf
)