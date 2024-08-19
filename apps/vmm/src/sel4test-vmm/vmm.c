/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stddef.h>
#include <stdint.h>
#include <vka/vka.h>
#include <sel4/sel4.h>
#include <vka/object.h>
#include <vka/arch/object.h>
#include <vka/capops.h>
#include <vspace/vspace.h>
#include <sel4utils/vspace.h>
#include <sel4utils/vspace_internal.h>
#include <sel4utils/api.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/debug.h>
#include <sel4debug/register_dump.h>
#include <sel4utils/thread.h>
#include <sel4utils/process.h>
#include <gpivmm/vmm.h>
#include <gpivmm/linux.h>
#include <gpivmm/fault.h>
#include <gpivmm/virq.h>
#include <gpivmm/sel4test-vmm.h>
#include <gpivmm/vgic/vgic.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/vcpu.h>

/* Uncomment to use the assembly-embedded kernel images */
// /* Data for the guest's kernel image. */
// extern char _guest_kernel_image[];
// extern char _guest_kernel_image_end[];
// /* Data for the device tree to be passed to the kernel. */
// extern char _guest_dtb_image[];
// extern char _guest_dtb_image_end[];
// /* Data for the initial RAM disk to be passed to the kernel. */
// extern char _guest_initrd_image[];
// extern char _guest_initrd_image_end[];

static vmon_context_t vmon_ctxt;

void vm_suspend(vm_context_t *vm)
{
    seL4_Error err = seL4_TCB_Suspend(vm->tcb.cptr);
    if (err)
    {
        VMM_PRINTERR("Failed to suspend VMM\n");
    }
}

int vm_write_registers(vm_context_t *vm, bool resume, seL4_UserContext *regs, size_t num_regs)
{
    seL4_Error err = seL4_TCB_WriteRegisters(vm->tcb.cptr, resume, 0, num_regs, regs);
    return err != seL4_NoError;
}

int vm_read_registers(vm_context_t *vm, bool suspend, seL4_UserContext *regs, size_t num_regs)
{
    seL4_Error err = seL4_TCB_ReadRegisters(vm->tcb.cptr, suspend, 0, num_regs, regs);
    return err != seL4_NoError;
}

void vm_dump_registers(vm_context_t *vm)
{
    sel4debug_dump_registers(vm->tcb.cptr);
}

void vm_dump_vcpu_registers(vm_context_t *vm)
{
    vcpu_regs_t regs = {0};
    vcpu_read_regs(vm->vcpu.cptr, &regs);
    vcpu_print_regs(&regs);
}

uint32_t vm_get_id(vm_context_t *vm)
{
    return vm->id;
}

int vm_inject_irq(vm_context_t *vm, int virq, int prio, int group, int idx)
{
    return seL4_ARM_VCPU_InjectIRQ(vm->vcpu.cptr, virq, prio, group, idx);
}

int vm_ack_vppi(vm_context_t *vm, uint64_t irq)
{
    return seL4_ARM_VCPU_AckVPPI(vm->vcpu.cptr, irq);
}

static void serial_ack(vm_context_t *vm, int irq, void *cookie)
{
    /*
     * For now we by default simply ack the serial IRQ, we have not
     * come across a case yet where more than this needs to be done.
     */
    VMM_PRINT("Acking serial interrupt\n");
    seL4_Error error = seL4_IRQHandler_Ack(vmon_ctxt.serial_irq_handler);
    WARN_IF_COND(error, "Failed to ACK serial interrupt, seL4_Error: %d\n", error);
}

static void handle_fault(void)
{
    vm_context_t *vm = NULL;
    seL4_Word badge = 0;
    seL4_MessageInfo_t info = {0};
    uint32_t vm_id = 0;
    while (1)
    {
        info = seL4_Recv(vmon_ctxt.vm_fault_ep.cptr, &badge);
        if (FAULT_BADGE_FLAG & badge)
        {
            vm_id = (uint32_t)(badge & ~FAULT_BADGE_FLAG);

            if (vm_id == 0 || vm_id >= MAX_GUEST_COUNT)
            {
                VMM_PRINTERR("Fault received from invalid VM: %u\n", vm_id);
                continue;
            }

            VMM_PRINTV("VM %u fault: %s\n", vm_id, fault_to_string(seL4_MessageInfo_get_label(info)));

            vm = vmon_ctxt.guests[vm_id];
            assert(vm != NULL);
            fault_handle(vm, &info);
        }
        else if (badge & SERIAL_IRQ_BIT)
        {
            VMM_PRINT("Got serial IRQ\n");
            if (!virq_inject(vm, GUEST_VCPU_ID, SERIAL_IRQ))
            {
                VMM_PRINTERR("Failed to inject serial IRQ %d into vCPU %d\n", SERIAL_IRQ, GUEST_VCPU_ID);
            }
        }
        else
        {
            VMM_PRINTERR("Received Unexpected fault or IRQ notification for badge: %lx\n", badge);
        }
    }
}

