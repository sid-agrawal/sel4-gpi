#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(libsel4gpi C)

include(nanopb)

SEL4_GENERATE_PROTOBUF(proto_srcs proto_hdrs 
    ${CMAKE_CURRENT_SOURCE_DIR}/ads_component_rpc.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/cpu_component_rpc.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/ep_component_rpc.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/mo_component_rpc.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/pd_component_rpc.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/resspc_component_rpc.proto
)

# CMake options
set(GPIServerEnabled ON CACHE BOOL "If true, run the GPI server")
set(GPIPDDeletionDepth "0" CACHE STRING "PD deletion depth for cleanup policies")
set(GPIRSDeletionDepth "0" CACHE STRING "Resource space deletion depth for cleanup policies")
set(GPINanobenchEnabled OFF CACHE BOOL "If true, output nano-benchmarks")

# Convert CMake options to defines
if (GPIServerEnabled)
    set(GPIServerDef "GPI_SERVER_ENABLED")
else()
    set(GPIServerDef "GPI_SERVER_DISABLED")
endif()

if (GPINanobenchEnabled)
    set(GPINanobenchDef "GPI_NANOBENCH_ENABLED")
else()
    set(GPINanobenchDef "GPI_NANOBENCH_DISABLED")
endif()

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
        elf
)

target_compile_options(sel4gpi PUBLIC 
    "-D${GPIServerDef}" 
    "-D${GPINanobenchDef}" 
    "-DGPI_CLEANUP_PD_DEPTH=${GPIPDDeletionDepth}"
    "-DGPI_CLEANUP_RS_DEPTH=${GPIRSDeletionDepth}"
    # "-fanalyzer"
    )
