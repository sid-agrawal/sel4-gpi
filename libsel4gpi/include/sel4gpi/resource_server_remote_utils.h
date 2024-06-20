#pragma once

#include <stdint.h>

#include <utils/uthash.h>

#include <sel4/sel4.h>
#include <sel4test/test.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_types.h>
#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/resource_server_clientapi.h>
#include <sel4gpi/gpi_rpc.h>

/** @file
 * Utility functions for non-RT PDs that serve GPI resources
 */

/**
 * Generic resource server context
 */
typedef struct _resource_server_context
{
    char resource_type_name[RESOURCE_TYPE_MAX_STRING_SIZE];
    gpi_cap_t resource_type;

    // RPC server context
    sel4gpi_rpc_server_t rpc_env;

    // Connection to the default resource space
    resspc_client_context_t default_space;

    // Run to serve requests
    seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr, bool *);

    // Run once when the server is started
    int (*init_fn)();

    // Client ID of the parent PD
    uint64_t parent_pd_id;

    // RDEs and other EPs
    seL4_CPtr parent_ep;
    seL4_CPtr mo_ep;
    seL4_CPtr resspc_ep;
    ads_client_context_t ads_conn;
    pd_client_context_t pd_conn;

    // The server listens on this endpoint.
    seL4_CPtr server_ep;

    // Other
    seL4_CPtr mcs_reply;
} resource_server_context_t;

/**
 * Starts the resource server in the current
 * thread of the current PD
 *
 * @param server_type string name of the resource type this server will provide
 * @param request_handler Function to handle client requests
 *                  param: seL4_MessageInfo_t tag, the request tag
 *                  param: seL4_Word badge, the request's badge
 *                  param: seL4_CPtr cap, the received cap
 *                  return: seL4_MessageInfo_t reply info
 * @param parent_ep Endpoint of the parent process
 * @param parent_pd_id the PD ID of the parent, so we can create an RDE 
 * @param init_fn To run at the beginning of main thread execution
 * @return 0 on successful exit, nonzero otherwise
 */
int resource_server_start(resource_server_context_t *context,
                          char *server_type,
                          seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr, bool *),
                          seL4_CPtr parent_ep,
                          uint64_t parent_pd_id,
                          int (*init_fn)());

/**
 * Main function for a resource server, receives requests
 */
int resource_server_main(void *context_v);

/**
 * Attach a MO from a client request to the server's ADS
 * @param mo_cap The MO cap to attach
 * @param vaddr Returns the vaddr where MO was attached
 */
int resource_server_attach_mo(resource_server_context_t *context,
                              seL4_CPtr mo_cap,
                              void **vaddr);

/**
 * Remove a previously attached MO from the server's ADS
 * @param vaddr The vaddr where MO was attached
 */
int resource_server_unattach(resource_server_context_t *context,
                             void *vaddr);

/**
 * Notifies the PD component of a resource that is created, but not yet
 * given to a client PD
 *
 * @param resource_id ID of the resource, needs to be unique within this server
 * @param space_conn Connection to the resource space, or NULL to use the server default
 * @param dest Returns the slot of the badged copy in the recipient's cspace
 */
int resource_server_create_resource(resource_server_context_t *context,
                                    resspc_client_context_t *space_conn,
                                    uint64_t resource_id);

/**
 * Notifies the PD component to create a badged copy of the server's endpoint
 * as a new resource in the recipient's cspace
 *
 * @param space_id ID of the resource space being allocated from
 * @param resource_id ID of the resource, needs to be unique within this server
 * @param client_id ID of the client PD
 * @param dest Returns the slot of the badged copy in the recipient's cspace
 */
int resource_server_give_resource(resource_server_context_t *context,
                                  uint64_t space_id,
                                  uint64_t resource_id,
                                  uint64_t client_id,
                                  seL4_CPtr *dest);

/**
 * Creates a new namespace resource space for this resource server
 *
 * @param context
 * @param client_id ID of the client PD that should get an RDE to the resource space
 * @param ret_conn returns the newly allocated resource space
 */
int resource_server_new_res_space(resource_server_context_t *context,
                                  uint64_t client_id,
                                  resspc_client_context_t *ret_conn);