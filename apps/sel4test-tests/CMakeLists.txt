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

# include(cpio)
# set(cpio_files "")
# New binaries in the CPIO go here.
# list(APPEND cpio_files "$<TARGET_FILE:hello>")
# list(APPEND cpio_files "$<TARGET_FILE:vmm>")
# MakeCPIO(archive.o "${cpio_files}")

add_executable(sel4test-tests EXCLUDE_FROM_ALL ${deps})
target_include_directories(
    sel4test-tests
    PRIVATE include arch/${KernelArch} sel4_arch/${KernelSel4Arch}
)

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
        sel4vmm
    PRIVATE sel4test-driver_Config
)
