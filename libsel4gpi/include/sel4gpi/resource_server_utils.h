#pragma once

#include <stdint.h>

#include <utils/uthash.h>

#include <sel4/sel4.h>
#include <sel4test/test.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/endpoint_clientapi.h>
#include <sel4gpi/resource_types.h>
#include <sel4gpi/resource_registry.h>
#include <sel4gpi/resource_server_clientapi.h>
#include <sel4gpi/gpi_rpc.h>

/** @file
 * Utility functions for non-RT PDs that serve GPI resources
 */
#define RESOURCE_SERVER_DEBUG 1
#define SERVER_UTILS "Server Utils"

// Could use the server's debug function instead
#if RESOURCE_SERVER_DEBUG
#define RESOURCE_SERVER_PRINTF(...)                                       \
    do                                                                    \
    {                                                                     \
        if (context->debug_print)                                         \
        {                                                                 \
            printf("%s %s: ", context->resource_type_name, SERVER_UTILS); \
            printf(__VA_ARGS__);                                          \
        }                                                                 \
    } while (0);
#else
#define RESOURCE_SERVER_PRINTF(...)
#endif

/**
 * Generic resource server context
 */
typedef struct _resource_server_context
{
    char resource_type_name[RESOURCE_TYPE_MAX_STRING_SIZE]; ///< Text name of the resource served by the component
    gpi_cap_t resource_type;                                ///< Code of the resource type served by the component
    sel4gpi_rpc_env_t rpc_env;                              //< RPC server context
    resspc_client_context_t default_space;                  ///< Connection to the default resource space

    void (*request_handler)( ///< Callback called when the server receives a client request
        void *msg_p,         ///< Contains the client message, type is the server's protobuf request structure
        void *msg_reply_p,   ///< To fill with the reply message, type is the server's protobuf reply structure
        seL4_Word sender_badge,
        seL4_CPtr received_cap,
        bool *need_new_reply_cap);

    int (*work_handler)( ///< Callback called when the RT needs the server to do some work
        PdWorkReturnMessage *work);

    int (*init_fn)(); ///< Run once when the server is started

    uint64_t parent_pd_id; ///< Client ID of the parent PD

    bool debug_print; ///< True if the resource server should output debug prints

    // RDEs and other EPs
    seL4_CPtr mo_ep;               ///< MO request ep
    seL4_CPtr resspc_ep;           ///< Resource space request ep
    ep_client_context_t parent_ep; ///< Parent's EP, used to notify once started
    ads_client_context_t ads_conn; ///< This PD's current ADS object
    pd_client_context_t pd_conn;   ///< This PD's PD object
    ep_client_context_t server_ep; ///< The server's own endpoint that it listens for requests on

    seL4_CPtr mcs_reply; ///< Unused
} resource_server_context_t;

/**
 * Starts the resource server in the current
 * thread of the current PD
 *
 * @param server_type string name of the resource type this server will provide
 * @param request_handler Function to handle client requests
 *                  param: msg_p, the client message
 *                  param: msg_reply_p, to fill with the reply message
 *                  param: seL4_Word badge, the request's badge
 *                  param: seL4_CPtr cap, the received cap
 *                  return: seL4_MessageInfo_t reply info
 * @param work_handler Function to handle work requests from the RT
 *                  param: PdWorkReturnMessage *work, the work details
 *                  return: 0 on success, error otherwise
 * @param parent_ep Endpoint of the parent process
 * @param parent_pd_id the PD ID of the parent, so we can create an RDE
 * @param init_fn To run at the beginning of main thread execution
 * @param debug_print If true, outputs debug print
 * @param request_desc protobuf structure for requests to this server
 * @param reply_desc protobuf structure for replies from this server
 * @return 0 on successful exit, nonzero otherwise
 */
int resource_server_start(resource_server_context_t *context,
                          char *server_type,
                          void (*request_handler)(void *, void *, seL4_Word, seL4_CPtr, bool *),
                          int (*work_handler)(PdWorkReturnMessage *),
                          seL4_CPtr parent_ep,
                          uint64_t parent_pd_id,
                          int (*init_fn)(),
                          bool debug_print,
                          pb_msgdesc_t *request_desc,
                          pb_msgdesc_t *reply_desc);

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

/**
 * Setup for the resource server to begin model extraction
 *
 * @param context
 * @param n_pages number of pages to allocate for the MO
 * @param mo this structure will be filled out with the MO allocated for model extraction
 * @param ms returns the location of the model state
 * @return 0 on success, error otherwise
 */
int resource_server_extraction_setup(resource_server_context_t *context,
                                     int n_pages,
                                     mo_client_context_t *mo,
                                     model_state_t **ms);

/**
 * Finish model extraction by sending the result to the RT and destroying the allocated MO
 *
 * @param context
 * @param mo the MO allocated for model extraction
 * @param ms returns the location of the model state
 * @param n_work the number of work requests being fulfilled
 * @return 0 on success, error otherwise
 */
int resource_server_extraction_finish(resource_server_context_t *context, mo_client_context_t *mo,
                                      model_state_t *ms, int n_requests);

/**
 * Finish a model extraction if there is no data to send
 *
 * @param context
 * @param n_requests the number of requests being fulfilled
 * @return 0 on success, error otherwise
 */
int resource_server_extraction_no_data(resource_server_context_t *context, int n_requests);