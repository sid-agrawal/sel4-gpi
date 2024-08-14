/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <gpivmm/vcpu.h>

#define SCTLR_EL1_UCI (1 << 26)    /* Enable EL0 access to DC CVAU, DC CIVAC, DC CVAC, \
                                    and IC IVAU in AArch64 state   */
#define SCTLR_EL1_C (1 << 2)       /* Enable data and unified caches */
#define SCTLR_EL1_I (1 << 12)      /* Enable instruction cache       */
#define SCTLR_EL1_CP15BEN (1 << 5) /* AArch32 CP15 barrier enable    */
#define SCTLR_EL1_UTC (1 << 15)    /* Enable EL0 access to CTR_EL0   */
#define SCTLR_EL1_NTWI (1 << 16)   /* WFI executed as normal         */
#define SCTLR_EL1_NTWE (1 << 18)   /* WFE executed as normal         */

/* Disable MMU, SP alignment check, and alignment check */
/* A57 default value */
#define SCTLR_EL1_RES 0x30d00800 /* Reserved value */
#define SCTLR_EL1 (SCTLR_EL1_RES | SCTLR_EL1_CP15BEN | SCTLR_EL1_UTC | SCTLR_EL1_NTWI | SCTLR_EL1_NTWE)
#define SCTLR_EL1_NATIVE (SCTLR_EL1 | SCTLR_EL1_C | SCTLR_EL1_I | SCTLR_EL1_UCI)
#define SCTLR_DEFAULT SCTLR_EL1_NATIVE

static seL4_Error vcpu_write_reg(seL4_CPtr vcpu, uint64_t reg, uint64_t value)
{
    return seL4_ARM_VCPU_WriteRegs(vcpu, reg, value);
}

static seL4_Word vcpu_read_reg(seL4_CPtr vcpu, uint64_t reg)
{
    seL4_ARM_VCPU_ReadRegs_t ret;
    ret = seL4_ARM_VCPU_ReadRegs(vcpu, reg);

    if (ret.error != seL4_NoError)
    {
        printf("Warning: VCPU read registers failed\n");
        return -1;
    }

    return ret.value;
}

void vcpu_reset(seL4_CPtr vcpu)
{
    // @ivanv this is an incredible amount of system calls
    // Reset registers
    // @ivanv: double check, shouldn't we be setting sctlr?
    vcpu_write_reg(vcpu, seL4_VCPUReg_SCTLR, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_TTBR0, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_TTBR1, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_TCR, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_MAIR, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_AMAIR, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_CIDR, 0);
    /* other system registers EL1 */
    vcpu_write_reg(vcpu, seL4_VCPUReg_ACTLR, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_CPACR, 0);
    /* exception handling registers EL1 */
    vcpu_write_reg(vcpu, seL4_VCPUReg_AFSR0, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_AFSR1, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_ESR, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_FAR, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_ISR, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_VBAR, 0);
    /* thread pointer/ID registers EL0/EL1 */
    vcpu_write_reg(vcpu, seL4_VCPUReg_TPIDR_EL1, 0);
#if CONFIG_MAX_NUM_NODES > 1
    /* Virtualisation Multiprocessor ID Register */
    vcpu_write_reg(vcpu, seL4_VCPUReg_VMPIDR_EL2, 0);
#endif /* CONFIG_MAX_NUM_NODES > 1 */
    /* general registers x0 to x30 have been saved by traps.S */
    vcpu_write_reg(vcpu, seL4_VCPUReg_SP_EL1, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_ELR_EL1, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_SPSR_EL1, 0); // 32-bit
    /* generic timer registers, to be completed */
    vcpu_write_reg(vcpu, seL4_VCPUReg_CNTV_CTL, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_CNTV_CVAL, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_CNTVOFF, 0);
    vcpu_write_reg(vcpu, seL4_VCPUReg_CNTKCTL_EL1, 0);
}

