/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright 2022, UNSW (ABN 57 195 873 179)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4debug/register_dump.h>
#include <sel4gpi/debug.h>
#include <vmm-common/fault.h>
#include <vmm-common/vmm.h>
#include <vmm-common/hsr.h>
#include <vmm-common/smc.h>
#include <osm-vmm/fault.h>
#include <osm-vmm/vmm.h>
// #include <osm-vmm/vcpu.h>
// #include <osm-vmm/vgic/vgic.h>

// bool fault_advance_vcpu(seL4_CPtr tcb, seL4_UserContext *regs)
// {
//     // For now we just ignore it and continue
//     // Assume 64-bit instruction
//     regs->pc += 4;
//     int err = seL4_TCB_WriteRegisters(tcb, true, 0, SEL4_USER_CONTEXT_SIZE, regs);
//     assert(err == seL4_NoError);

//     return (err == seL4_NoError);
// }

// bool fault_advance(seL4_CPtr tcb, seL4_UserContext *regs, uint64_t addr, uint64_t fsr, uint64_t reg_val)
// {
//     /* Get register opearand */
//     int rt = get_rt(fsr);

//     seL4_Word *reg_ctx = decode_rt(rt, regs);
//     *reg_ctx = fault_emulate(regs, *reg_ctx, addr, fsr, reg_val);
//     // DFAULT("%s: Emulate fault @ 0x%x from PC 0x%x\n",
//     //        fault->vcpu->vm->vm_name, fault->addr, fault->ip);

//     return fault_advance_vcpu(tcb, regs);
// }

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
        if (handle_smc(GUEST_VCPU_ID, &regs, hsr))
        {
            return fault_advance_vcpu(vm->tcb.cptr, &regs);
        }
        return false;
    case HSR_WFx_EXCEPTION:
        // If we get a WFI exception, we just do nothing in the VMM.
        return true;
    default:
        VMM_PRINTERR("unknown SMC exception, EC class: 0x%lx, HSR: 0x%x\n", hsr_ec_class, hsr);
        return false;
    }
}

bool fault_handle_vppi_event(vm_context_t *vm)
{
    uint64_t ppi_irq = seL4_GetMR(seL4_VPPIEvent_IRQ);
    // We directly inject the interrupt assuming it has been previously registered.
    // If not the interrupt will dropped by the VM.
    bool success = vgic_inject_irq(vm->vcpu.cptr, GUEST_VCPU_ID, ppi_irq);
    if (!success)
    {
        // @ivanv, make a note that when having a lot of printing on it can cause this error
        VMM_PRINTERR("VPPI IRQ %lu dropped on VM %u\n", ppi_irq, vm->id);
        // Acknowledge to unmask it as our guest will not use the interrupt
        seL4_Error err = seL4_ARM_VCPU_AckVPPI(vm->vcpu.cptr, ppi_irq);
        if (err)
        {
            VMM_PRINTERR("Failed to ACK VPPI in VM %d\n", vm->id);
        }
    }

    return true;
}

bool fault_handle_user_exception(vm_context_t *vm)
{
    // @ivanv: print out VM name/vCPU id when we have multiple VMs
    size_t fault_ip = seL4_GetMR(seL4_UserException_FaultIP);
    size_t number = seL4_GetMR(seL4_UserException_Number);
    VMM_PRINTERR("Invalid instruction fault at IP: 0x%lx, number: 0x%lx", fault_ip, number);
    /* All we do is dump the TCB registers. */
    sel4debug_dump_registers(vm->tcb.cptr);

    return true;
}

// @ivanv: document where these come from
#define SYSCALL_PA_TO_IPA 65
#define SYSCALL_NOP 67

