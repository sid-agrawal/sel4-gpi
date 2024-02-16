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
#include "vcpu.h"
#include "vmm/vmm.h"
#include <vka/vka.h>
#include <sel4/sel4.h>
#include <vka/object.h>
#include <vka/arch/object.h>
#include <vka/capops.h>
#include <utils/zf_log.h>
#include <sel4utils/sel4_zf_logif.h>
#include <vspace/vspace.h>
#include <sel4utils/vspace.h>
#include <sel4utils/vspace_internal.h>
#include <sel4utils/api.h>


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
#define GUEST_RAM_SIZE 0x10000000 // 128 MB // expected by linux's dts

#if defined(BOARD_qemu_arm_virt)
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

#define VM_CNODE_BITS 7

/* Data for the guest's kernel image. */
extern char _guest_kernel_image[];
extern char _guest_kernel_image_end[];
/* Data for the device tree to be passed to the kernel. */
extern char _guest_dtb_image[];
extern char _guest_dtb_image_end[];
/* Data for the initial RAM disk to be passed to the kernel. */
extern char _guest_initrd_image[];
extern char _guest_initrd_image_end[];

// static vmm_env_t vmm_env;

static void serial_ack(size_t vcpu_id, int irq, void *cookie) {
    /*
     * For now we by default simply ack the serial IRQ, we have not
     * come across a case yet where more than this needs to be done.
     */
    // microkit_irq_ack(SERIAL_IRQ_CH);
    ZF_LOGI("serial IRQ received\n");
    vmm_env_t *vmm_e = (vmm_env_t *) cookie;
    seL4_Error error = seL4_IRQHandler_Ack(vmm_e->serial_irq_handler); // XXX + serial channel
    ZF_LOGE_IFERR(error, "Failed to ACK serial interrupt");
}

