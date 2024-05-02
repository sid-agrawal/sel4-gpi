#pragma once

#include <stdint.h>

#include <sel4/sel4.h>
#include <sel4test/test.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/pd_clientapi.h>

/** @file
 * Utility functions for PDs that serve GPI resources
 */

#define RESOURCE_SERVER_DEBUG 0

/* IPC values returned in the "label" message header. */
enum rs_errors
{
    RS_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    RS_ERROR_RR_SIZE = seL4_NumErrors, // RR request shared memory is too small
    RS_ERROR_DNE,                      // RR request resource no longer exists
    RS_ERROR_NS,                       // Namespace does not exist
    RS_NUM_ERRORS
};

/* IPC Message register values for RSMSGREG_FUNC */
enum rs_funcs
{
    RS_FUNC_GET_RR_REQ = 0,
    RS_FUNC_GET_RR_ACK,

    RS_FUNC_NEW_NS_REQ,
    RS_FUNC_NEW_NS_ACK,

    RS_FUNC_END,
};

/* Message registers for all resource server requests */
enum rs_msgregs
{
    /* These are fixed headers in every message. */
    RSMSGREG_FUNC = 0,

    /* This is a convenience label for IPC MessageInfo length. */
    RSMSGREG_LABEL0,

    /* Extract RR */
    RSMSGREG_EXTRACT_RR_REQ_SIZE = RSMSGREG_LABEL0,
    RSMSGREG_EXTRACT_RR_REQ_VADDR,
    RSMSGREG_EXTRACT_RR_REQ_ID,
    RSMSGREG_EXTRACT_RR_REQ_PD_ID,
    RSMSGREG_EXTRACT_RR_REQ_RS_PD_ID,
    RSMSGREG_EXTRACT_RR_REQ_END,
    RSMSGREG_EXTRACT_RR_ACK_END = RSMSGREG_LABEL0,

    /* New NS */
    RSMSGREG_NEW_NS_REQ_END = RSMSGREG_LABEL0,
    RSMSGREG_NEW_NS_ACK_ID = RSMSGREG_LABEL0,
    RSMSGREG_NEW_NS_ACK_END,
};

/**
 * Generic resource server context
 */
typedef struct _resource_server_context
{
    gpi_cap_t resource_type;
    uint64_t server_id;

    // Run to serve requests
    seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr, bool *);

    // Run once when the server is started
    int (*init_fn)();

    // RDEs and other EPs
    seL4_CPtr parent_ep;
    seL4_CPtr mo_ep;
    ads_client_context_t ads_conn;
    pd_client_context_t pd_conn;

    // The server listens on this endpoint.
    seL4_CPtr server_ep;

    // Other
    seL4_CPtr mcs_reply;
} resource_server_context_t;

/**
 * Starts a resource server in a new PD
 * @param rde_id Manager ID of RDE to add, optionsl
 * @param rde_pd_cap PD resource for RDE to add, optional
 * @param image_name name of the resource server's image
 * @param server_pd_cap returns the PD resource of the started server
 * @param resource_manager_id returns the resource manager ID of the started server
 */
int start_resource_server_pd(uint64_t rde_id,
                             seL4_CPtr rde_pd_cap,
                             char *image_name,
                             seL4_CPtr *server_pd_cap,
                             uint64_t *resource_manager_id);

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
 * Request a resource server to dump resource relations
 *
 * @param server_ep Unbadged ep of the resource server
 * @param res_id The id of the resource to dump relations for
 * @param pd_id The id of the pd that has the resource (for the has_access_to row)
 * @param server_pd_id The id of the server pd
 * @param remote_vaddr location of shared memory in the resource server
 * @param local_vaddr location of shared memory in the caller
 * @param size size of shared memory
 * @param model_state_t Location of the resulting model state
 *                     (same as local_vaddr on success)
 * @return
 *      RS_NOERROR if dump completed successfully
 *      RS_ERROR_RR_SIZE if size was too small to write RR
 *      + Error codes for the respective resource server
 */
int resource_server_get_rr(seL4_CPtr server_ep,
                           seL4_Word res_id,
                           seL4_Word pd_id,
                           seL4_Word server_pd_id,
                           void *remote_vaddr,
                           void *local_vaddr,
                           size_t size,
                           model_state_t **ret_state);

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

/**
 * Request a new namespace ID from a resource server
 *
 * @param server_ep the EP of the resource server
 * @param ns_id returns the newly allocated NS ID
 */
int resource_server_client_new_ns(seL4_CPtr server_ep,
                                  uint64_t *ns_id);