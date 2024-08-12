/*
 * Copyright 2022, UNSW (ABN 57 195 873 179)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sel4test-vmm/vmm.h>

/* Helpers for emulating the fault and getting fault details */
bool fault_advance_vcpu(seL4_CPtr tcb, seL4_UserContext *regs);
bool fault_advance(seL4_CPtr tcb, seL4_UserContext *regs, uint64_t addr, uint64_t fsr, uint64_t reg_val);
