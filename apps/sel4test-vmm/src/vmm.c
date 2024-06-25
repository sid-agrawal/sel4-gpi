/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stddef.h>
#include <stdint.h>
#include "vgic/vgic.h"
#include "linux.h"
#include "fault.h"
#include "guest.h"
#include "virq.h"
#include "tcb.h"
#include <sel4test-vmm/vmm.h>
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
#include <sel4test-vmm/vcpu.h>

// @ivanv: ideally we would have none of these hardcoded values
// initrd, ram size come from the DTB
// We can probably add a node for the DTB addr and then use that.
// Part of the problem is that we might need multiple DTBs for the same example
// e.g one DTB for VMM one, one DTB for VMM two. we should be able to hide all
// of this in the build system to avoid doing any run-time DTB stuff.

/*
 * As this is just an example, for simplicity we just make the size of the
 * guest's "RAM" the same for all platforms. For just booting Linux with a
 * simple user-space, 0x10000000 bytes (256MB) is plenty.
 */
#define GUEST_RAM_SIZE 0x10000000

#if defined(BOARD_qemu_arm_virt)
#define GUEST_RAM_PADDR 0x40000000
#define GUEST_RAM_VADDR 0x40000000
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4d700000
#define SERIAL_PADDR 0x9000000
#define GIC_PADDR 0x8040000
#define LINUX_GIC_PADDR 0x8010000
#elif defined(BOARD_rpi4b_hyp)
#define GUEST_DTB_VADDR 0x2e000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#elif defined(BOARD_odroidc2_hyp)
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#elif defined(BOARD_odroidc4)
#define GUEST_RAM_VADDR 0x20000000
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#define ODROID_BUS1 0xff600000
#define ODROID_BUS2 0xff800000
#define ODROID_BUS3 0xffd00000
#define GIC_PADDR 0xffc06000
#define LINUX_GIC_PADDR 0xffc02000
#elif defined(BOARD_imx8mm_evk_hyp)
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4d700000
#else
#error Need to define guest kernel image address and DTB address
#endif

/* For simplicity we just enforce the serial IRQ channel number to be the same
 * across platforms. */
#define SERIAL_IRQ_CH 1

#if defined(BOARD_qemu_arm_virt)
#define SERIAL_IRQ 33
#elif defined(BOARD_odroidc2_hyp) || defined(BOARD_odroidc4)
#define SERIAL_IRQ 225
#elif defined(BOARD_rpi4b_hyp)
#define SERIAL_IRQ 57
#elif defined(BOARD_imx8mm_evk_hyp)
#define SERIAL_IRQ 79
#else
#error Need to define serial interrupt
#endif

#define VM_CNODE_BITS 17

/* Data for the guest's kernel image. */
extern char _guest_kernel_image[];
extern char _guest_kernel_image_end[];
/* Data for the device tree to be passed to the kernel. */
extern char _guest_dtb_image[];
extern char _guest_dtb_image_end[];
/* Data for the initial RAM disk to be passed to the kernel. */
extern char _guest_initrd_image[];
extern char _guest_initrd_image_end[];

// static void serial_ack(size_t vcpu_id, int irq, void *cookie)
// {
//     /*
//      * For now we by default simply ack the serial IRQ, we have not
//      * come across a case yet where more than this needs to be done.
//      */
//     ZF_LOGI("serial IRQ received\n");
//     vmm_env_t *vmm_e = (vmm_env_t *)cookie;
//     seL4_Error error = seL4_IRQHandler_Ack(vmm_env.serial_irq_handler); // XXX + serial channel
//     ZF_LOGE_IFERR(error, "Failed to ACK serial interrupt");
// }