static int setup_dev_frames(vka_t *vka,
                            vm_context_t *vm,
                            size_t num_pages,
                            uintptr_t paddr,
                            size_t vm_dev_frame_idx)
{
    int error = 0;
    VMM_PRINT("Setting up dev frame at 0x%lx with %zu pages\n", paddr, num_pages);
    uintptr_t curr = paddr;
    seL4_CPtr *caps = calloc(num_pages, sizeof(seL4_CPtr));

    for (size_t i = 0; i < num_pages; i++)
    {
        error = vka_alloc_frame_at(vka, seL4_PageBits, curr, &vm->dev_frames[vm_dev_frame_idx]);
        GOTO_IF_ERR(error, "Failed to allocate odroid serial frame");
        curr += SIZE_BITS_TO_BYTES(seL4_PageBits);
        caps[i] = vm->dev_frames[vm_dev_frame_idx].cptr;
        vm_dev_frame_idx++;
    }

    reservation_t res = vspace_reserve_range_at(&vm->vspace, (void *)paddr,
                                                num_pages * SIZE_BITS_TO_BYTES(seL4_PageBits),
                                                seL4_AllRights, 0);
    GOTO_IF_COND(res.res == NULL, "Failed to reserve range\n");
    error = vspace_map_pages_at_vaddr(&vm->vspace, caps, NULL, (void *)paddr, num_pages, seL4_PageBits, res);
    GOTO_IF_ERR(error, "Failed to map devie region to VM");

    free(caps);
err_goto:
    return error;
}

static int start_handler_threads(vka_t *vka, vspace_t *vspace, simple_t *simple, seL4_CPtr fault_ep)
{
    int error = 0;
    // assumming that the the root CNode for this process is in the default slot
    sel4utils_thread_config_t t_cfg = thread_config_default(simple,
                                                            SEL4UTILS_CNODE_SLOT,
                                                            api_make_guard_skip_word(seL4_WordBits - VMON_CNODE_BITS),
                                                            fault_ep,
                                                            seL4_MaxPrio - 1);

    sel4utils_thread_t fault_thread;
    error = sel4utils_configure_thread_config(vka, vspace, vspace, t_cfg, &fault_thread);
    GOTO_IF_ERR(error, "Failed to configure thread\n");

    error = sel4utils_start_thread(&fault_thread, (sel4utils_thread_entry_fn)handle_fault, NULL, NULL, 1);
    GOTO_IF_ERR(error, "Failed to start thread\n");

    error = seL4_TCB_BindNotification(fault_thread.tcb.cptr, vmon_ctxt.irq_ntfn.cptr);
    GOTO_IF_ERR(error, "seL4_Error: %d, Failed to bind IRQ handling notification to interrupt handling thread\n", error);
err_goto:
    return error;
}

int sel4test_vmm_init(seL4_IRQHandler irq_handler,
                      vka_t *vka,
                      vspace_t *vspace,
                      seL4_CPtr asid_pool,
                      simple_t *simple,
                      seL4_CPtr tcb,
                      seL4_CPtr fault_ep)
{
    int error = 0;
    memset(&vmon_ctxt, 0, sizeof(vmon_context_t));
    vmon_ctxt.serial_irq_handler = irq_handler;
    vmon_ctxt.vka = vka;
    vmon_ctxt.vspace = vspace;
    vmon_ctxt.asid_pool = asid_pool;
    vmon_ctxt.simple = simple;
    vmon_ctxt.tcb = tcb;
    vmon_ctxt.guest_id_counter = 1;

    error = vmm_init_virq(GUEST_VCPU_ID, &serial_ack);
    GOTO_IF_ERR(error, "Failed to initialize VIRQ controller\n");

    /* fault endpoint */
    error = vka_alloc_endpoint(vka, &vmon_ctxt.vm_fault_ep);
    GOTO_IF_ERR(error, "Failed to allocate a fault endpoint to listen for VM faults\n");

    /* interrupt notification */
    error = vka_alloc_notification(vka, &vmon_ctxt.irq_ntfn);
    GOTO_IF_ERR(error, "Failed to allocate notification");

    /* make a badge for the serial device interrupt */
    cspacepath_t src, dest;
    error = vka_cspace_alloc_path(vka, &dest);
    GOTO_IF_ERR(error, "Failed to allocate slot for the badged notification");

    vka_cspace_make_path(vka, vmon_ctxt.irq_ntfn.cptr, &src);

    error = vka_cnode_mint(&dest, &src, seL4_AllRights, SERIAL_IRQ_BIT);
    GOTO_IF_ERR(error, "Failed to mint notification badge for serial interrupts");

    error = seL4_IRQHandler_SetNotification(vmon_ctxt.serial_irq_handler, dest.capPtr);
    GOTO_IF_ERR(error, "Failed to set serial IRQ notification");

    vmon_ctxt.serial_irq_ntfn = dest.capPtr;

    error = start_handler_threads(vka, vspace, simple, fault_ep);

err_goto:
    return error;
}

