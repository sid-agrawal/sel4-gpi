#pragma once

#include <stdint.h>
#include <sel4/sel4.h>

#include <sel4gpi/resource_component_utils.h>
#include <sel4gpi/resource_space_obj.h>
#include <sel4gpi/endpoint_component.h>
#include <sel4gpi/model_exporting.h>

/** @file APIs for managing and interacting with the resource space component
 *
 * Defines the constants for the protocol, messages, and server-side state, as
 * well as the entry point and back-end routines for the server's API.
 *
 * All vka_t, vspace_t and simple_t instances that are supplied to this library
 * by the developer must persist and be functional for the lifetime of the
 * server thread.
 */

#define RESSPC_RPC_MAGIC 0x535043
#define RESSPC_SERVS "ResSpace Component: "
#define RESSPC_SERVC "ResSpace Client   : "

// Default "space" for resource spaces to be part of, to prevent circular definition
// of resource space
#define RESSPC_SPACE_ID 0x1

/* Per-space context maintained by the server. */
typedef struct _resspc_component_registry_entry
{
    resource_registry_node_t gen; ///< Generic registry entry data
    res_space_t space;            ///< Resource space data
} resspc_component_registry_entry_t;

/**
 * Configuration options for a resource space
 * Pass as arg0 to resource_component_allocate
 **/
typedef struct _resspc_config
{
    gpi_cap_t type;     ///< Type of resources in the space
    seL4_CPtr ep;       ///< Raw endpoint of the resource space manager
    gpi_obj_id_t pd_id; ///< ID of the manager PD
    void *data;         ///< Field for some generic data (not used)
} resspc_config_t;

/**
 * To initialize the resource space component at the beginning of execution
 */
int resspc_component_initialize(vka_t *server_vka,
                                vspace_t *server_vspace,
                                vka_object_t server_ep_obj);

/* Global server instance accessor functions. */
resource_component_context_t *get_resspc_component(void);

/**
 * Find a resource space by id
 *
 * @param space_id the resource space id
 */
resspc_component_registry_entry_t *resource_space_get_entry_by_id(gpi_space_id_t space_id);

/**
 * Add a map connection from one resource space to another
 * This allows us to map a resource in the first space to a resource in the second space
 *
 * @param src_spc_id ID of the source resource space
 * @param dest_spc_id ID of the destination resource space
 * @return 0 on success, error otherwise
 */
int resspc_component_map_space(gpi_space_id_t src_spc_id, gpi_space_id_t dest_spc_id);

/**
 * Check if the source resource space maps to the destination resource space
 *
 * @param src_space_id ID of the source resource space
 * @param dest_space_id ID of the destination resource space
 * @return 1 if the mapping exists, 0 otherwise
 */
int resspc_check_map(gpi_space_id_t src_space_id, gpi_space_id_t dest_space_id);

/**
 * Mark a resource space for deletion
 * This is used while destroying a PD that manages the resource space
 * The spaces marked for deletion will be swept later with resspc_component_sweep()
 *
 * @param spc_id the space to mark for deletion
 * @param execute_cleanup_policy if true, execute a cleanup policy starting from this space
 * @return 0 on success, error otherwise
 */
int resspc_component_mark_delete(gpi_space_id_t spc_id, bool execute_cleanup_policy);

/**
 * Sweep any resource spaces marked for deletion
 */
void resspc_component_sweep(void);

/**
 * Add any relations of a given space to the model state
 *
 * @param space the target space
 * @param ms the model state
 * @param pd_node the node for the PD being extracted (unused)
 * @return gpi_model_node_t * the model state node for the resource space
 */
gpi_model_node_t *resspc_dump_rr(res_space_t *space, model_state_t *ms, gpi_model_node_t *pd_node);