#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <sel4utils/vspace.h>
#include <simple/simple.h>
#include <sel4gpi/linked_list.h>

#define GUEST_VCPU_ID 0
#define GUEST_NUM_VCPUS 1
#define BASE_VM_TCB_CAP 266

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// @ivanv: if we keep using this, make sure that we have a static assert
// that sizeof seL4_UserContext is 0x24
// Note that this is AArch64 specific
#if defined(CONFIG_ARCH_AARCH64)
#define SEL4_USER_CONTEXT_SIZE 0x24
#endif

// typedef struct _hyp_native_context
// {
//     seL4_CPtr
// } hyp_native_context_t;

typedef struct _vm_native_context
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
} vm_native_context_t;

int vm_native_setup(seL4_IRQHandler irq_handler,
                    vka_t *vka, vspace_t *vspace,
                    seL4_CPtr vspace_root, seL4_CPtr asid_pool,
                    simple_t *simple, vm_native_context_t **ret_vm);