static void handle_fault(void *arg0, void *arg1, void *arg2)
{
    VMM_PRINT("in handle_fault\n");
    vm_context_t *vm = (vm_context_t *)arg0;
    seL4_CPtr fault_ep = (seL4_CPtr)arg1;
    while (1)
    {
        seL4_MessageInfo_t info = seL4_Recv(fault_ep, NULL);
        VMM_PRINT("fault: %s\n", fault_to_string(seL4_MessageInfo_get_label(info)));
        fault_handle_vcpu_exception(vm);
        vcpu_print_regs(vm->vcpu.cptr);
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

int vm_native_setup(seL4_IRQHandler irq_handler,
                    vka_t *vka,
                    vspace_t *vspace,
                    seL4_CPtr vspace_root,
                    seL4_CPtr asid_pool,
                    simple_t *simple,
                    vm_context_t **ret_vm)
{
    int error;
    vm_context_t *vm = calloc(1, sizeof(vm_context_t));

    /* vm's vspace */
    error = vka_alloc_vspace_root(vka, &vm->vspace_root);
    GOTO_IF_ERR(error, "Failed to allocate vm's page directory");

    error = seL4_ARCH_ASIDPool_Assign(asid_pool, vm->vspace_root.cptr);
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
    GOTO_IF_ERR(error, "Failed to mint vm's cnode to its cspace");

    /* fault endpoint */
    vka_object_t fault_ep = {0};
    error = vka_alloc_endpoint(vka, &fault_ep);
    GOTO_IF_ERR(error, "Failed to allocate vm's fault endpoint");

    vka_cspace_make_path(vka, fault_ep.cptr, &src);
    next_slot.capPtr++;
    error = vka_cnode_copy(&next_slot, &src, seL4_AllRights);
    GOTO_IF_ERR(error, "Failed to copy vm's fault endpoint into its cspace\n");
    vm->fault_ep = next_slot.capPtr;

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
    GOTO_IF_ERR(error, "Failed to configure TCB, seL4_Error: %ld\n", error);

    error = seL4_TCB_SetSchedParams(vm->tcb.cptr, simple_get_tcb(simple), seL4_MaxPrio, seL4_MaxPrio);
    GOTO_IF_ERR(error, "Failed to set TCB priority, seL4_Error: %ld\n", error);

#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugNameThread(vm->tcb.cptr, "vcpu");
#endif

    /* setup serial IRQ and notification */
    vka_object_t ntfn;
    error = vka_alloc_notification(vka, &ntfn);
    GOTO_IF_ERR(error, "Failed to allocate notification");

    cspacepath_t path;
    error = vka_cspace_alloc_path(vka, &path);
    GOTO_IF_ERR(error, "Failed to allocate path for the badged notification");

    cspacepath_t ntfn_path;
    vka_cspace_make_path(vka, ntfn.cptr, &ntfn_path);

    error = vka_cnode_mint(&path, &ntfn_path, seL4_AllRights, 0);
    GOTO_IF_ERR(error, "Failed to mint notification badge");

    error = seL4_IRQHandler_SetNotification(irq_handler, path.capPtr);
    GOTO_IF_ERR(error, "Failed to set IRQ notification");
    // TODO handle IRQ notifications

    reservation_t res;

    /* GIC vCPU interface region */
    VMM_PRINT("Mapping GIC interface - seL4: %lx, linux: %lx\n", GIC_PADDR, LINUX_GIC_PADDR);
    error = vka_alloc_frame_at(vka, seL4_PageBits, (uintptr_t)GIC_PADDR, &vm->gic_vcpu_frame);
    GOTO_IF_ERR(error, "Failed to allocate GIC vCPU frame");

    res = vspace_reserve_range_at(&vm->vspace, (void *)LINUX_GIC_PADDR, BIT(seL4_PageBits), seL4_AllRights, 0);
    seL4_CPtr caps = vm->gic_vcpu_frame.cptr;
    error = vspace_map_pages_at_vaddr(&vm->vspace, &caps, NULL, (void *)LINUX_GIC_PADDR, 1, seL4_PageBits, res);
    GOTO_IF_ERR(error, "Failed to map GIC vCPU region to VM");

#ifdef BOARD_qemu_arm_virt
    /* map in serial device region */
    vm->n_dev_frames = 1;
    vm->dev_frames = calloc(1, sizeof(vka_object_t));
    error = setup_dev_frames(vka, vm, 1, (void *)SERIAL_PADDR, 0);
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
    error = setup_dev_frames(vka, vm, num_pages, (void *)ODROID_BUS1, dev_frame_idx);
    GOTO_IF_ERR(error, "Failed to setup bus region 0x%lx\n", ODROID_BUS1);

    /* BUS 2 */
    dev_frame_idx += num_pages;
    error = setup_dev_frames(vka, vm, num_pages, (void *)ODROID_BUS2, dev_frame_idx);
    GOTO_IF_ERR(error, "Failed to setup bus region 0x%lx\n", ODROID_BUS2);

    /* BUS 3 */
    num_pages = BYTES_TO_4K_PAGES(MiB_TO_BYTES(1));
    dev_frame_idx += num_pages;
    error = setup_dev_frames(vka, vm, num_pages, (void *)ODROID_BUS3, dev_frame_idx);
    GOTO_IF_ERR(error, "Failed to setup bus region 0x%lx\n", ODROID_BUS3);
#endif
    /* guest ram */
    size_t guest_ram_pages = BYTES_TO_SIZE_BITS_PAGES(GUEST_RAM_SIZE, seL4_LargePageBits);
    res = vspace_reserve_range_at(&vm->vspace, (void *)GUEST_RAM_VADDR, GUEST_RAM_SIZE, seL4_AllRights, 1);
    seL4_CPtr *ram_frames_in_guest = calloc(guest_ram_pages, sizeof(seL4_CPtr));
    seL4_CPtr *ram_frames_in_hyp = calloc(guest_ram_pages, sizeof(seL4_CPtr));

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
        ram_frames_in_hyp[i] = dst.capPtr;
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
        ram_frames_in_hyp[i] = dst.capPtr;
    }
#endif

    error = vspace_map_pages_at_vaddr(&vm->vspace, ram_frames_in_guest, NULL, (void *)GUEST_RAM_VADDR, guest_ram_pages, seL4_LargePageBits, res);
    GOTO_IF_ERR(error, "Failed to map guest RAM in VMM's vspace");

    void *guest_ram_curr_vspace = vspace_map_pages(vspace, ram_frames_in_hyp, NULL, seL4_AllRights, guest_ram_pages, seL4_LargePageBits, 1);
    ZF_LOGF_IF(guest_ram_curr_vspace == NULL, "Failed to map guest RAM to HYP's vspace");

    size_t kernel_size = _guest_kernel_image_end - _guest_kernel_image;
    size_t dtb_size = _guest_dtb_image_end - _guest_dtb_image;
    size_t initrd_size = _guest_initrd_image_end - _guest_initrd_image;

    uintptr_t guest_dtb_curr_vspace = (uintptr_t)guest_ram_curr_vspace + ((uintptr_t)GUEST_DTB_VADDR - (uintptr_t)GUEST_RAM_VADDR);
    uintptr_t guest_initrd_curr_vspace = (uintptr_t)guest_ram_curr_vspace + ((uintptr_t)GUEST_INIT_RAM_DISK_VADDR - (uintptr_t)GUEST_RAM_VADDR);
    VMM_PRINT("guest ram: %lx, dtb: %lx initrd: %lx\n", guest_ram_curr_vspace, guest_dtb_curr_vspace, guest_initrd_curr_vspace);

    uintptr_t kernel_pc_curr_vspace = linux_setup_images((uintptr_t)guest_ram_curr_vspace,
                                                         (uintptr_t)_guest_kernel_image,
                                                         kernel_size,
                                                         (uintptr_t)_guest_dtb_image,
                                                         (uintptr_t)guest_dtb_curr_vspace,
                                                         dtb_size,
                                                         (uintptr_t)_guest_initrd_image,
                                                         (uintptr_t)guest_initrd_curr_vspace,
                                                         initrd_size);

    uintptr_t kernel_pc_vm_vspace = (uintptr_t)GUEST_RAM_VADDR + (kernel_pc_curr_vspace - (uintptr_t)guest_ram_curr_vspace);
    VMM_PRINT("kernel_pc_vm_vspace %lx\n", kernel_pc_vm_vspace);

    // bool success = virq_controller_init(GUEST_VCPU_ID);
    // if (!success)
    // {
    //     ZF_LOGE("Failed to initialise emulated interrupt controller\n");
    //     return;
    // }

    // // @ivanv: Note that remove this line causes the VMM to fault if we
    // // actually get the interrupt. This should be avoided by making the VGIC driver more stable.
    // success = virq_register(GUEST_VCPU_ID, SERIAL_IRQ, &serial_ack, (void *)&vmm_env);
    // // /* Just in case there is already an interrupt available to handle, we ack it here. */
    // error = seL4_IRQHandler_Ack(vmm_env.serial_irq_handler); // XXX + serial channel num
    // ZF_LOGE_IFERR(error, "Failed to ACK interrupt");

    // /* Finally start the guest */
    sel4utils_thread_t thread;

    sel4utils_thread_config_t t_cfg = thread_config_default(simple,
                                                            SEL4UTILS_CNODE_SLOT,
                                                            api_make_guard_skip_word(seL4_WordBits - 17),
                                                            seL4_CapNull,
                                                            seL4_MaxPrio);

    error = sel4utils_configure_thread_config(vka, vspace, vspace, t_cfg, &thread);
    GOTO_IF_ERR(error, "Failed to configure fault handling thread\n");

    VMM_PRINT("starting thread\n");
    error = sel4utils_start_thread(&thread, handle_fault, (void *)vm, fault_ep.cptr, 1);
    GOTO_IF_ERR(error, "Failed to start fault handling thread\n");

    guest_start(vm->tcb.cptr, GUEST_VCPU_ID, kernel_pc_vm_vspace,
                (uintptr_t)GUEST_DTB_VADDR, (uintptr_t)GUEST_INIT_RAM_DISK_VADDR);

err_goto:
    if (ret_vm)
    {
        *ret_vm = vm;
    }

    if (ram_frames_in_guest)
    {
        free(ram_frames_in_guest);
    }

    if (ram_frames_in_hyp)
    {
        free(ram_frames_in_hyp);
    }

    if (guest_ram_curr_vspace)
    {
        /* this should also free the copied frame slots */
        vspace_unmap_pages(vspace, guest_ram_curr_vspace, guest_ram_pages, seL4_LargePageBits, vka);
    }

    return error;
}

// void notified(microkit_channel ch) {
//     switch (ch) {
//         case SERIAL_IRQ_CH: {
//             bool success = virq_inject(GUEST_VCPU_ID, SERIAL_IRQ);
//             if (!success) {
//                 LOG_VMM_ERR("IRQ %d dropped on vCPU %d\n", SERIAL_IRQ, GUEST_VCPU_ID);
//             }
//             break;
//         }
//         default:
//             VMM_PRINT("Unexpected channel, ch: 0x%lx\n", ch);
//     }
// }

/*
 * The primary purpose of the VMM after initialisation is to act as a fault-handler,
 * whenever our guest causes an exception, it gets delivered to this entry point for
 * the VMM to handle.
 */
// void fault(microkit_id id, microkit_msginfo msginfo)
// {
//     bool success = fault_handle(id, msginfo);
//     if (success) {
//         /* Now that we have handled the fault successfully, we reply to it so
//          * that the guest can resume execution. */
//         microkit_fault_reply(microkit_msginfo_new(0, 0));
//     }
// }
