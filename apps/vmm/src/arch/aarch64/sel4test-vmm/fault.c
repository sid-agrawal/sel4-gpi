/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright 2022, UNSW (ABN 57 195 873 179)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "hsr.h"
#include "smc.h"
#include "vgic/vgic.h"
#include <sel4gpi/debug.h>
#include <vmm-common/fault_common.h>
#include <vmm-common/vmm_common.h>
#include <sel4test-vmm/fault.h>
#include <sel4test-vmm/vmm.h>
#include <sel4test-vmm/vcpu.h>

bool fault_advance_vcpu(size_t vcpu_id, seL4_UserContext *regs)
{
    // For now we just ignore it and continue
    // Assume 64-bit instruction
    regs->pc += 4;
    int err = seL4_TCB_WriteRegisters(vcpu_id, true, 0, SEL4_USER_CONTEXT_SIZE, regs);
    assert(err == seL4_NoError);

    return (err == seL4_NoError);
}

enum fault_width
{
    WIDTH_DOUBLEWORD = 0,
    WIDTH_WORD = 1,
    WIDTH_HALFWORD = 2,
    WIDTH_BYTE = 3,
};

static enum fault_width fault_get_width(uint64_t fsr)
{
    if (HSR_IS_SYNDROME_VALID(fsr))
    {
        switch (HSR_SYNDROME_WIDTH(fsr))
        {
        case 0:
            return WIDTH_BYTE;
        case 1:
            return WIDTH_HALFWORD;
        case 2:
            return WIDTH_WORD;
        case 3:
            return WIDTH_DOUBLEWORD;
        default:
            // @ivanv: reviist
            // print_fault(f);
            assert(0);
            return 0;
        }
    }
    else
    {
        ZF_LOGE("Received invalid FSR: 0x%lx\n", fsr);
        // @ivanv: reviist
        // int rt;
        // rt = decode_instruction(f);
        // assert(rt >= 0);
        return -1;
    }
}

uint64_t fault_get_data_mask(uint64_t addr, uint64_t fsr)
{
    uint64_t mask = 0;
    switch (fault_get_width(fsr))
    {
    case WIDTH_BYTE:
        mask = 0x000000ff;
        assert(!(addr & 0x0));
        break;
    case WIDTH_HALFWORD:
        mask = 0x0000ffff;
        assert(!(addr & 0x1));
        break;
    case WIDTH_WORD:
        mask = 0xffffffff;
        assert(!(addr & 0x3));
        break;
    case WIDTH_DOUBLEWORD:
        mask = ~mask;
        break;
    default:
        ZF_LOGE("unknown width: 0x%x, from FSR: 0x%lx, addr: 0x%lx\n",
                fault_get_width(fsr), fsr, addr);
        assert(0);
        return 0;
    }
    mask <<= (addr & 0x3) * 8;
    return mask;
}

static seL4_Word wzr = 0;
seL4_Word *decode_rt(uint64_t reg, seL4_UserContext *regs)
{
    switch (reg)
    {
    case 0:
        return &regs->x0;
    case 1:
        return &regs->x1;
    case 2:
        return &regs->x2;
    case 3:
        return &regs->x3;
    case 4:
        return &regs->x4;
    case 5:
        return &regs->x5;
    case 6:
        return &regs->x6;
    case 7:
        return &regs->x7;
    case 8:
        return &regs->x8;
    case 9:
        return &regs->x9;
    case 10:
        return &regs->x10;
    case 11:
        return &regs->x11;
    case 12:
        return &regs->x12;
    case 13:
        return &regs->x13;
    case 14:
        return &regs->x14;
    case 15:
        return &regs->x15;
    case 16:
        return &regs->x16;
    case 17:
        return &regs->x17;
    case 18:
        return &regs->x18;
    case 19:
        return &regs->x19;
    case 20:
        return &regs->x20;
    case 21:
        return &regs->x21;
    case 22:
        return &regs->x22;
    case 23:
        return &regs->x23;
    case 24:
        return &regs->x24;
    case 25:
        return &regs->x25;
    case 26:
        return &regs->x26;
    case 27:
        return &regs->x27;
    case 28:
        return &regs->x28;
    case 29:
        return &regs->x29;
    case 30:
        return &regs->x30;
    case 31:
        return &wzr;
    default:
        printf("invalid reg %ld\n", reg);
        assert(!"Invalid register");
        return NULL;
    }
}