static int setup_guest_devices(vm_context_t *vm, const char *kernel_img_name,
                               uint64_t kernel_img_offset, copy_kernel_image_fn_t kernel_img_cp_fn,
                               uint64_t guest_id, uintptr_t *kernel_pc)
{
    int error = 0;
    vka_t *vka = vmon_ctxt.vka;
    vspace_t *vspace = vmon_ctxt.vspace;

    /* vm's vspace */
    error = vka_alloc_vspace_root(vka, &vm->vspace_root);
    GOTO_IF_ERR(error, "Failed to allocate vm's page directory");

    error = seL4_ARCH_ASIDPool_Assign(vmon_ctxt.asid_pool, vm->vspace_root.cptr);
    GOTO_IF_ERR(error, "Failed to assign vm's vspace to an asid pool");

    error = sel4utils_get_empty_vspace(vspace, &vm->vspace, &vm->vspace_data, vka, vm->vspace_root.cptr, NULL, NULL);
    GOTO_IF_ERR(error, "Failed to make vm's vspace");

    /* vm's cspace */
    error = vka_alloc_cnode_object(vka, VM_CNODE_BITS, &vm->cspace);
    GOTO_IF_ERR(error, "Failed to allocate vm's cspace");

    cspacepath_t src;
    vka_cspace_make_path(vka, vm->cspace.cptr, &src);

    cspacepath_t next_slot = {
        .capPtr = 1,
        .root = vm->cspace.cptr,
        .capDepth = VM_CNODE_BITS};

    seL4_Word cnode_guard = api_make_guard_skip_word(seL4_WordBits - VM_CNODE_BITS);
    error = vka_cnode_mint(&next_slot, &src, seL4_AllRights, cnode_guard);
    next_slot.capPtr++;
    GOTO_IF_ERR(error, "Failed to mint vm's cnode to its cspace");

    /* make a badged fault EP */
    vka_cspace_make_path(vka, vmon_ctxt.vm_fault_ep.cptr, &src);
    error = vka_cnode_mint(&next_slot, &src, seL4_AllRights, FAULT_BADGE_FLAG | (uint64_t)guest_id);
    GOTO_IF_ERR(error, "Failed to mint vm's fault endpoint into its cspace\n");
    vm->fault_ep = next_slot.capPtr;
    next_slot.capPtr++;

    /* vcpu */
    error = vka_alloc_vcpu(vka, &vm->vcpu);
    GOTO_IF_ERR(error, "Failed to allocate a vcpu");

    /* tcb */
    error = vka_alloc_tcb(vka, &vm->tcb);
    GOTO_IF_ERR(error, "Failed to make vm's TCB");

    error = seL4_ARM_VCPU_SetTCB(vm->vcpu.cptr, vm->tcb.cptr);
    GOTO_IF_ERR(error, "Failed to bind TCB to VCPU");

    seL4_CPtr ipc_buf_frame = seL4_CapNull;
    void *ipc_buf = vspace_new_ipc_buffer(&vm->vspace, &ipc_buf_frame);
    GOTO_IF_COND(ipc_buf_frame == seL4_CapNull || ipc_buf == NULL, "Failed to allocate and/or map IPC buffer\n");

    error = seL4_TCB_Configure(vm->tcb.cptr, vm->fault_ep, vm->cspace.cptr, cnode_guard, vm->vspace_root.cptr,
                               0, (seL4_Word)ipc_buf, ipc_buf_frame);
    GOTO_IF_ERR(error, "Failed to configure TCB, seL4_Error: %d\n", error);

    error = seL4_TCB_SetSchedParams(vm->tcb.cptr, simple_get_tcb(vmon_ctxt.simple), seL4_MaxPrio - 1, seL4_MaxPrio - 1);
    GOTO_IF_ERR(error, "Failed to set TCB priority, seL4_Error: %d\n", error);

#ifdef CONFIG_DEBUG_BUILD
    char vm_name[MAX_VM_NAME_LEN];
    snprintf(vm_name, MAX_VM_NAME_LEN, "VM%u", (uint32_t)guest_id);
    seL4_DebugNameThread(vm->tcb.cptr, vm_name);
#endif

    reservation_t res;

    if (strstr(kernel_img_name, LINUX_KERNEL_NAME) != NULL)
    {
        /* GIC vCPU interface region */
        VMM_PRINT("Mapping GIC interface - seL4: %x, linux: %x\n", GIC_PADDR, LINUX_GIC_PADDR);
        error = vka_alloc_frame_at(vka, seL4_PageBits, (uintptr_t)GIC_PADDR, &vm->gic_vcpu_frame);
        GOTO_IF_ERR(error, "Failed to allocate GIC vCPU frame");

        res = vspace_reserve_range_at(&vm->vspace, (void *)LINUX_GIC_PADDR, BIT(seL4_PageBits), seL4_AllRights, 0);
        error = vspace_map_pages_at_vaddr(&vm->vspace, &vm->gic_vcpu_frame.cptr, NULL, (void *)LINUX_GIC_PADDR, 1, seL4_PageBits, res);
        GOTO_IF_ERR(error, "Failed to map GIC vCPU region to VM");
    }
#ifdef BOARD_qemu_arm_virt
    /* map in serial device region */
    vm->n_dev_frames = 1;
    vm->dev_frames = calloc(1, sizeof(vka_object_t));
    error = setup_dev_frames(vka, vm, 1, SERIAL_PADDR, 0);
    GOTO_IF_ERR(error, "Failed to map serial device to VM");
#elif BOARD_odroidc4
    /* Bus 1 and 2 span 2MB, and Bus 3 spans 1 MB
     * not using large pages, since libplatsupport might already have allocated a 4K page in the middle of
     * one of these regions, preventing us from allocating a large page here */
    vm->n_dev_frames = BYTES_TO_4K_PAGES(MiB_TO_BYTES(5));
    vm->dev_frames = calloc(vm->n_dev_frames, sizeof(vka_object_t));

    /* BUS 1 */
    size_t num_pages = BYTES_TO_4K_PAGES(MiB_TO_BYTES(2));
    size_t dev_frame_idx = 0;
    error = setup_dev_frames(vka, vm, num_pages, ODROID_BUS1, dev_frame_idx);
    GOTO_IF_ERR(error, "Failed to setup bus region 0x%u\n", ODROID_BUS1);

    /* BUS 2 */
    dev_frame_idx += num_pages;
    error = setup_dev_frames(vka, vm, num_pages, ODROID_BUS2, dev_frame_idx);
    GOTO_IF_ERR(error, "Failed to setup bus region 0x%u\n", ODROID_BUS2);

    /* BUS 3 */
    num_pages = BYTES_TO_4K_PAGES(MiB_TO_BYTES(1));
    dev_frame_idx += num_pages;
    error = setup_dev_frames(vka, vm, num_pages, ODROID_BUS3, dev_frame_idx);
    GOTO_IF_ERR(error, "Failed to setup bus region 0x%u\n", ODROID_BUS3);
#endif
    /* guest ram */
    size_t guest_ram_pages = BYTES_TO_SIZE_BITS_PAGES(GUEST_RAM_SIZE, seL4_LargePageBits);
    res = vspace_reserve_range_at(&vm->vspace, (void *)GUEST_RAM_VADDR, GUEST_RAM_SIZE, seL4_AllRights, 1);
    seL4_CPtr *ram_frames_in_guest = calloc(guest_ram_pages, sizeof(seL4_CPtr));
    seL4_CPtr *ram_frames_in_vmm = calloc(guest_ram_pages, sizeof(seL4_CPtr));

#ifdef BOARD_qemu_arm_virt
    uintptr_t paddr = GUEST_RAM_PADDR;
    for (size_t i = 0; i < guest_ram_pages; i++)
    {
        vka_object_t frame;
        error = vka_alloc_frame_at(vka, seL4_LargePageBits, paddr, &frame);
        GOTO_IF_ERR(error, "Failed to allocate frame for RAM\n");
        ram_frames_in_guest[i] = frame.cptr;
        /* copy the frame so it can be mapped to current vspace */
        cspacepath_t src, dst;
        vka_cspace_make_path(vka, frame.cptr, &src);
        error = vka_cspace_alloc_path(vka, &dst);
        GOTO_IF_ERR(error, "Failed to allocate slot\n");

        error = vka_cnode_copy(&dst, &src, seL4_AllRights);
        GOTO_IF_ERR(error, "Failed to copy frame\n");
        ram_frames_in_vmm[i] = dst.capPtr;
        paddr += SIZE_BITS_TO_BYTES(seL4_LargePageBits);
    }

#elif BOARD_odroidc4
    for (size_t i = 0; i < guest_ram_pages; i++)
    {
        vka_object_t frame;
        error = vka_alloc_frame(vka, seL4_LargePageBits, &frame);
        GOTO_IF_ERR(error, "Failed to allocate frame for RAM\n");
        ram_frames_in_guest[i] = frame.cptr;

        /* copy the frame so it can be mapped to current vspace */
        cspacepath_t src, dst;
        vka_cspace_make_path(vka, frame.cptr, &src);
        error = vka_cspace_alloc_path(vka, &dst);
        GOTO_IF_ERR(error, "Failed to allocate slot\n");

        error = vka_cnode_copy(&dst, &src, seL4_AllRights);
        GOTO_IF_ERR(error, "Failed to copy frame\n");
        ram_frames_in_vmm[i] = dst.capPtr;
    }
#endif

    error = vspace_map_pages_at_vaddr(&vm->vspace, ram_frames_in_guest, NULL, (void *)GUEST_RAM_VADDR, guest_ram_pages, seL4_LargePageBits, res);
    GOTO_IF_ERR(error, "Failed to map guest RAM in VMM's vspace");

    void *guest_ram_curr_vspace = vspace_map_pages(vspace, ram_frames_in_vmm, NULL, seL4_ReadWrite, guest_ram_pages, seL4_LargePageBits, 1);
    ZF_LOGF_IF(guest_ram_curr_vspace == NULL, "Failed to map guest RAM to VMM's vspace");

    *kernel_pc = kernel_img_cp_fn((uintptr_t)guest_ram_curr_vspace,
                                  kernel_img_name, kernel_img_offset);

err_goto:
    if (ram_frames_in_guest)
    {
        free(ram_frames_in_guest);
    }

    if (ram_frames_in_vmm)
    {
        free(ram_frames_in_vmm);
    }

    if (guest_ram_curr_vspace)
    {
        /* this should also free the copied frame slots */
        vspace_unmap_pages(vspace, guest_ram_curr_vspace, guest_ram_pages, seL4_LargePageBits, vka);
    }

    return error;
}