void vcpu_read_regs(seL4_CPtr vcpu, vcpu_regs_t *regs)
{
    regs->sctlr = vcpu_read_reg(vcpu, seL4_VCPUReg_SCTLR);
    regs->ttbr0 = vcpu_read_reg(vcpu, seL4_VCPUReg_TTBR0);
    regs->ttbr1 = vcpu_read_reg(vcpu, seL4_VCPUReg_TTBR1);
    regs->tcr = vcpu_read_reg(vcpu, seL4_VCPUReg_TCR);
    regs->mair = vcpu_read_reg(vcpu, seL4_VCPUReg_MAIR);
    regs->amair = vcpu_read_reg(vcpu, seL4_VCPUReg_AMAIR);
    regs->cidr = vcpu_read_reg(vcpu, seL4_VCPUReg_CIDR);
    /* other system registers EL1 */
    regs->actlr = vcpu_read_reg(vcpu, seL4_VCPUReg_ACTLR);
    regs->cpacr = vcpu_read_reg(vcpu, seL4_VCPUReg_CPACR);
    /* exception handling registers EL1 */
    regs->afsr0 = vcpu_read_reg(vcpu, seL4_VCPUReg_AFSR0);
    regs->afsr1 = vcpu_read_reg(vcpu, seL4_VCPUReg_AFSR1);
    regs->esr = vcpu_read_reg(vcpu, seL4_VCPUReg_ESR);
    regs->far = vcpu_read_reg(vcpu, seL4_VCPUReg_FAR);
    regs->isr = vcpu_read_reg(vcpu, seL4_VCPUReg_ISR);
    regs->vbar = vcpu_read_reg(vcpu, seL4_VCPUReg_VBAR);
    /* thread pointer/ID registers EL0/EL1 */
    regs->tpidr_el1 = vcpu_read_reg(vcpu, seL4_VCPUReg_TPIDR_EL1);
    // @ivanv: I think thins might not be the correct ifdef
#if CONFIG_MAX_NUM_NODES > 1
    /* Virtualisation Multiprocessor ID Register */
    regs->vmpidr_el2 = vcpu_read_reg(vcpu, seL4_VCPUReg_VMPIDR_EL2);
#endif
    /* general registers x0 to x30 have been saved by traps.S */
    regs->sp_el1 = vcpu_read_reg(vcpu, seL4_VCPUReg_SP_EL1);
    regs->elr_el1 = vcpu_read_reg(vcpu, seL4_VCPUReg_ELR_EL1);
    regs->spsr_el1 = vcpu_read_reg(vcpu, seL4_VCPUReg_SPSR_EL1); // 32-bit // @ivanv what
    /* generic timer registers, to be completed */
    regs->cntv_ctl = vcpu_read_reg(vcpu, seL4_VCPUReg_CNTV_CTL);
    regs->cntv_cval = vcpu_read_reg(vcpu, seL4_VCPUReg_CNTV_CVAL);
    regs->cntvoff = vcpu_read_reg(vcpu, seL4_VCPUReg_CNTVOFF);
    regs->cntkctl_el1 = vcpu_read_reg(vcpu, seL4_VCPUReg_CNTKCTL_EL1);
}

void vcpu_print_regs(vcpu_regs_t *regs)
{
    // @ivanv this is an incredible amount of system calls
    printf("dumping VCPU registers:\n");
    /* VM control registers EL1 */
    printf("    sctlr: 0x%016lx\n", regs->sctlr);
    printf("    ttbr0: 0x%016lx\n", regs->ttbr0);
    printf("    ttbr1: 0x%016lx\n", regs->ttbr1);
    printf("    tcr: 0x%016lx\n", regs->tcr);
    printf("    mair: 0x%016lx\n", regs->mair);
    printf("    amair: 0x%016lx\n", regs->amair);
    printf("    cidr: 0x%016lx\n", regs->cidr);
    /* other system registers EL1 */
    printf("    actlr: 0x%016lx\n", regs->actlr);
    printf("    cpacr: 0x%016lx\n", regs->cpacr);
    /* exception handling registers EL1 */
    printf("    afsr0: 0x%016lx\n", regs->afsr0);
    printf("    afsr1: 0x%016lx\n", regs->afsr1);
    printf("    esr: 0x%016lx\n", regs->esr);
    printf("    far: 0x%016lx\n", regs->far);
    printf("    isr: 0x%016lx\n", regs->isr);
    printf("    vbar: 0x%016lx\n", regs->vbar);
    /* thread pointer/ID registers EL0/EL1 */
    printf("    tpidr_el1: 0x%016lx\n", regs->tpidr_el1);
    // @ivanv: I think thins might not be the correct ifdef
#if CONFIG_MAX_NUM_NODES > 1
    /* Virtualisation Multiprocessor ID Register */
    printf("    vmpidr_el2: 0x%016lx\n", regs->vmpidr_el2);
#endif
    /* general registers x0 to x30 have been saved by traps.S */
    printf("    sp_el1: 0x%016lx\n", regs->sp_el1);
    printf("    elr_el1: 0x%016lx\n", regs->elr_el1);
    printf("    spsr_el1: 0x%016lx\n", regs->spsr_el1); // 32-bit // @ivanv what
    /* generic timer registers, to be completed */
    printf("    cntv_ctl: 0x%016lx\n", regs->cntv_ctl);
    printf("    cntv_cval: 0x%016lx\n", regs->cntv_cval);
    printf("    cntvoff: 0x%016lx\n", regs->cntvoff);
    printf("    cntkctl_el1: 0x%016lx\n", regs->cntkctl_el1);
}