bool fault_is_write(uint64_t fsr)
{
    return (fsr & (1U << 6)) != 0;
}

bool fault_is_read(uint64_t fsr)
{
    return !fault_is_write(fsr);
}

static int get_rt(uint64_t fsr)
{

    int rt = -1;
    if (HSR_IS_SYNDROME_VALID(fsr))
    {
        rt = HSR_SYNDROME_RT(fsr);
    }
    else
    {
        printf("decode_insturction for AArch64 not implemented\n");
        assert(0);
        // @ivanv: implement decode instruction for aarch64
        // rt = decode_instruction(f);
    }
    assert(rt >= 0);
    return rt;
}

uint64_t fault_get_data(seL4_UserContext *regs, uint64_t fsr)
{
    /* Get register opearand */
    int rt = get_rt(fsr);

    uint64_t data = *decode_rt(rt, regs);

    return data;
}

uint64_t fault_emulate(seL4_UserContext *regs, uint64_t reg, uint64_t addr, uint64_t fsr, uint64_t reg_val)
{
    uint64_t m, s;
    s = (addr & 0x3) * 8;
    m = fault_get_data_mask(addr, fsr);
    if (fault_is_read(fsr))
    {
        /* Read data must be shifted to lsb */
        return (reg & ~(m >> s)) | ((reg_val & m) >> s);
    }
    else
    {
        /* Data to write must be shifted left to compensate for alignment */
        return (reg & ~m) | ((reg_val << s) & m);
    }
}

void fault_emulate_write(seL4_UserContext *regs, size_t addr, size_t fsr, size_t reg_val)
{
    // @ivanv: audit
    /* Get register opearand */
    int rt = get_rt(fsr);
    seL4_Word *reg_ctx = decode_rt(rt, regs);
    *reg_ctx = fault_emulate(regs, *reg_ctx, addr, fsr, reg_val);
}

bool fault_advance(size_t vcpu_id, seL4_UserContext *regs, uint64_t addr, uint64_t fsr, uint64_t reg_val)
{
    /* Get register opearand */
    int rt = get_rt(fsr);

    seL4_Word *reg_ctx = decode_rt(rt, regs);
    *reg_ctx = fault_emulate(regs, *reg_ctx, addr, fsr, reg_val);
    // DFAULT("%s: Emulate fault @ 0x%x from PC 0x%x\n",
    //        fault->vcpu->vm->vm_name, fault->addr, fault->ip);

    return fault_advance_vcpu(vcpu_id, regs);
}

bool fault_handle_vcpu_exception(vm_context_t *vm)
{
    uint32_t hsr = seL4_GetMR(seL4_VCPUFault_HSR);
    uint64_t hsr_ec_class = HSR_EXCEPTION_CLASS(hsr);

    seL4_UserContext regs;
    int err = seL4_TCB_ReadRegisters(vm->tcb.cptr, false, 0, SEL4_USER_CONTEXT_SIZE, &regs);
    assert(err == seL4_NoError);
    switch (hsr_ec_class)
    {
    case HSR_SMC_64_EXCEPTION:
        return handle_smc(&regs, hsr);
    case HSR_WFx_EXCEPTION:
        // If we get a WFI exception, we just do nothing in the VMM.
        CPRINTF("WFI exception\n");
        return true;
    default:
        ZF_LOGE("unknown SMC exception, EC class: 0x%lx, HSR: 0x%x\n", hsr_ec_class, hsr);
        return false;
    }
}

bool fault_handle_vppi_event(size_t vcpu_id)
{
    uint64_t ppi_irq = seL4_GetMR(seL4_VPPIEvent_IRQ);
    // We directly inject the interrupt assuming it has been previously registered.
    // If not the interrupt will dropped by the VM.
    bool success = vgic_inject_irq(vcpu_id, ppi_irq);
    if (!success)
    {
        // @ivanv, make a note that when having a lot of printing on it can cause this error
        ZF_LOGE("VPPI IRQ %lu dropped on vCPU %ld\n", ppi_irq, vcpu_id);
        // Acknowledge to unmask it as our guest will not use the interrupt
        // microkit_arm_vcpu_ack_vppi(vcpu_id, ppi_irq); // XXX
    }

    return true;
}

