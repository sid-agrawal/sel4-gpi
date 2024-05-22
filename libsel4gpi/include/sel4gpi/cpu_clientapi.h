#pragma once

#include <sys/types.h>
#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/cpu_component.h>
#include <sel4gpi/gpi_client.h>

/**
 * @brief   Initialize the cpu client.
 *
 * @param server_ep_cap Well known server endpoint cap.
 * @param client_vka client's cka for allocating memory.
 * @param ret_conn client's connection object
 * @return int 0 on success, -1 on failure.
 */
int cpu_component_client_connect(seL4_CPtr server_ep_cap,
                                 seL4_CPtr free_slot,
                                 cpu_client_context_t *ret_conn);

/**
 * @brief   Disconnect the cpu client.
 *
 * @param conn
 * @return int 0 on success, -1 on failure.
 */
int cpu_component_client_disconnect(cpu_client_context_t *conn);

/**
 * @brief starts a execution of a CPU object.
 * Requires an ADS and PD have already been binded to the CPU (via cpu_client_config)
 * Assumes that any necessary state (stack or register arguments, entry points) in the ADS has already been set up for the CPU
 *
 * @param conn client connection object
 * @return int 0 on success, 1 on failure.
 */
int cpu_client_start(cpu_client_context_t *conn);

/**
 * @brief Configures a CPU object by binding it to the given ADS and PD
 *
 * @param cpu the CPU object to configure
 * @param ads the ADS object to bind
 * @param pd the PD to bind
 * @param ipc_buf_mo MO of the the ipc buf for the cpu (OPTIONAL)
 * @param cnode_guard guard configured for the PD's croot
 * @param fault_ep_position w.r.t the PD's cspace, the fault endpoint (OPTIONAL)
 * @param ipc_buf_addr w.r.t the given ADS, address to IPC buf (OPTIONAL)
 * @return int returns 0 on success, 1 on failure
 */
int cpu_client_config(cpu_client_context_t *cpu,
                      ads_client_context_t *ads,
                      pd_client_context_t *pd,
                      mo_client_context_t *ipc_buf_mo,
                      seL4_Word cnode_guard,
                      seL4_CPtr fault_ep_position,
                      seL4_Word ipc_buf_addr);

/**
 * @brief Change just the vspace of the CPU object
 *
 * @param conn client connection object
 * @param ads_conn ads connection object
 * @return int 0 on success, -1 on failure.
 */
int cpu_client_change_vspace(cpu_client_context_t *conn,
                             ads_client_context_t *ads_conn);

/* ======================================= CONVENIENCE FUNCTIONS (NOT PART OF FRAMEWORK) ================================================= */
/**
 * @brief sets the TLS base for a CPU obj. The given TLS base should be w.r.t the CPU's ADS
 * The returned stack pointer is also the one set within the CPU object's metadata
 *
 * @param cpu the target CPU object
 * @param tls_base the TLS base in the target CPU's ADS
 * @param ret_init_stack returns the position of the initial stack pointer (OPTIONAL)
 * @return int returns 0 on success, 1 on failure
 */
int cpu_client_set_tls_stack_top(cpu_client_context_t *cpu, void *tls_base, void **ret_init_stack);
