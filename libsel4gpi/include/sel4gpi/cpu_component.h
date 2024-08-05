#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/cpu_obj.h>
#include <sel4gpi/ads_obj.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/resource_component_utils.h>
#include <cpu_component_rpc.pb.h>

/** @file APIs for managing and interacting with the serial server thread.
 *
 * Defines the constants for the protocol, messages, and server-side state, as
 * well as the entry point and back-end routines for the server's API.
 *
 * All vka_t, vspace_t and simple_t instances that are supplied to this library
 * by the developer must persist and be functional for the lifetime of the
 * server thread.
 */

#define CPU_RPC_MAGIC 0x435055
#define CPUSERVS "CPUServ Component: "
#define CPUSERVC "CPUServ Client   : "

/* Per-client context maintained by the server. */
typedef struct _cpu_component_registry_entry
{
    resource_registry_node_t gen;
    cpu_t cpu;
} cpu_component_registry_entry_t;

/**
 * To initialize the cpu component at the beginning of execution
 */
int cpu_component_initialize(vka_t *server_vka,
                             vspace_t *server_vspace,
                             vka_object_t server_ep_obj);

/* Global server instance accessor functions. */
resource_component_context_t *get_cpu_component(void);

/**
 * Stops a CPU's TCB
 *
 * @param cpu_id ID of the cpu to stop
 * @return 0 on success, error otherwise
 */
int cpu_component_stop(gpi_obj_id_t cpu_id);

/**
 * Allocate a CPU from the root task
 *
 * @param client_id the PD id of the client requesting the CPU
 * @param ret_cpu returns the created CPU
 * @param ret_cap returns the slot of the new CPU, in the client (or NULL, to make no cap)
 */
int cpu_component_allocate(gpi_obj_id_t client_id, cpu_t **ret_cpu, seL4_CPtr *ret_cap);

/**
 * @brief Configures a CPU object by binding it to the given ADS and PD
 * NOTE: the fault endpoint's refcount is not increased here for a few reasons:
 *       1) we can't send more than 3 unwrapped caps, and don't have a way to indicate which
 *          EP within the EP component's metadata to refcount -> this could be solved
 *          with other workarounds
 *       2) if the refcount to this EP has reached 0, all PDs holding it have exited,
 *          and nothing will be able to reference it anymore. If the CPU object binded
 *          to this EP is still lying around, it will have to be reconfigured regardless
 *          of whether the EP still exists or not.
 *
 * @param cpu the CPU object to configure
 * @param ads the ADS object to bind
 * @param pd the PD to bind
 * @param cnode_guard guard configured for the PD's croot
 * @param fault_ep_position w.r.t the PD's cspace, the fault endpoint (OPTIONAL)
 * @param ipc_buf_mo MO of the the ipc buf for the cpu (OPTIONAL)
 * @param ipc_buf_addr w.r.t the given ADS, address to IPC buf (OPTIONAL)
 * @return int returns 0 on success, 1 on failure
 */
int cpu_component_configure(cpu_t *cpu,
                            ads_t *ads,
                            pd_t *pd,
                            uint64_t cnode_guard,
                            seL4_CPtr fault_ep,
                            mo_t *ipc_buf_mo,
                            void *ipc_buf_addr);