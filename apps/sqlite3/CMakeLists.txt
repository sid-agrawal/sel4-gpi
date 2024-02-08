#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(sqlite3 C)

file(GLOB deps src/*.c)

list(SORT deps)

add_library(sqlite3 STATIC EXCLUDE_FROM_ALL ${deps})
target_include_directories(sqlite3 PUBLIC include)
target_link_libraries(sqlite3 muslc)

add_compile_definitions(_HAVE_SQLITE_CONFIG_H=1)