vmm_env_t *vm_setup(seL4_IRQHandler irq_handler, vka_t *vka, vspace_t *vspace, seL4_CPtr vspace_root, seL4_CPtr asid_pool, simple_t *simple) {
    int error;
    seL4_Error serr;
    vmm_env_t *vmm_e = malloc(sizeof(vmm_env_t));
    vmm_e->vka = vka;
    vmm_e->vspace = vspace;
    vmm_e->serial_irq_handler = irq_handler;

    /* vm's vspace */
    error = vka_alloc_vspace_root(vka, &vmm_e->vm_vspace_root);
    ZF_LOGF_IF(error, "Failed to allocate vm's page directory");

    serr = seL4_ARCH_ASIDPool_Assign(asid_pool, vmm_e->vm_vspace_root.cptr);
    ZF_LOGF_IFERR(serr, "Failed to assign vm's vspace to an asid pool");

    error = sel4utils_get_empty_vspace(vspace, &vmm_e->vm_vspace, &vmm_e->vm_vspace_data, vka, vmm_e->vm_vspace_root.cptr, NULL, NULL);
    ZF_LOGF_IF(error, "Failed to make vm's vspace");

    /* fault endpoint */
    error = vka_alloc_endpoint(vka, &vmm_e->vm_fault_ep);
    ZF_LOGF_IF(error, "Failed to allocate vm's fault endpoint");

    /* vm's cspace */
    error = vka_alloc_cnode_object(vka, VM_CNODE_BITS, &vmm_e->vm_cspace);
    ZF_LOGF_IF(error, "Failed to allocate vm's cspace");
    int curr_slot = 1;

    cspacepath_t src;
    vka_cspace_make_path(vka, vmm_e->vm_cspace.cptr, &src);

    cspacepath_t dest = {
        .capPtr = curr_slot,
        .root = vmm_e->vm_cspace.cptr,
        .capDepth = VM_CNODE_BITS
    };
    
    error = vka_cnode_mint(&dest, &src, seL4_AllRights, 1); // TODO create badges properly
    ZF_LOGF_IF(error, "Failed to mint vm's cnode to its cspace");

    // TODO: copy fault EP to VM's cspace

    vka_cspace_make_path(vka, vmm_e->vm_vspace_root.cptr, &src);
    dest.capPtr++;
    error = vka_cnode_copy(&dest, &src, seL4_AllRights);
    ZF_LOGF_IF(error, "Failed to copy vm's page directory to its cspace");

    /* vcpu */
    error = vka_alloc_vcpu(vka, &vmm_e->vcpu);
    ZF_LOGF_IF(error, "Failed to allocate a vcpu");

    /* tcb */
    error = vka_alloc_tcb(vka, &vmm_e->vm_tcb);
    ZF_LOGF_IF(error, "Failed to make vm's TCB");

    /* scheduler context for MCS kernel */
    // should also set scheduling parameters when this is enabled
    // error = vka_alloc_sched_context(vmm_e->vka, &vmm_e->vm_sched_ctxt);
    // ZF_LOGF_IF(error, "Failed to make vm's scheduler context");

    // XXX do we need a cnode guard?
    serr = api_tcb_set_space(vmm_e->vm_tcb.cptr, vmm_e->vm_fault_ep.cptr, vmm_e->vm_cspace.cptr, 0, vmm_e->vm_vspace_root.cptr, 0);
    ZF_LOGF_IFERR(serr, "Failed to set TCB cspace and vspace");

    serr = seL4_ARM_VCPU_SetTCB(vmm_e->vcpu.cptr, vmm_e->vm_tcb.cptr);
    ZF_LOGF_IFERR(serr, "Failed to bind TCB to VCPU");

    serr = api_tcb_set_sched_params(vmm_e->vm_tcb.cptr, simple_get_tcb(simple), seL4_MaxPrio, seL4_MaxPrio, seL4_CapNull, seL4_CapNull);

    seL4_DebugNameThread(vmm_e->vm_tcb.cptr, "vcpu");

    /* setup serial IRQ and notification */
    vka_object_t ntfn;
    error = vka_alloc_notification(vka, &ntfn);
    ZF_LOGF_IF(error, "Failed to allocate notification");

    cspacepath_t path;
    error = vka_cspace_alloc_path(vka, &path);
    ZF_LOGF_IF(error, "Failed to allocate path for the badged notification");
    
    cspacepath_t ntfn_path;
    vka_cspace_make_path(vka, ntfn.cptr, &ntfn_path);

    error = vka_cnode_mint(&path, &ntfn_path, seL4_AllRights, 2); // TODO create badges properly
    ZF_LOGF_IF(error, "Failed to mint notification badge");

    serr = seL4_IRQHandler_SetNotification(irq_handler, path.capPtr);
    ZF_LOGF_IFERR(serr, "Failed to set IRQ notification");

    #ifdef BOARD_qemu_arm_virt
    /* map in serial device region */
    error = vka_alloc_frame_at(vka, seL4_PageBits, (uintptr_t) SERIAL_PADDR, &vmm_e->serial_dev_frame[0]);
    ZF_LOGF_IF(error, "Failed to allocate serial device frame");

    reservation_t res = vspace_reserve_range_at(&vmm_e->vm_vspace, (void *) SERIAL_PADDR, BIT(seL4_PageBits), seL4_AllRights, 0);
    seL4_CPtr caps[1] = { vmm_e->serial_dev_frame[0].cptr };
    error = vspace_map_pages_at_vaddr(&vmm_e->vm_vspace, caps, NULL, (void *) SERIAL_PADDR, 1, seL4_PageBits, res);
    ZF_LOGF_IF(error, "Failed to map serial device to VM");

    /* GIC vCPU interface region */
    error = vka_alloc_frame_at(vka, seL4_PageBits, (uintptr_t) GIC_PADDR, &vmm_e->gic_vcpu_frame);
    ZF_LOGF_IF(error, "Failed to allocate GIC vCPU frame");

    res = vspace_reserve_range_at(&vmm_e->vm_vspace, (void *) LINUX_GIC_PADDR, BIT(seL4_PageBits), seL4_AllRights, 0);
    caps[0] = vmm_e->gic_vcpu_frame.cptr;
    error = vspace_map_pages_at_vaddr(&vmm_e->vm_vspace, caps, NULL, (void *) LINUX_GIC_PADDR, 1, seL4_PageBits, res);
    ZF_LOGF_IF(error, "Failed to map GIC vCPU region to VM");
    #endif

    #ifdef BOARD_odroidc4
    error = vka_alloc_frame_at(vka, seL4_PageBits, (uintptr_t) ODROID_BUS1, &vmm_e->serial_dev_frame[0]);
    ZF_LOGF_IF(error, "Failed to allocate odroid bus 1 frame");

    error = vka_alloc_frame_at(vka, seL4_PageBits, (uintptr_t) ODROID_BUS2, &vmm_e->serial_dev_frame[1]);
    ZF_LOGF_IF(error, "Failed to allocate odroid bus 2 frame");

    error = vka_alloc_frame_at(vka, seL4_PageBits, (uintptr_t) ODROID_BUS3, &vmm_e->serial_dev_frame[2]);
    ZF_LOGF_IF(error, "Failed to allocate odroid bus 3 frame");

    res = vspace_reserve_range_at(&vmm_e->vm_vspace, (void *) ODROID_BUS1, BIT(seL4_PageBits), seL4_AllRights, 0);
    caps[0] = vmm_e->serial_dev_frame[0].cptr;
    error = vspace_map_pages_at_vaddr(&vmm_e->vm_vspace, caps, NULL, (void *) ODROID_BUS1, 1, seL4_PageBits, res);
    ZF_LOGF_IF(error, "Failed to map odroid bus 1 region to VM");

    res = vspace_reserve_range_at(&vmm_e->vm_vspace, (void *) ODROID_BUS2, BIT(seL4_PageBits), seL4_AllRights, 0);
    caps[0] = vmm_e->serial_dev_frame[1].cptr;
    error = vspace_map_pages_at_vaddr(&vmm_e->vm_vspace, caps, NULL, (void *) ODROID_BUS2, 1, seL4_PageBits, res);
    ZF_LOGF_IF(error, "Failed to map odroid bus 2 region to VM");

    res = vspace_reserve_range_at(&vmm_e->vm_vspace, (void *) ODROID_BUS3, BIT(seL4_PageBits), seL4_AllRights, 0);
    caps[0] = vmm_e->serial_dev_frame[2].cptr;
    error = vspace_map_pages_at_vaddr(&vmm_e->vm_vspace, caps, NULL, (void *) ODROID_BUS3, 1, seL4_PageBits, res);
    ZF_LOGF_IF(error, "Failed to map odroid bus 3 region to VM");
    #endif

    /* guest ram */
    size_t num_pages = DIV_ROUND_UP(GUEST_RAM_SIZE, BIT(seL4_LargePageBits));
    
    reservation_t vmm_res = vspace_reserve_range_at(vmm_e->vspace, (void *) GUEST_RAM_VADDR, GUEST_RAM_SIZE, seL4_AllRights, 1);
    error = vspace_new_pages_at_vaddr(vmm_e->vspace, (void *) GUEST_RAM_VADDR, DIV_ROUND_UP(GUEST_RAM_SIZE, BIT(seL4_LargePageBits)), seL4_LargePageBits, vmm_res);
    ZF_LOGF_IF(error, "Failed to allocate guest RAM in VMM's vspace");

    error = sel4utils_share_mem_at_vaddr(vmm_e->vspace, &vmm_e->vm_vspace, (void *) GUEST_RAM_VADDR, num_pages, seL4_LargePageBits, (void *) GUEST_RAM_VADDR, vmm_res);
    ZF_LOGF_IF(error, "Failed to copy guest RAM to VM's vspace");

    return vmm_e;
}

