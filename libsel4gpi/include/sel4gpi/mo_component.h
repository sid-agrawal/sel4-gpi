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

    MOMSGREG_CONNECT_ACK_END = MOMSGREG_LABEL0,

    /* Disconnect / Delete*/
    MOMSGREG_DISCONNECT_REQ_END = MOMSGREG_LABEL0,
    MOMSGREG_DISCONNECT_ACK_END = MOMSGREG_LABEL0,
};

/* Per-client context maintained by the server. */
typedef struct _mo_component_registry_entry
{
    mo_t mo;
    uint32_t count; /*There can be more than one cap to this object.*/
    struct _mo_component_registry_entry *next;

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

    int registry_n_entries;
    mo_component_registry_entry_t *client_registry;
} mo_component_context_t;

/**
 * Internal library function: acts as the main() for the server thread.
 **/
void mo_component_handle(seL4_MessageInfo_t tag,
                         seL4_Word badge,
                         cspacepath_t *received_cap,
                         seL4_MessageInfo_t *reply_tag);

/* Global server instance accessor functions. */
mo_component_context_t *get_mo_component(void);

void mo_handle_allocation_request(seL4_MessageInfo_t *reply_tag);

int forge_mo_cap_from_frames(seL4_CPtr *frame_caps,
                             uint32_t num_pages,
                             vka_t *vka,
                             seL4_CPtr *cap_ret,
                             mo_t **mo_ref);

int forge_mo_caps_from_vspace(vspace_t *child_vspace,
                              vka_t *vka,
                              uint32_t *num_return_caps,
                              seL4_CPtr *cap_ret);

mo_component_registry_entry_t *mo_component_registry_get_entry_by_badge(seL4_Word badge);