/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <sel4gpi/thread.h>
#include <sel4test/testutil.h>
#include <sel4test/macros.h>

#include <utils/util.h>

#include <serial_server/test.h>

int thread_create_with_isolated_stack(void * fun){

    // mr.new()
    // as.clone()
    // as.remove()
    // as.attach()
    // cpu.new()
    // cpu.attach()
    
    return 0;
}


int process_create() {

   // pd.new() // creates new as, mr, cpu
}

int thread_create() {

    // cpu.new()
    // cpu.attach()
    // cpu.start()
    
}