bool fault_handle_unknown_syscall(vm_context_t *vm)
{
    // @ivanv: should print out the name of the VM the fault came from.
    size_t syscall = seL4_GetMR(seL4_UnknownSyscall_Syscall);
    size_t fault_ip = seL4_GetMR(seL4_UnknownSyscall_FaultIP);
    VMM_PRINT("Received syscall 0x%lx from VM %d\n", syscall, vm->id);
    switch (syscall)
    {
    case SYSCALL_PA_TO_IPA:
        // @ivanv: why do we not do anything here?
        // @ivanv, how to get the physical address to translate?
        VMM_PRINT("Received PA translation syscall\n");
        break;
    case SYSCALL_NOP:
        VMM_PRINT("Received NOP syscall\n");
        break;
    default:
        VMM_PRINTERR("Unknown syscall: syscall number: 0x%lx, PC: 0x%lx\n", syscall, fault_ip);
        return false;
    }

    seL4_UserContext regs;
    seL4_Error err = seL4_TCB_ReadRegisters(vm->tcb.cptr, false, 0, SEL4_USER_CONTEXT_SIZE, &regs);
    assert(err == seL4_NoError);
    if (err != seL4_NoError)
    {
        VMM_PRINTERR("Failure reading TCB registers when handling unknown syscall, error %d", err);
        return false;
    }

    return fault_advance_vcpu(vm->tcb.cptr, &regs);
}

bool fault_handle_vm_exception(vm_context_t *vm)
{
    uintptr_t addr = seL4_GetMR(seL4_VMFault_Addr);
    size_t fsr = seL4_GetMR(seL4_VMFault_FSR);

    seL4_UserContext regs;
    int err = seL4_TCB_ReadRegisters(vm->tcb.cptr, false, 0, SEL4_USER_CONTEXT_SIZE, &regs);
    assert(err == seL4_NoError);

    switch (addr)
    {
    case GIC_DIST_PADDR ... GIC_DIST_PADDR + GIC_DIST_SIZE:
        return handle_vgic_dist_fault(vm->vcpu.cptr, vm->tcb.cptr, GUEST_VCPU_ID, addr, fsr, &regs);
    default:
    {
        bool success = fault_handle_registered_vm_exceptions(vm->id, addr, fsr, &regs);
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
            VMM_PRINTERR("unexpected memory fault on address: 0x%lx, FSR: 0x%lx, IP: 0x%lx, is_prefetch: %s, is_write: %s\n",
                         addr, fsr, ip, is_prefetch ? "true" : "false", is_write ? "true" : "false");
            // sel4debug_dump_registers(vm->tcb.cptr);
            // vcpu_print_regs(vm->vcpu.cptr);
        }
        else
        {
            /* @ivanv, is it correct to unconditionally advance the CPU here? */
            fault_advance_vcpu(vm->tcb.cptr, &regs);
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
        VMM_PRINTV("fault_handle_vm_exception\n");
        success = fault_handle_vm_exception(vm);
        break;
    case seL4_Fault_UnknownSyscall:
        VMM_PRINTV("fault_handle_unknown_syscall\n");
        success = fault_handle_unknown_syscall(vm);
        break;
    case seL4_Fault_UserException:
        VMM_PRINTV("fault_handle_user_exception\n");
        success = fault_handle_user_exception(vm);
        break;
    case seL4_Fault_VGICMaintenance:
        VMM_PRINTV("fault_handle_vgic_maintenance\n");
        success = fault_handle_vgic_maintenance(vm->vcpu.cptr, GUEST_VCPU_ID);
        break;
    case seL4_Fault_VCPUFault:
        VMM_PRINTV("fault_handle_vcpu_exception\n");
        success = fault_handle_vcpu_exception(vm);
        break;
    case seL4_Fault_VPPIEvent:
        VMM_PRINTV("fault_handle_vppi_event\n");
        success = fault_handle_vppi_event(vm);
        break;
    default:
        /* We have reached a genuinely unexpected case, stop the guest. */
        VMM_PRINTERR("unknown fault label 0x%lx, stopping guest with ID 0x%lx\n", label, vm->vcpu.cptr);
        // seL4_TCB_Suspend(vm->tcb.cptr); (XXX) Linh
    }

    if (!success)
    {
        VMM_PRINTERR("Failed to handle %s fault\n", fault_to_string(label));
        // sel4debug_dump_registers(vm->tcb.cptr); (XXX) Linh
        // vcpu_print_regs(vm->vcpu.cptr); (XXX) Linh
    }

    return success;
}
