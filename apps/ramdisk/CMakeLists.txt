#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(ramdisk C CXX)

include(nanopb)

# Use arm arch source files on RISCV
set(arch ${KernelArch})
if(KernelArchRiscV)
    set(arch arm)
endif()

SEL4_GENERATE_PROTOBUF(proto_srcs proto_hdrs ${CMAKE_CURRENT_SOURCE_DIR}/ramdisk_rpc.proto)

file(GLOB client_deps src/ramdisk_client/*.c)
list(SORT client_deps)
add_library(ramdisk_client STATIC EXCLUDE_FROM_ALL ${client_deps} ${proto_srcs})
target_include_directories(ramdisk_client PUBLIC ${CMAKE_CURRENT_BINARY_DIR} include/ramdisk_client include/shared)
target_link_libraries(ramdisk_client muslc sel4 utils sel4test sel4gpi sel4nanopb)

file(GLOB server_deps src/ramdisk_server/*.c)
list(SORT server_deps)
add_executable(ramdisk_server EXCLUDE_FROM_ALL ${server_deps} ${proto_srcs})
set_target_properties(
    ramdisk_server
    PROPERTIES
    COMPILE_DEFINITIONS RAMDISK_EXECUTABLE=1)
#add_executable(ramdisk_server EXCLUDE_FROM_ALL src/ramdisk_server/main.c src/ramdisk_server/ramdisk_server.c)
target_include_directories(ramdisk_server PRIVATE ${CMAKE_CURRENT_BINARY_DIR} include/shared include/ramdisk_server)
target_link_libraries(
    ramdisk_server
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