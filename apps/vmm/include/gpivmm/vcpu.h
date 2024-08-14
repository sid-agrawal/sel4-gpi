/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <sel4/sel4.h>
#include <stdint.h>
#include <stddef.h>

typedef struct _vcpu_regs
{
    /* VM control registers EL1 */
    seL4_Word sctlr;
    seL4_Word ttbr0;
    seL4_Word ttbr1;
    seL4_Word tcr;
    seL4_Word mair;
    seL4_Word amair;
    seL4_Word cidr;

    /* other system registers el1 */
    seL4_Word actlr;
    seL4_Word cpacr;

    /* exception handling registers el1 */
    seL4_Word afsr0;
    seL4_Word afsr1;
    seL4_Word esr;
    seL4_Word far;
    seL4_Word isr;
    seL4_Word vbar;

    /* thread pointer/id registers el0/el1 */
    seL4_Word tpidr_el1;

#if CONFIG_MAX_NUM_NODES > 1
    /* virtualisation multiprocessor id register */
    seL4_Word vmpidr_el2;
#endif
    /* general registers x0 to x30 have been saved by traps.s */
    seL4_Word sp_el1;
    seL4_Word elr_el1;
    seL4_Word spsr_el1; // 32-bit

    /* generic timer registers; to be completed */
    seL4_Word cntv_ctl;
    seL4_Word cntv_cval;
    seL4_Word cntvoff;
    seL4_Word cntkctl_el1;
} vcpu_regs_t;

void vcpu_reset(seL4_CPtr vcpu);
void vcpu_read_regs(seL4_CPtr vcpu, vcpu_regs_t *regs);
void vcpu_print_regs(vcpu_regs_t *regs);
