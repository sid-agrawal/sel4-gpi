#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(sel4test-tests C CXX)

# Use arm arch source files on RISCV
set(arch ${KernelArch})
if(KernelArchRiscV)
    set(arch arm)
endif()

file(
    GLOB
        deps
        src/*.c
        src/tests/*.c
        src/tests/*.S
        src/arch/${arch}/*.c
        src/arch/${KernelArch}/tests/*.c
        src/*.cxx
        src/tests/*.cxx
        src/arch/${KernelArch}/tests/*.S
)

# not compiling out-of-date tests that either need to just be removed or updated
list(REMOVE_ITEM deps "${CMAKE_CURRENT_SOURCE_DIR}/src/tests/lwc.c")

add_subdirectory(../sqlite3 sqlite3)

include(cpio)
set(cpio_files "")
# New binaries in the CPIO go here.
list(APPEND cpio_files "$<TARGET_FILE:hello_benchmark>")
MakeCPIO(archive.o "${cpio_files}")

add_executable(sel4test-tests EXCLUDE_FROM_ALL ${deps} archive.o)
target_include_directories(
    sel4test-tests
    PRIVATE include arch/${KernelArch} sel4_arch/${KernelSel4Arch}
)

# Add custom linker script
set(linkerScript "${CMAKE_CURRENT_LIST_DIR}/src/linker.lds")

set_target_properties(
   sel4test-tests
   PROPERTIES LINK_DEPENDS ${linkerScript}
)
add_dependencies(elfloader elfloader_linker)
set_property(
   TARGET sel4test-tests
   APPEND_STRING
   PROPERTY
       LINK_FLAGS
       " -Wl,-T ${linkerScript} -static -Wl,--build-id=none"
)

# Add cmake configuration options
set(GPIBenchmarkIterBits "0" CACHE STRING "Benchmarks will be run 2^n times")

set(GPIExtractModel OFF CACHE BOOL "If true, extract the model state during tests")

if (GPIExtractModel)
    set(GPIExtractModelDef "GPI_EXTRACT_MODEL")
else()
    set(GPIExtractModelDef "GPI_NO_EXTRACT")
endif()

target_link_libraries(
    sel4test-tests
    PUBLIC
        sel4_autoconf
        muslc
        sel4
        sel4runtime
        sel4allocman
        sel4vka
        sel4utils
        sel4rpc
        sel4bench
        sel4test
        sel4sync
        sel4muslcsys
        sel4testsupport
        sel4gpi
        sel4serialserver_tests
        vmm
        ramdisk_client
        fs_client
        sqlite3
        kvstore_client
    PRIVATE sel4test-driver_Config
)

target_compile_options(sel4test-tests PUBLIC
    "-DGPI_BENCHMARK_ITER_BITS=${GPIBenchmarkIterBits}"
    "-D${GPIExtractModelDef}")
