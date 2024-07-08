#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <sel4utils/vspace.h>
#include <simple/simple.h>
#include <sel4gpi/linked_list.h>
#include <vmm-common/vmm_common.h>

/**
 * @brief Holds information about a VM for a
 * seL4-test VMM implementation
 */
typedef struct _vm_context
{
    vka_object_t vcpu;
    vka_object_t vspace_root;
    vspace_t vspace;
    sel4utils_alloc_data_t vspace_data;
    vka_object_t tcb;
    vka_object_t sched_ctxt;
    seL4_CPtr fault_ep;
    vka_object_t cspace;
    vka_object_t *dev_frames;
    size_t n_dev_frames;
    vka_object_t gic_vcpu_frame;
} vm_context_t;

typedef struct _vmon_context
{
    vka_t *vka;
    vspace_t *vspace;
    seL4_CPtr asid_pool;
    simple_t *simple;
    seL4_CPtr tcb;
    seL4_IRQHandler serial_irq_handler;
    vka_object_t irq_ntfn;     ///< unbadged notification for handling any interrupt
    seL4_CPtr serial_irq_ntfn; ///< badged notification for the serial interrupt
    vka_object_t vm_fault_ep;
    uint32_t guest_id_counter; ///< the next free VM ID
    vm_context_t *guests[GUEST_NUM_VCPUS];
} vmon_context_t;

/**
 * @brief initializes the VMM, creates two threads for handling VM faults and interrupts
 * The only interrupts currently handled are for the UART device
 *
 * @param irq_handler handler for serial device
 * @param vka a VKA for allocations
 * @param vspace the current vspace
 * @param asid_pool an ASID pool for new vspace allocations
 * @param simple the simple object for getting root task caps
 * @param tcb the current TCB
 * @param fault_ep OPTIONAL: an endpoint for VMM faults
 * @return int 0 on success, other on error
 */
int sel4test_vmm_init(seL4_IRQHandler irq_handler,
                      vka_t *vka,
                      vspace_t *vspace,
                      seL4_CPtr asid_pool,
                      simple_t *simple,
                      seL4_CPtr tcb,
                      seL4_CPtr fault_ep);

/**
 * @brief creates a VM for a linux guest and starts it
 *
 * @return int the VM ID of the guest, 0 if an error occurred
 */
uint32_t sel4test_new_guest(void);
