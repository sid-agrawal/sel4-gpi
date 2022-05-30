/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <autoconf.h>

#include <stdio.h>
#include <stdlib.h>
#include "hello.h"

int main(int argc, char **argv)
{
//    sel4muslcsys_register_stdio_write_fn(write_buf);

    printf("hello works!\n");

    return 0;
}
