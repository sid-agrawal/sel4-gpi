/**
 * @file vmm_common.h
 * @author Linh Pham (phamhlinh01@gmail.com)
 * @brief Shared definitions between OSmosis and sel4test VMM implementations
 * @version 0.1
 * @date 2024-07-10
 *
 * @copyright Copyright (c) 2024
 *
 */
#pragma once
#include <sel4gpi/debug.h>
#include <utils/ansi_color.h>
#define GUEST_VCPU_ID 0   //
#define GUEST_NUM_VCPUS 1 // number of virtual processors used by the guest
#define MAX_GUEST_COUNT 5 // maximum number of guests supported by the VMM
#define MAX_VM_NAME_LEN 32

// @ivanv: if we keep using this, make sure that we have a static assert
// that sizeof seL4_UserContext is 0x24
// Note that this is AArch64 specific
#if defined(CONFIG_ARCH_AARCH64)
#define SEL4_USER_CONTEXT_SIZE 0x24
#endif

#define VM_CNODE_BITS 17
#define VMON_CNODE_BITS 17

#define VMM_DBG 1

#define VMM_PRINT(msg, ...) \
    OSDB_LVL_PRINT(OSDB_INFO, VMM_DBG, COLORIZE("[VMM] %s():\t", WHITE) msg, __func__, ##__VA_ARGS__);

#define VMM_PRINTERR(msg, ...) \
    OSDB_LVL_PRINT(OSDB_ERROR, VMM_DBG, COLORIZE("[VMM] %s():\t", RED) msg, __func__, ##__VA_ARGS__);

#define VMM_PRINTV(msg, ...) \
    OSDB_LVL_PRINT(OSDB_VERBOSE, VMM_DBG, COLORIZE("[VMM] %s():\t", WHITE) msg, __func__, ##__VA_ARGS__);
