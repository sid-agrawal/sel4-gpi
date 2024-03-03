#pragma once

#include <stdint.h>

#include <sel4/sel4.h>
#include <sel4test/test.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/pd_clientapi.h>

#define RESOURCE_SERVER_DEBUG 0

/** @file
 * Utility functions for PDs that serve GPI resources
 */

/**
 * Generic resource server context
 */
typedef struct _resource_server_context
{
    // Used only when server started as thread
    vka_t *server_vka;

    // Generic functions used when started as thread or PD
    int (*next_slot)(seL4_CPtr *);
    int (*badge_ep)(seL4_Word, seL4_CPtr *);

    // RDEs and other EPs
    seL4_CPtr parent_ep;
    seL4_CPtr gpi_ep;
    ads_client_context_t *ads_conn;
    pd_client_context_t *pd_conn;

    // The server listens on this endpoint.
    seL4_CPtr server_ep;

    // Other
    seL4_CPtr mcs_reply;
} resource_server_context_t;

/**
 * Starts a resource server in a new PD
 * @param vka vka to use for temporary startup resources
 * @param gpi_ep endpoint of the gpi server
 * @param rde_type type of an RDE to add, optional
 * @param rde_ep value of an RDE to add, optional
 * @param image_name name of the resource server's image
 * @param server_ep returns the endpoint of the started server
 */
int start_resource_server_pd(vka_t *vka,
                             seL4_CPtr gpi_ep,
                             gpi_cap_t rde_type,
                             seL4_CPtr rde_ep,
                             char *image_name,
                             seL4_CPtr *server_ep);

/**
 * Spawns the resource server thread.
 * Server thread is spawned within the VSpace and
 * CSpace of the thread that spawned it.
 *
 * Note: mainly for use from the root task, which does
 * not have a PD cap. Within a regular PD, use resource_server_start
 *
 * CAUTION:
 * All vka_t, vspace_t, and simple_t instances passed to this library by
 * reference must remain functional throughout the lifetime of the server.
 *
 * @param parent_simple Initialized simple_t for the parent process that is
 *                      spawning the server thread.
 * @param parent_vka Initialized vka_t for the parent process that is spawning
 *                   the server thread.
 * @param parent_vspace Initialized vspace_t for the parent process that is
 *                      spawning the server thread.
 * @param gpi_ep Endpoint to the gpi server
 * @param parent_ep Endpoint to communicate with parent
 * @param ads_ep Initialized ADS connection
 * @param priority Server thread's priority.
 * @param thread_name Name of the new thread
 * @param main_fn Main function to run in new thread
 * @return int 0 on success, -1 otherwise
 */
int resource_server_spawn_thread(resource_server_context_t *context,
                                 simple_t *parent_simple,
                                 vka_t *parent_vka,
                                 vspace_t *parent_vspace,
                                 seL4_CPtr gpi_ep,
                                 seL4_CPtr parent_ep,
                                 seL4_CPtr ads_ep,
                                 uint8_t priority,
                                 char *thread_name,
                                 int (*main_fn)());

/**
 * Starts the resource server in the current
 * thread of the current PD
 *
 * @param ads_conn ADS RDE
 * @param pd_conn PD RDE
 * @param gpi_ep General gpi ep
 * @param parent_ep Endpoint of the parent process
 * @param main_fn Main function to execute when ready
 * @return 0 on successful exit, nonzero otherwise
 */
int resource_server_start(resource_server_context_t *context,
                          ads_client_context_t *ads_conn,
                          pd_client_context_t *pd_conn,
                          seL4_CPtr gpi_ep,
                          seL4_CPtr parent_ep,
                          int (*main_fn)());

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
 * Gets the next free cspace slot for a resource server to use
 * Uses the vka if the resource server has one, otherwise uses pd clientapi
 */
int resource_server_next_slot(resource_server_context_t *context,
                              seL4_CPtr *slot);

/**
 * Frees a previously allocated cspace slot
 * Uses the vka if the resource server has one, otherwise uses pd clientapi
 */
int resource_server_free_slot(resource_server_context_t *context,
                              seL4_CPtr slot);

/**
 * Creates a badged version of a raw endpoint capability
 * Uses the vka if the resource server has one, otherwise uses pd clientapi
 */
int resource_server_badge_ep(resource_server_context_t *context,
                             seL4_Word badge, seL4_CPtr *badged_ep);

/**
 * Main function for a resource server, receives requests
 * @param request_handler Function to handle client requests
 *                  param: seL4_MessageInfo_t tag, the request tag
 *                  param: seL4_Word badge, the request's badge
 *                  param: seL4_CPtr cap, the received cap
 *                  return: seL4_MessageInfo_t reply info
 */
int resource_server_main(resource_server_context_t *context,
                         seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr));

/**
 * Attach a MO from a client request to the server's ADS
 * @param mo_cap The MO cap to attach
 * @param vaddr Returns the vaddr where MO was attached
 */
int resource_server_attach_mo(resource_server_context_t *context,
                      seL4_CPtr mo_cap,
                      void **vaddr);