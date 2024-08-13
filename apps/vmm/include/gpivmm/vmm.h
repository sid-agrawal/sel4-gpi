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
#include <stdbool.h>

#define GUEST_VCPU_ID 0   // ID of the first vCPU, defined for convenience since only one vCPU is ever started
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

struct _vm_context;
typedef struct _vm_context vm_context_t;

// ========== Helpers that each implementation-type specifies ==========

/**
 * @brief Retrieves the VCPU object from a VM context
 *
 * @param vm the VM context
 * @return seL4_CPtr slot holding the VCPU object
 */
seL4_CPtr vm_get_vcpu(vm_context_t *vm);

/**
 * @brief pauses execution of a VM
 *
 * @param vm the VM context
 */
void vm_suspend(vm_context_t *vm);

/**
 * @brief write to a VM's TCB registers
 *
 * @param vm the VM context
 * @param resume whether to resume the TCB after writing
 * @param regs content of registers
 * @param num_regs the number of registers to write
 * @return int 0 on success, other on error
 */
int vm_write_registers(vm_context_t *vm, bool resume, seL4_UserContext *regs, size_t num_regs);

/**
 * @brief reads a VM's TCB registers
 *
 * @param vm the VM context
 * @param suspend whether to suspend the TCB after reading
 * @param regs content of registers
 * @param num_regs the number of registers to read
 * @return int 0 on success, other on error
 */
int vm_read_registers(vm_context_t *vm, bool suspend, seL4_UserContext *regs, size_t num_regs);

/**
 * @brief prints a VM's TCB register contents
 *
 * @param vm the VM context
 */
void vm_dump_registers(vm_context_t *vm);

/**
 * @brief prints a VM's VCPU register contents
 *
 * @param vm the VM context
 */
void vm_dump_vcpu_registers(vm_context_t *vm);

/**
 * @brief Gets the ID of a VM
 *
 * @param vm the VM context
 * @return uint32_t the ID of the VM, will never be 0
 */
uint32_t vm_get_id(vm_context_t *vm);
