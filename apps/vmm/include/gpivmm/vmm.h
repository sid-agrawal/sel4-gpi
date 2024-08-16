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
#include <utils/attribute.h>
#include <stdbool.h>

#define GUEST_VCPU_ID 0   // ID of the first vCPU, defined for convenience since only one vCPU is ever started
#define GUEST_NUM_VCPUS 1 // number of virtual processors used by the guest
#define MAX_GUEST_COUNT 5 // maximum number of guests supported by the VMM
#define MAX_VM_NAME_LEN 32

#define SERIAL_IRQ_BIT 1             // Indicator bit on notification for serial IRQs
#define FAULT_BADGE_FLAG (1UL << 63) // indicator flag on fault EP badge

// On QEMU, there is a special reserved region for VM guest RAM
#define QEMU_VM_RESERVE_PADDR 0x40000000

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

// forward declare VM and VMM contexts, these are defined in implementation specific headers
struct _vm_context;
typedef struct _vm_context vm_context_t;

struct _vmon_context;
typedef struct _vmon_context vmon_context_t;

/**
 * @brief Function for copying a kernel image into guest RAM at the given offset.
 * Returns the entry point for the guest in its own ADS.
 */
typedef uintptr_t (*copy_kernel_image_fn_t)(uintptr_t guest_ram_curr_vspace, const char *kernel_image_name, uint64_t offset);

// Device addresses

/*
 * As this is just an example, for simplicity we just make the size of the
 * guest's "RAM" the same for all platforms. For just booting Linux with a
 * simple user-space, 0x10000000 bytes (256MB) is plenty.
 */
#define GUEST_RAM_SIZE 0x10000000

#if defined(BOARD_qemu_arm_virt)
#define GUEST_RAM_PADDR 0x40000000
#define GUEST_RAM_VADDR 0x40000000
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4d700000
#define SERIAL_PADDR 0x9000000
#define GIC_PADDR 0x8040000
#define LINUX_GIC_PADDR 0x8010000
#elif defined(BOARD_odroidc4)
#define GUEST_RAM_VADDR 0x20000000
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#define ODROID_BUS1 0xff600000
#define ODROID_BUS2 0xff800000
#define ODROID_BUS3 0xffd00000
#define GIC_PADDR 0xffc06000
#define LINUX_GIC_PADDR 0xffc02000
#else
#error Need to define guest kernel image address and DTB address
#endif

#if defined(BOARD_qemu_arm_virt)
#define SERIAL_IRQ 33
#elif defined(BOARD_odroidc2_hyp) || defined(BOARD_odroidc4)
#define SERIAL_IRQ 225
#else
#error Need to define serial interrupt
#endif

/* guest specific image names and offsets */
#define HELLO_KERNEL_NAME "hellokernel.bin"
#define HELLO_KERNEL_PC_OFFSET 0x1000000
#define LINUX_KERNEL_NAME "linux"
#define LINUX_DTB_NAME "linux.dtb"
#define LINUX_INITRD_NAME "rootfs.cpio.gz"

// ========================== Generic Helpers ==========================

typedef void (*virq_ack_fn_t)(vm_context_t *vm, int irq, void *cookie);

/**
 * @brief Initializes VIRQ handling for a VCPU and registers an ACK function
 * for serial device interrupts
 *
 * @param vcpu_id virtual processor ID
 * @param serial_ack_fn the ACK function for serial interrupts
 * @return int 0 on success, other on failure
 */
int vmm_init_virq(size_t vcpu_id, virq_ack_fn_t serial_ack_fn);

/**
 * @brief Copies the linux kernel image into guest RAM. Additionally copies linux's DTB and init ramdisk,
 * and performs sanity checks on image size and header magic values
 *
 * @param guest_ram_curr_vspace vaddr of guest RAM in the current ADS
 * @param kernel_image_name UNUSED
 * @param offset UNUSED
 * @return uintptr_t entry point of the kernel in the guest's ADS
 */
uintptr_t linux_copy_kernel_image(uintptr_t guest_ram_curr_vspace,
                                  UNUSED const char *kernel_image_name,
                                  UNUSED uint64_t offset);

/**
 * @brief Copies an arbitrary kernel image into guest RAM without any additional setup
 *
 * @param guest_ram_curr_vspace vaddr of guest RAM in the current ADS
 * @param kernel_image_name name of the image in the CPIO archive
 * @param offset offset in guest RAM at which to copy the image
 * @return uintptr_t entry point of the kernel in the guest's ADS
 */
uintptr_t generic_copy_kernel_image(uintptr_t guest_ram_curr_vspace,
                                    const char *kernel_image_name, uint64_t offset);

// ========== Helpers that each implementation-type specifies ==========

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

/**
 * @brief Injects an IRQ into a VM
 *
 * @param vm the VM context
 * @param virq virtual IRQ ID
 * @param prio priority of IRQ to inject
 * @param group IRQ group
 * @param idx VGIC list register
 * @return int 0 on success, other on failure
 */
int vm_inject_irq(vm_context_t *vm, int virq, int prio, int group, int idx);

/**
 * @brief Acks a VPPI for a VM
 *
 * @param vm the VM context
 * @param irq IRQ ID to ACK
 * @return int int 0 on success, other on failure
 */
int vm_ack_vppi(vm_context_t *vm, uint64_t irq);
