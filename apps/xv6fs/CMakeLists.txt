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

SEL4_GENERATE_PROTOBUF(proto_srcs proto_hdrs ${CMAKE_CURRENT_SOURCE_DIR}/fs_rpc.proto)

file(GLOB client_deps src/fs_client/*.c)
list(SORT client_deps)
add_library(fs_client STATIC EXCLUDE_FROM_ALL ${client_deps} ${proto_srcs})
target_include_directories(fs_client PUBLIC ${CMAKE_CURRENT_BINARY_DIR} include/fs_client include/shared)
target_link_libraries(fs_client muslc sel4muslcsys sel4 utils sel4test sel4gpi ramdisk_client sel4nanopb)

file(GLOB server_deps src/fs_server/*.c)
list(SORT server_deps)
add_executable(fs_server EXCLUDE_FROM_ALL ${server_deps} ${proto_srcs})
set_target_properties(
    fs_server
    PROPERTIES
    COMPILE_DEFINITIONS FS_EXECUTABLE=1)
target_include_directories(fs_server PRIVATE ${CMAKE_CURRENT_BINARY_DIR} include/shared include/fs_server)
target_link_libraries(
    fs_server
    PUBLIC
        sel4_autoconf
        muslc
        sel4
        utils
        sel4runtime
        sel4utils
        sel4sync
        sel4muslcsys
        sel4gpi
        ramdisk_client
        sel4nanopb
    PRIVATE sel4_autoconf
)