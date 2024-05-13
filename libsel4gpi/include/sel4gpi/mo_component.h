#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/mo_obj.h>
#include <sel4gpi/ads_obj.h>
#include <sel4gpi/resource_server_utils.h>

/** @file APIs for managing and interacting with the serial server thread.
 *
 * Defines the constants for the protocol, messages, and server-side state, as
 * well as the entry point and back-end routines for the server's API.
 *
 * All vka_t, vspace_t and simple_t instances that are supplied to this library
 * by the developer must persist and be functional for the lifetime of the
 * server thread.
 */

#define MOSERVS "MOServ Component: "
#define MOSERVC "MOServ Client   : "

#define MO_SERVER_BADGE_VALUE_EMPTY (0)
#define MO_SERVER_BADGE_PARENT_VALUE (0xDEADBEEF)

/* IPC values returned in the "label" message header. */
enum mo_component_errors
{
    MO_SERVER_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    MO_SERVER_ERROR_BIND_FAILED = seL4_NumErrors,
    MO_SERVER_ERROR_UNKNOWN
};

/* IPC Message register values for SSMSGREG_FUNC */
enum mo_component_funcs
{
    MO_FUNC_CONNECT_REQ,
    MO_FUNC_CONNECT_ACK,

    MO_FUNC_DISCONNECT_REQ,
    MO_FUNC_DISCONNECT_ACK,
};

/* Designated purposes of each message register in the mini-protocol. */
enum mo_component_msgregs
{
    /* These four are fixed headers in every serserv message. */
    MOMSGREG_FUNC = 0,
    /* This is a convenience label for IPC MessageInfo length. */
    MOMSGREG_LABEL0,

    /* Connect / New */
    MOMSGREG_CONNECT_REQ_NUM_PAGES = MOMSGREG_LABEL0,
    MOMSGREG_CONNECT_REQ_END,

    MOMSGREG_CONNECT_ACK_ID = MOMSGREG_LABEL0,
    MOMSGREG_CONNECT_ACK_END,

    /* Disconnect / Delete*/
    MOMSGREG_DISCONNECT_REQ_END = MOMSGREG_LABEL0,
    MOMSGREG_DISCONNECT_ACK_END = MOMSGREG_LABEL0,
};

/* Per-client context maintained by the server. */
typedef struct _mo_component_registry_entry
{
    resource_server_registry_node_t gen;
    mo_t mo;
} mo_component_registry_entry_t;

/* State maintained by the server. */
typedef struct _mo_component_context
{
    simple_t *server_simple;
    vka_t *server_vka;
    seL4_CPtr server_cspace;
    vspace_t *server_vspace;
    sel4utils_thread_t server_thread;

    // The server listens on this endpoint.
    vka_object_t server_ep_obj;

    // Registry
    resource_server_registry_t mo_registry;
} mo_component_context_t;

/**
 * To initialize the mo component at the beginning of execution
 */
int mo_component_initialize(simple_t *server_simple,
                            vka_t *server_vka,
                            seL4_CPtr server_cspace,
                            vspace_t *server_vspace,
                            sel4utils_thread_t server_thread,
                            vka_object_t server_ep_obj);

/**
 * Internal library function: acts as the main() for the server thread.
 **/
void mo_component_handle(seL4_MessageInfo_t tag,
                         seL4_Word badge,
                         cspacepath_t *received_cap,
                         seL4_MessageInfo_t *reply_tag);

/* Global server instance accessor functions. */
mo_component_context_t *get_mo_component(void);

void mo_handle_allocation_request(seL4_Word sender_badge, seL4_MessageInfo_t *reply_tag);

int forge_mo_cap_from_frames(seL4_CPtr *frame_caps,
                             uint32_t num_pages,
                             vka_t *vka,
                             uint32_t client_pd_id,
                             seL4_CPtr *cap_ret,
                             mo_t **mo_ref);

mo_component_registry_entry_t *mo_component_registry_get_entry_by_badge(seL4_Word badge);

mo_component_registry_entry_t *mo_component_registry_get_entry_by_id(seL4_Word objectID);

/**
 * Allocate a new MO object and add it to the registry
 * Exposed for use by the ADS component during ADS copy
 * 
 * @param client_id PD id of the recipient of the MO
 * @param forge true if this function should not actually allocate an MO with frames, use for forging
 * @param ret_entry registry entry of the new MO
 * @param ret_cap the badged endpoint capability for the new MO
*/
int mo_component_allocate_mo(uint64_t client_id, bool forge, int num_pages, mo_component_registry_entry_t **ret_entry, seL4_CPtr *ret_cap);