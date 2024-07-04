#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <sel4utils/vspace.h>
#include <simple/simple.h>
#include <sel4gpi/linked_list.h>
#include <vmm-common/vmm-common.h>

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

int vm_native_setup(seL4_IRQHandler irq_handler,
                    vka_t *vka, vspace_t *vspace,
                    seL4_CPtr vspace_root, seL4_CPtr asid_pool,
                    simple_t *simple, vm_context_t **ret_vm);
