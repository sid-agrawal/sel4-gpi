#pragma once
#include <gpivmm/vmm.h>

/**
 * @brief Holds information about a VM for a
 * seL4-test VMM implementation
 */
struct _vm_context
{
    uint32_t id;                 ///< an ID assigned by the VMM
    sel4gpi_runnable_t runnable; ///< the PD, ADS, CPU of the VM
};

typedef struct _vmon_context
{
    ep_client_context_t vm_fault_ep;          ///< Listening endpoint for VM faults
    sel4gpi_runnable_t fault_thread_runnable; ///< Runnable for fault handling thread
    uint32_t guest_id_counter;                ///< the next free VM ID
} vmon_context_t;

/**
 * @brief initializes the VMM
 * - allocates an EP for VM fault handling
 * - starts the thread for fault and IRQ handling
 * - binds the VMM to the serial IRQ
 *
 * @return int 0 on success, other on error
 */
int osm_vmm_init(void);

/**
 * @brief starts a new guest from the given binary image as a PD.
 * Currently only performs additional RAM setup (DTB and init ramdisk) for Linux guests.
 * Other guests simply get a copy of their kernel_image in RAM
 *
 * @param kernel_image name of kernel image
 * @return int 0 on success, 1 on failure
 */
uint32_t osm_new_guest(const char *kernel_image);
