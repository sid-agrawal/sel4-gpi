#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(sample-resource-server C CXX)

include(nanopb)

# Use arm arch source files on RISCV
set(arch ${KernelArch})
if(KernelArchRiscV)
    set(arch arm)
endif()

SEL4_GENERATE_PROTOBUF(proto_srcs proto_hdrs ${CMAKE_CURRENT_SOURCE_DIR}/sample_rpc.proto)

# Create the client library
file(GLOB client_deps src/sample_client/*.c)
list(SORT client_deps)
add_library(sample_client STATIC EXCLUDE_FROM_ALL ${client_deps} ${proto_srcs})
target_include_directories(sample_client PUBLIC ${CMAKE_CURRENT_BINARY_DIR} include/sample_client include/shared)
target_link_libraries(sample_client muslc sel4 utils sel4test sel4gpi sel4nanopb)

# Create the server executable
file(GLOB server_deps src/sample_server/*.c)
list(SORT server_deps)
add_executable(sample_server EXCLUDE_FROM_ALL ${server_deps} ${proto_srcs})
target_include_directories(sample_server PRIVATE ${CMAKE_CURRENT_BINARY_DIR} include/shared include/sample_server)
target_link_libraries(
    sample_server
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