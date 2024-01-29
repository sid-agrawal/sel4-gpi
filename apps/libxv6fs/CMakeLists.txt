#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(libxv6fs C)

file(GLOB deps src/*.c)

list(SORT deps)

add_library(xv6fs STATIC EXCLUDE_FROM_ALL ${deps})
target_include_directories(xv6fs PUBLIC include PRIVATE include_private)
target_link_libraries(xv6fs muslc sel4 utils sel4test sel4serialserver)
