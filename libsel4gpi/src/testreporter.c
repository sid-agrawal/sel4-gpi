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

#include <sel4gpi/testreporter.h>
#include <sel4test/testutil.h>
#include <sel4test/macros.h>

#include <utils/util.h>

#include <serial_server/test.h>

/* Used to ensure that serial server parent tests are included */
UNUSED void dummy_func()
{
    printf("hello from dumy func");
}

