#pragma once

#include <stdint.h>
#include <sel4/sel4.h>

#include <sel4gpi/resource_server_rt_utils.h>
#include <sel4gpi/resource_space_obj.h>
#include <sel4gpi/endpoint_component.h>

/** @file APIs for managing and interacting with the resource space component
 *
 * Defines the constants for the protocol, messages, and server-side state, as
 * well as the entry point and back-end routines for the server's API.
 *
 * All vka_t, vspace_t and simple_t instances that are supplied to this library
 * by the developer must persist and be functional for the lifetime of the
 * server thread.
 */

#define RESSPC_SERVS "ResSpace Component: "
#define RESSPC_SERVC "ResSpace Client   : "

// Default "space" for resource spaces to be part of, to prevent circular definition
// of resource space
#define RESSPC_SPACE_ID 0x1

// Null resource space ID
#define RESSPC_ID_NULL 0xFF

/* IPC values returned in the "label" message header. */
enum res_space_component_errors
{
    RESSPC_SERVER_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    RESSPC_SERVER_ERROR_UNKNOWN = seL4_NumErrors,
};

/* IPC Message register values */
enum res_space_component_funcs
{
    RESSPC_FUNC_CONNECT_REQ = 0,
    RESSPC_FUNC_CONNECT_ACK,

    RESSPC_FUNC_DISCONNECT_REQ,
    RESSPC_FUNC_DISCONNECT_ACK,

    RESSPC_FUNC_MAP_SPACE_REQ,
    RESSPC_FUNC_MAP_SPACE_ACK,

    RESSPC_FUNC_CREATE_RES_REQ,
    RESSPC_FUNC_CREATE_RES_ACK,
};

/* Designated purposes of each message register in the mini-protocol. */
enum res_space_component_msgregs
{
    /* These four are fixed headers in every serserv message. */
    RESSPCMSGREG_FUNC = 0,
    /* This is a convenience label for IPC MessageInfo length. */
    RESSPCMSGREG_LABEL0,

    /* Connect / New */
    RESSPCMSGREG_CONNECT_REQ_CLIENT_ID = RESSPCMSGREG_LABEL0,
    RESSPCMSGREG_CONNECT_REQ_END,

    RESSPCMSGREG_CONNECT_ACK_ID = RESSPCMSGREG_LABEL0,
    RESSPCMSGREG_CONNECT_ACK_TYPE,
    RESSPCMSGREG_CONNECT_ACK_SLOT,
    RESSPCMSGREG_CONNECT_ACK_END,

    /* Map Space */
    RESSPCMSGREG_MAP_SPACE_REQ_SPACE_ID = RESSPCMSGREG_LABEL0,
    RESSPCMSGREG_MAP_SPACE_REQ_END,

    RESSPCMSGREG_MAP_SPACE_ACK_END,

    /* Create Resource */
    RESSPCMSGREG_CREATE_RES_REQ_RES_ID = RESSPCMSGREG_LABEL0,
    RESSPCMSGREG_CREATE_RES_REQ_END,

    RESSPCMSGREG_CREATE_RES_ACK_END,

    /* Disconnect / Delete*/
    RESSPCMSGREG_DISCONNECT_REQ_END = RESSPCMSGREG_LABEL0,
    RESSPCMSGREG_DISCONNECT_ACK_END = RESSPCMSGREG_LABEL0,
};

/* Per-space context maintained by the server. */
typedef struct _resspc_component_registry_entry
{
    resource_server_registry_node_t gen; ///< Generic registry entry data
    res_space_t space;                   ///< Resource space data
} resspc_component_registry_entry_t;

/**
 * Configuration options for a resource space
 * Pass as arg0 to resource_component_allocate
 **/
typedef struct _resspc_config
{
    gpi_cap_t type; ///< Type of resources in the space
    seL4_CPtr ep;   ///< Raw endpoint of the resource space manager
    pd_t *pd;       ///< Handle to the manager PD
    void *data;     ///< Field for some generic data (not used)
} resspc_config_t;

/**
 * To initialize the resource space component at the beginning of execution
 */
int resspc_component_initialize(simple_t *server_simple,
                                vka_t *server_vka,
                                seL4_CPtr server_cspace,
                                vspace_t *server_vspace,
                                sel4utils_thread_t server_thread,
                                vka_object_t server_ep_obj);

/* Global server instance accessor functions. */
resource_component_context_t *get_resspc_component(void);

/**
 * Find a resource space by id
 *
 * @param space_id the resource space id
 */
resspc_component_registry_entry_t *resource_space_get_entry_by_id(seL4_Word space_id);

/**
 * Add a map connection from one resource space to another
 * This allows us to map a resource in the first space to a resource in the second space
 * 
 * @param src_spc_id ID of the source resource space
 * @param dest_spc_id ID of the destination resource space
 * @return 0 on success, error otherwise
*/
int resspc_component_map_space(uint64_t src_spc_id, uint64_t dest_spc_id);

/**
 * Check if the source resource space maps to the destination resource space
 * 
 * @param src_space_id ID of the source resource space
 * @param dest_space_id ID of the destination resource space
 * @return 1 if the mapping exists, 0 otherwise
*/
int resspc_check_map(uint64_t src_space_id, uint64_t dest_space_id);

/**
 * Mark a resource space for deletion
 * This is used while destroying a PD that manages the resource space
 * The spaces marked for deletion will be swept later with resspc_component_sweep()
 * 
 * @param spc_id
 * @return 0 on success, error otherwise
*/
int resspc_component_mark_delete(uint64_t spc_id);

/**
 * Sweep any resource spaces marked for deletion
 * 
 * This executes the cleanup policy for resource spaces
 * (XXX) Arya: Todo, specify the cleanup policies
 * 
 * @return 0 on success, error otherwise
*/
int resspc_component_sweep(void);