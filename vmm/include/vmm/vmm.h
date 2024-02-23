#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <sel4utils/vspace.h>
#include <simple/simple.h>

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

typedef struct vmm_env
{
    vka_t *vka;
    vspace_t *vspace;
    seL4_IRQHandler serial_irq_handler;
    vka_object_t vcpu;

    // TODO: only handles 1 for now, generalize to more vm's
    /* vm objects */
    vka_object_t vm_vspace_root;
    vspace_t vm_vspace;
    sel4utils_alloc_data_t vm_vspace_data;
    vka_object_t vm_tcb;
    vka_object_t vm_sched_ctxt;
    vka_object_t vm_fault_ep;
    vka_object_t vm_cspace;
    vka_object_t serial_dev_frame[3]; // odroid requires three different regions for serial io
    vka_object_t gic_vcpu_frame;

} vmm_env_t;

typedef struct vm_data
{

} vm_data_t;

void vm_init(vmm_env_t *vmm_e);
vmm_env_t *vm_setup(seL4_IRQHandler irq_handler,
                    vka_t *vka, vspace_t *vspace,
                    seL4_CPtr vspace_root, seL4_CPtr asid_pool, simple_t *simple);