bool fault_handle_user_exception(size_t vcpu_id)
{
    // @ivanv: print out VM name/vCPU id when we have multiple VMs
    size_t fault_ip = seL4_GetMR(seL4_UserException_FaultIP);
    size_t number = seL4_GetMR(seL4_UserException_Number);
    ZF_LOGE("Invalid instruction fault at IP: 0x%lx, number: 0x%lx", fault_ip, number);
    /* All we do is dump the TCB registers. */
    // tcb_print_regs(vcpu_id);

    return true;
}

// @ivanv: document where these come from
#define SYSCALL_PA_TO_IPA 65
#define SYSCALL_NOP 67

bool fault_handle_unknown_syscall(size_t vcpu_id)
{
    // @ivanv: should print out the name of the VM the fault came from.
    size_t syscall = seL4_GetMR(seL4_UnknownSyscall_Syscall);
    size_t fault_ip = seL4_GetMR(seL4_UnknownSyscall_FaultIP);
    ZF_LOGI("Received syscall 0x%lx\n", syscall);
    switch (syscall)
    {
    case SYSCALL_PA_TO_IPA:
        // @ivanv: why do we not do anything here?
        // @ivanv, how to get the physical address to translate?
        ZF_LOGI("Received PA translation syscall\n");
        break;
    case SYSCALL_NOP:
        ZF_LOGI("Received NOP syscall\n");
        break;
    default:
        ZF_LOGE("Unknown syscall: syscall number: 0x%lx, PC: 0x%lx\n", syscall, fault_ip);
        return false;
    }

    seL4_UserContext regs;
    seL4_Error err = seL4_TCB_ReadRegisters(vcpu_id, false, 0, SEL4_USER_CONTEXT_SIZE, &regs);
    assert(err == seL4_NoError);
    if (err != seL4_NoError)
    {
        ZF_LOGE("Failure reading TCB registers when handling unknown syscall, error %d", err);
        return false;
    }

    return fault_advance_vcpu(vcpu_id, &regs);
}

struct vm_exception_handler
{
    uintptr_t base;
    uintptr_t end;
    vm_exception_handler_t callback;
    void *data;
};
#define MAX_VM_EXCEPTION_HANDLERS 16
struct vm_exception_handler registered_vm_exception_handlers[MAX_VM_EXCEPTION_HANDLERS];
size_t vm_exception_handler_index = 0;

bool fault_register_vm_exception_handler(uintptr_t base, size_t size, vm_exception_handler_t callback, void *data)
{
    // @ivanv audit necessary here since this code was written very quickly. Other things to check such
    // as the region of memory is not overlapping with other regions, also should have GIC_DIST regions
    // use this API.
    if (vm_exception_handler_index == MAX_VM_EXCEPTION_HANDLERS - 1)
    {
        return false;
    }

    // @ivanv: use a define for page size? preMAture GENeraliZAATION
    if (base % 0x1000 != 0)
    {
        return false;
    }

    registered_vm_exception_handlers[vm_exception_handler_index] = (struct vm_exception_handler){
        .base = base,
        .end = base + size,
        .callback = callback,
        .data = data,
    };
    vm_exception_handler_index += 1;

    return true;
}

static bool fault_handle_registered_vm_exceptions(size_t vcpu_id, uintptr_t addr, size_t fsr, seL4_UserContext *regs)
{
    for (int i = 0; i < MAX_VM_EXCEPTION_HANDLERS; i++)
    {
        uintptr_t base = registered_vm_exception_handlers[i].base;
        uintptr_t end = registered_vm_exception_handlers[i].end;
        vm_exception_handler_t callback = registered_vm_exception_handlers[i].callback;
        void *data = registered_vm_exception_handlers[i].data;
        if (addr >= base && addr < end)
        {
            bool success = callback(vcpu_id, addr - base, fsr, regs, data);
            if (!success)
            {
                // @ivanv: improve error message
                ZF_LOGE("registered virtual memory exception handler for region [0x%lx..0x%lx) at address 0x%lx failed\n", base, end, addr);
            }
            /* Whether or not the callback actually successfully handled the
             * exception, we return true to say that we at least found a handler
             * for the faulting address. */
            return true;
        }
    }

    /* We could not find a handler for the faulting address. */
    return false;
}