uint32_t sel4test_new_guest(const char *kernel_image)
{
    int error = 0;
    uint32_t guest_id = vmon_ctxt.guest_id_counter;
    GOTO_IF_COND(vmon_ctxt.guest_id_counter >= MAX_GUEST_COUNT, "Maximum number of guests started\n");
    vm_context_t *vm = calloc(1, sizeof(vm_context_t));

    uintptr_t kernel_pc_vm_vspace = 0;
    uint64_t kernel_img_offset = 0;
    copy_kernel_image_fn_t cp_fn = NULL;
    seL4_UserContext regs = {0};

    if (strstr(kernel_image, LINUX_KERNEL_NAME) != NULL)
    {
        cp_fn = linux_copy_kernel_image;
        regs.x0 = (seL4_Word)GUEST_DTB_VADDR;
    }
    else
    {
        if (strcmp(kernel_image, HELLO_KERNEL_NAME) == 0)
        {
            kernel_img_offset = HELLO_KERNEL_PC_OFFSET;
        }
        cp_fn = generic_copy_kernel_image;
    }

    error = setup_guest_devices(vm, kernel_image, kernel_img_offset, cp_fn, guest_id, &kernel_pc_vm_vspace);
    GOTO_IF_ERR(error, "Failed to setup guest device regions\n");

    /* Just in case there is already an interrupt available to handle, we ack it here. */
    serial_ack(vm, SERIAL_IRQ, NULL);

    regs.spsr = 0x5; // PMODE_EL1h
    regs.pc = (seL4_Word)kernel_pc_vm_vspace;
    // Writing to x0, pc, and spsr
    seL4_Error s_err = seL4_TCB_WriteRegisters(vm->tcb.cptr, true, 0, 4, &regs);
    GOTO_IF_COND(s_err != seL4_NoError, "Failed to start guest\n");

    vmon_ctxt.guests[guest_id] = vm;
    vmon_ctxt.guest_id_counter++;
    vm->id = guest_id;

err_goto:
    return error ? 0 : guest_id;
}
