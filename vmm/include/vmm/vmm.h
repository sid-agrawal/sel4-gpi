#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vka/vka.h>
#include <vspace/vspace.h>

#define GUEST_VCPU_ID 0
#define GUEST_NUM_VCPUS 1
#define BASE_VM_TCB_CAP 266

#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

// @ivanv: if we keep using this, make sure that we have a static assert
// that sizeof seL4_UserContext is 0x24
// Note that this is AArch64 specific
#if defined(CONFIG_ARCH_AARCH64)
    #define SEL4_USER_CONTEXT_SIZE 0x24
#endif

void vm_init(seL4_IRQHandler irq_handler, vka_t *vka, vspace_t *vspace);
