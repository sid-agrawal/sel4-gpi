/*
 * Copyright 2022, UNSW (ABN 57 195 873 179)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sel4gpi/cpu_clientapi.h>
#include <sel4gpi/pd_creation.h>
#include <osm-vmm/vmm.h>

// /* Helpers for emulating the fault and getting fault details */
bool fault_advance_vcpu(cpu_client_context_t *vm_cpu, seL4_UserContext *regs);
bool fault_advance(cpu_client_context_t *vm_cpu, seL4_UserContext *regs, uint64_t addr, uint64_t fsr, uint64_t reg_val);