bool fault_handle_vm_exception(size_t vcpu_id)
{
    uintptr_t addr = seL4_GetMR(seL4_VMFault_Addr);
    size_t fsr = seL4_GetMR(seL4_VMFault_FSR);

    seL4_UserContext regs;
    int err = seL4_TCB_ReadRegisters(vcpu_id, false, 0, SEL4_USER_CONTEXT_SIZE, &regs);
    // assert(err == seL4_NoError);
    printf("addr: %lx\n", addr);
    return false;
    switch (addr)
    {
    case GIC_DIST_PADDR ... GIC_DIST_PADDR + GIC_DIST_SIZE:
        return handle_vgic_dist_fault(vcpu_id, addr, fsr, &regs);
#if defined(GIC_V3)
    /* Need to handle redistributor faults for GICv3 platforms. */
    case GIC_REDIST_PADDR ... GIC_REDIST_PADDR + GIC_REDIST_SIZE:
        return handle_vgic_redist_fault(vcpu_id, addr, fsr, &regs);
#endif
    default:
    {
        bool success = fault_handle_registered_vm_exceptions(vcpu_id, addr, fsr, &regs);
        if (!success)
        {
            /*
             * We could not find a registered handler for the address, meaning that the fault
             * is genuinely unexpected. Surprise!
             * Now we print out as much information relating to the fault as we can, hopefully
             * the programmer can figure out what went wrong.
             */
            size_t ip = seL4_GetMR(seL4_VMFault_IP);
            size_t is_prefetch = seL4_GetMR(seL4_VMFault_PrefetchFault);
            bool is_write = fault_is_write(fsr);
            ZF_LOGE("unexpected memory fault on address: 0x%lx, FSR: 0x%lx, IP: 0x%lx, is_prefetch: %s, is_write: %s\n",
                    addr, fsr, ip, is_prefetch ? "true" : "false", is_write ? "true" : "false");
            // tcb_print_regs(vcpu_id);
            // vcpu_print_regs(vcpu_id); // XXX
        }
        else
        {
            /* @ivanv, is it correct to unconditionally advance the CPU here? */
            fault_advance_vcpu(vcpu_id, &regs);
        }

        return success;
    }
    }
}

bool fault_handle(vm_context_t *vm, seL4_MessageInfo_t *msg)
{
    bool success = false;
    size_t label = seL4_MessageInfo_ptr_get_label(msg);
    switch (label)
    {
    case seL4_Fault_VMFault:
        printf("fault_handle_vm_exception\n");
        success = fault_handle_vm_exception(0);
        break;
    case seL4_Fault_UnknownSyscall:
        printf("fault_handle_unknown_syscall\n");
        // success = fault_handle_unknown_syscall(vcpu);
        break;
    case seL4_Fault_UserException:
        printf("fault_handle_user_exception\n");
        // success = fault_handle_user_exception(vcpu);
        break;
    case seL4_Fault_VGICMaintenance:
        printf("fault_handle_vgic_maintenance\n");
        // success = fault_handle_vgic_maintenance(vcpu);
        break;
    case seL4_Fault_VCPUFault:
        printf("fault_handle_vcpu_exception\n");
        success = fault_handle_vcpu_exception(vm);
        break;
    case seL4_Fault_VPPIEvent:
        printf("fault_handle_vppi_event\n");
        // success = fault_handle_vppi_event(vcpu);
        break;
    default:
        /* We have reached a genuinely unexpected case, stop the guest. */
        printf("unknown fault label 0x%lx, stopping guest with ID 0x%lx\n", label, vm->vcpu.cptr);
        // microkit_vm_stop(vcpu); // XXX
        /* Dump the TCB and vCPU registers to hopefully get information as
         * to what has gone wrong. */
        // tcb_print_regs(vcpu);
        // vcpu_print_regs(vcpu); // XXX
    }

    if (!success)
    {
        printf("Failed to handle %s fault\n", fault_to_string(label));
        vcpu_print_regs(vm->vcpu.cptr);
    }

    return success;
}
