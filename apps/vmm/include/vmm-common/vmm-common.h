#pragma once
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
