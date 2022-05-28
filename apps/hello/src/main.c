/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <autoconf.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <arch_stdio.h>
#include <allocman/vka.h>
#include <allocman/bootstrap.h>

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4platsupport/timer.h>

#include <sel4utils/util.h>
#include <sel4utils/mapping.h>
#include <sel4utils/vspace.h>

#include <sel4test/test.h>

#include <vka/capops.h>

#include "helpers.h"
#include "test.h"
#include "init.h"
#include <rpc.pb.h>

int main(int argc, char **argv)
{
    sel4muslcsys_register_stdio_write_fn(write_buf);

    printf("hello works!\n");

    return 0;
}
