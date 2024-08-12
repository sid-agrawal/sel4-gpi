#pragma once
#include <vmm-common/vmm.h>

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
    seL4_IRQHandler serial_irq_handler;
    ep_client_context_t vm_fault_ep;
    uint32_t guest_id_counter; ///< the next free VM ID
    vm_context_t *guests[MAX_GUEST_COUNT];
} vmon_context_t;

/**
 * @brief starts a new linux guest as a PD.
 * Does not support any other type of guest because we currently don't need to
 *
 * @return int 0 on success, 1 on failure
 */
uint32_t osm_new_guest(void);
