/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <sel4/sel4.h>
#include <stdint.h>
#include <stddef.h>

void vcpu_reset(seL4_CPtr vcpu);
void vcpu_print_regs(seL4_CPtr vcpu);
