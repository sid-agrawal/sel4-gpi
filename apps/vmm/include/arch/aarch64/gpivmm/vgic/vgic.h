/*
 * Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright 2022, UNSW (ABN 57 195 873 179)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdbool.h>
#include <stdint.h>
#include <gpivmm/virq.h>
#include <sel4/sel4.h>

// @ivanv: this should all come from the DTS!
// @ivanv: either this should all be compile time or all runtime
// as in initialising the vgic should depend on the runtime values
#if defined(BOARD_qemu_arm_virt)
#define GIC_V2
#define GIC_DIST_PADDR 0x8000000
#elif defined(BOARD_odroidc4)
#define GIC_V2
#define GIC_DIST_PADDR 0xffc01000
#else
#error Need to define GIC addresses
#endif

#if defined(GIC_V2)
#define GIC_DIST_SIZE 0x1000
#else
#error Unsupported GIC version
#endif

/* every vcpu_id reference here is the ID of the virtual processor - NOT the VMM's assigned ID for the guest */
void vgic_init();
bool fault_handle_vgic_maintenance(vm_context_t *vm, size_t vcpu_id);
bool handle_vgic_dist_fault(vm_context_t *vm, size_t vcpu_id, uint64_t fault_addr, uint64_t fsr, seL4_UserContext *regs);
bool vgic_register_irq(size_t vcpu_id, int virq_num, virq_ack_fn_t ack_fn, void *ack_data);
bool vgic_inject_irq(vm_context_t *vm, size_t vcpu_id, int irq);
