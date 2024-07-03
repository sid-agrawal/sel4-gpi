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
#include <sel4gpi/resource_component_utils.h>

/** @file APIs for managing and interacting with the serial server thread.
 *
 * Defines the constants for the protocol, messages, and server-side state, as
 * well as the entry point and back-end routines for the server's API.
 *
 * All vka_t, vspace_t and simple_t instances that are supplied to this library
 * by the developer must persist and be functional for the lifetime of the
 * server thread.
 */

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

int forge_cpu_cap_from_tcb(sel4utils_process_t *proc, vka_t *vka, uint32_t client_id, seL4_CPtr *cap_ret, uint32_t *id_ret);

/**
 * Stops a CPU's TCB
 * 
 * @param cpu_id ID of the cpu to stop
 * @return 0 on success, error otherwise
*/
int cpu_component_stop(uint32_t cpu_id);