#pragma once

#include <stdint.h>

#include <utils/uthash.h>

#include <sel4/sel4.h>
#include <sel4test/test.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/resource_server_clientapi.h>

/** @file
 * Utility functions for non-RT PDs that serve GPI resources
 */

#define RESOURCE_SERVER_DEBUG 1

/**
 * Generic resource server context
 */
typedef struct _resource_server_context
{
    gpi_cap_t resource_type;

    // Connection to the default resource space
    resspc_client_context_t default_space;

    // Run to serve requests
    seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr, bool *);

    // Run once when the server is started
    int (*init_fn)();

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
 * @param server_type The type of resource this server will serve
 * @param request_handler Function to handle client requests
 *                  param: seL4_MessageInfo_t tag, the request tag
 *                  param: seL4_Word badge, the request's badge
 *                  param: seL4_CPtr cap, the received cap
 *                  return: seL4_MessageInfo_t reply info
 * @param parent_ep Endpoint of the parent process
 * @param init_fn To run at the beginning of main thread execution
 * @return 0 on successful exit, nonzero otherwise
 */
int resource_server_start(resource_server_context_t *context,
                          gpi_cap_t server_type,
                          seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr, bool *),
                          seL4_CPtr parent_ep,
                          int (*init_fn)());

/**
 * Recv function for MCS or non-MCS kernel
 */
seL4_MessageInfo_t resource_server_recv(resource_server_context_t *context,
                                        seL4_Word *sender_badge_ptr);

/**
 * Reply function for MCS or non-MCS kernel
 */
void resource_server_reply(resource_server_context_t *context,
                           seL4_MessageInfo_t tag);

/**
 * Gets the next free cspace slot, otherwise uses pd clientapi
 */
int resource_server_next_slot(resource_server_context_t *context,
                              seL4_CPtr *slot);

/**
 * Frees a previously allocated cspace slot, otherwise uses pd clientapi
 */
int resource_server_free_slot(resource_server_context_t *context,
                              seL4_CPtr slot);

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
 * @param dest Returns the slot of the badged copy in the recipient's cspace
 */
int resource_server_create_resource(resource_server_context_t *context,
                                    uint64_t resource_id);

/**
 * Notifies the PD component to create a badged copy of the server's endpoint
 * as a new resource in the recipient's cspace
 *
 * @param ns_id ID of the namespace being allocated from
 * @param resource_id ID of the resource, needs to be unique within this server
 * @param client_id ID of the client PD
 * @param dest Returns the slot of the badged copy in the recipient's cspace
 */
int resource_server_give_resource(resource_server_context_t *context,
                                  uint64_t ns_id,
                                  uint64_t resource_id,
                                  uint64_t client_id,
                                  seL4_CPtr *dest);

/**
 * Creates a new namespace ID for this resource server
 *
 * @param client_id Client ID of the client that requested the new NS
 * @param ns_id returns the newly allocated NS ID
 */
int resource_server_new_ns(resource_server_context_t *context,
                           uint64_t client_id,
                           uint64_t *ns_id);