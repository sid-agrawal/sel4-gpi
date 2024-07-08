#pragma once
#define GUEST_VCPU_ID 1 // TODO: fix this
#define GUEST_NUM_VCPUS 5

// @ivanv: if we keep using this, make sure that we have a static assert
// that sizeof seL4_UserContext is 0x24
// Note that this is AArch64 specific
#if defined(CONFIG_ARCH_AARCH64)
#define SEL4_USER_CONTEXT_SIZE 0x24
#endif

#define VM_CNODE_BITS 17
#define VMON_CNODE_BITS 17

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
