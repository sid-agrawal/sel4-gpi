#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(libsel4gpi C)

file(GLOB deps src/*.c)

list(SORT deps)

add_library(sel4gpi STATIC EXCLUDE_FROM_ALL ${deps})
target_include_directories(sel4gpi PUBLIC include)
target_link_libraries(sel4gpi muslc sel4 utils sel4test sel4serialserver sel4allocman sel4bench)
