#
# Copyright 2024 Ethan Xu
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)
project(sddf_blk_client C)

file(GLOB deps src/*.c)

add_executable(sddf-blk-client EXCLUDE_FROM_ALL ${deps})

target_include_directories(sddf-blk-client PRIVATE include)

target_link_libraries(
  sddf-blk-client
    PUBLIC 
         muslc
         sel4
         sel4runtime
         sel4allocman
         sel4vka
         sel4utils
         sel4rpc
         sel4test
         sel4sync
         sel4muslcsys
         sel4testsupport
         sel4gpi
         sel4serialserver_tests
    PRIVATE sel4_autoconf
  )
