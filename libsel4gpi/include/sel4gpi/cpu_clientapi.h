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
#include <sel4gpi/ads_clientapi.h>

typedef struct _cpu_client_context
{
   cspacepath_t badged_server_ep_cspath;
   // cspacepath_t public_server_ep_cspath;
} cpu_client_context_t;

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
 * @brief
 *
 * @param conn client connection object
 * @param entry_fn the address of the function to be called when the thread starts.
 * @param initial_stack address to the top of the stack wrt CPU's vspace (OPTIONAL)
 * @param arg0 arg0 to entry function
 * @return int 0 on success, -1 on failure.
 */
int cpu_client_start(cpu_client_context_t *conn,
                     sel4utils_thread_entry_fn entry_fn,
                     seL4_Word initial_stack,
                     seL4_Word arg0);

/**
 * @brief Configure the cpu oject.
 *
 * @param conn client connection object
 * @param ads_conn ads connection object
 * @param cspace_root cspace root for the cpu object.
 * @param fault_ep W.r.t of the CPU's cspace, the fault endpoint.
 * @return int 0 on success, -1 on failure.
 */

/**
 * @brief Configure the cpu oject.
 *
 * @param conn client connection object
 * @param ads_conn ads connection object
 * @param ipc_buf_mo MO of the the ipc buf for the cpu (OPTIONAL)
 * @param cspace_root cspace root for the cpu object.
 * @param cnode_guard guard configured for the cspace root
 * @param fault_ep_position W.r.t of the CPU's cspace, the fault endpoint (OPTIONAL)
 * @param ipc_buf_addr address to IPC buf (in CPU's vspace, OPTIONAL)
 * @param stack_addr address to stack (in CPU's vspace, OPTIONAL)
 * @return int
 */
int cpu_client_config(cpu_client_context_t *conn,
                      ads_client_context_t *ads_conn,
                      mo_client_context_t *ipc_buf_mo,
                      seL4_CPtr cspace_root,
                      seL4_Word cnode_guard,
                      seL4_CPtr fault_ep_position,
                      seL4_Word ipc_buf_addr,
                      seL4_Word stack_addr);

/**
 * @brief Change just the vspace of the CPU object
 *
 * @param conn client connection object
 * @param ads_conn ads connection object
 * @return int 0 on success, -1 on failure.
 */
int cpu_client_change_vspace(cpu_client_context_t *conn,
                             ads_client_context_t *ads_conn);