void vm_init(vmm_env_t *vmm_e) {
    int error;
    /* Initialise the VMM, the VCPU(s), and start the guest */
    /* Place all the binaries in the right locations before starting the guest */
    size_t kernel_size = _guest_kernel_image_end - _guest_kernel_image;
    size_t dtb_size = _guest_dtb_image_end - _guest_dtb_image;
    size_t initrd_size = _guest_initrd_image_end - _guest_initrd_image;
    
    uintptr_t kernel_pc = linux_setup_images((uintptr_t) GUEST_RAM_VADDR,
                                      (uintptr_t) _guest_kernel_image,
                                      kernel_size,
                                      (uintptr_t) _guest_dtb_image,
                                      (uintptr_t) GUEST_DTB_VADDR,
                                      dtb_size,
                                      (uintptr_t) _guest_initrd_image,
                                      (uintptr_t) GUEST_INIT_RAM_DISK_VADDR,
                                      initrd_size
                                      );
    if (!kernel_pc) {
        ZF_LOGE("Failed to initialise guest images\n");
        return;
    }
    /* Initialise the virtual GIC driver */
    bool success = virq_controller_init(GUEST_VCPU_ID);
    if (!success) {
        ZF_LOGE("Failed to initialise emulated interrupt controller\n");
        return;
    }
    // @ivanv: Note that remove this line causes the VMM to fault if we
    // actually get the interrupt. This should be avoided by making the VGIC driver more stable.
    success = virq_register(GUEST_VCPU_ID, SERIAL_IRQ, &serial_ack, (void *) vmm_e);
    // /* Just in case there is already an interrupt available to handle, we ack it here. */
    // // microkit_irq_ack(SERIAL_IRQ_CH);
    error = seL4_IRQHandler_Ack(vmm_e->serial_irq_handler); // XXX + serial channel num
    ZF_LOGE_IFERR(error, "Failed to ACK interrupt");
    // /* Finally start the guest */
    guest_start(vmm_e, GUEST_VCPU_ID, kernel_pc, (uintptr_t) GUEST_DTB_VADDR, (uintptr_t) GUEST_INIT_RAM_DISK_VADDR);
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
//             printf("Unexpected channel, ch: 0x%lx\n", ch);
//     }
// }

/*
 * The primary purpose of the VMM after initialisation is to act as a fault-handler,
 * whenever our guest causes an exception, it gets delivered to this entry point for
 * the VMM to handle.
 */
// void fault(microkit_id id, microkit_msginfo msginfo) {
//     bool success = fault_handle(id, msginfo);
//     if (success) {
//         /* Now that we have handled the fault successfully, we reply to it so
//          * that the guest can resume execution. */
//         microkit_fault_reply(microkit_msginfo_new(0, 0));
//     }
// }
