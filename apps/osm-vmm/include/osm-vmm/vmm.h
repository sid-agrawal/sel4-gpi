#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <sel4utils/vspace.h>
#include <simple/simple.h>
#include <sel4gpi/linked_list.h>

// (XXX) Linh: To be removed
#define GUEST_VCPU_ID 0
#define GUEST_NUM_VCPUS 1
#define BASE_VM_TCB_CAP 266

// @ivanv: if we keep using this, make sure that we have a static assert
// that sizeof seL4_UserContext is 0x24
// Note that this is AArch64 specific
#if defined(CONFIG_ARCH_AARCH64)
#define SEL4_USER_CONTEXT_SIZE 0x24
#endif

#define VMM_DBG 1

#ifdef VMM_DBG
#define VMM_PRINT(msg, ...)                                              \
    do                                                                   \
    {                                                                    \
        printf(COLORIZE("%s():\t", WHITE) msg, __func__, ##__VA_ARGS__); \
    } while (0)
#else
#define VMM_PRINT(...)
#endif // VMM_DBG

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
// int vmm_init()

/**
 * @brief starts a new linux guest as a PD.
 * Does not support any other type of guest because we currently don't need to
 *
 * @return int 0 on success, 1 on failure
 */
int new_guest(void);
