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

SEL4_GENERATE_PROTOBUF(proto_srcs proto_hdrs 
    ${CMAKE_CURRENT_SOURCE_DIR}/kvstore_server_rpc.proto
)

file(GLOB server_lib_deps src/server/kvstore_server.c)
list(SORT server_lib_deps)
add_library(kvstore_server_lib STATIC EXCLUDE_FROM_ALL ${server_lib_deps} ${proto_srcs})
target_include_directories(kvstore_server_lib 
    PUBLIC include/server include/shared 
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(kvstore_server_lib muslc sel4 utils sel4gpi sqlite3 fs_client sel4nanopb)

file(GLOB client_lib_deps src/client/*.c)
list(SORT client_lib_deps)
add_library(kvstore_client STATIC EXCLUDE_FROM_ALL ${client_lib_deps} ${proto_srcs})
target_include_directories(kvstore_client 
    PUBLIC include/client include/shared ${CMAKE_CURRENT_BINARY_DIR}
    PRIVATE include/server)
target_link_libraries(kvstore_client muslc sel4 utils sel4gpi sqlite3 fs_client kvstore_server_lib sel4nanopb)

file(GLOB server_deps src/server/*.c)
list(SORT server_deps)
add_executable(kvstore_server EXCLUDE_FROM_ALL ${server_deps} ${proto_srcs})

target_include_directories(kvstore_server 
    PRIVATE include/shared include/server ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(
    kvstore_server
    PUBLIC
        sel4_autoconf
        muslc
        sel4
        utils
        sel4runtime
        sel4muslcsys
        sel4gpi
        sqlite3
        fs_client
        sel4nanopb
)