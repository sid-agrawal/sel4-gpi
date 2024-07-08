/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright 2022, UNSW (ABN 57 195 873 179)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sel4/sel4.h>
#include <sel4test-vmm/vmm.h>

/* SMC vCPU fault handler */
bool handle_smc(seL4_UserContext *regs, uint32_t hsr);

/* Helper functions */
void smc_set_return_value(seL4_UserContext *u, uint64_t val);

/* Gets the value of x1-x6 */
uint64_t smc_get_arg(seL4_UserContext *u, uint64_t arg);
