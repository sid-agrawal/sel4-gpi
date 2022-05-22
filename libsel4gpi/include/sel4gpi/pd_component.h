#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/pd_obj.h>

/** @file APIs for managing and interacting with the serial server thread.
 *
 * Defines the constants for the protocol, messages, and server-side state, as
 * well as the entry point and back-end routines for the server's API.
 *
 * All vka_t, vspace_t and simple_t instances that are supplied to this library
 * by the developer must persist and be functional for the lifetime of the
 * server thread.
 */

#define PDSERVS     "PDServ Component: "
#define PDSERVC     "PDServ Client   : "

#define PD_SERVER_BADGE_VALUE_EMPTY (0)
#define PD_SERVER_BADGE_PARENT_VALUE (0xDEADBEEF)


/* IPC values returned in the "label" message header. */
enum pd_component_errors {
    PD_SERVER_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    PD_SERVER_ERROR_BIND_FAILED = seL4_NumErrors,
    PD_SERVER_ERROR_UNKNOWN
};

/* IPC Message register values for SSMSGREG_FUNC */
enum pd_component_funcs {
    PD_FUNC_START_REQ = 0,
    PD_FUNC_START_ACK,

    PD_FUNC_CONFIG_REQ,
    PD_FUNC_CONFIG_ACK,

    PD_FUNC_DISCONNECT_REQ,
    PD_FUNC_DISCONNECT_ACK,
};

/* Designated purposes of each message register in the mini-protocol. */
enum pd_component_msgregs
{
    /* These four are fixed headers in every serserv message. */
    PDMSGREG_FUNC = 0,
    /* This is a convenience label for IPC MessageInfo length. */
    PDMSGREG_LABEL0,

    /* Connect / New */
    PDMSGREG_CONNECT_REQ_END = PDMSGREG_LABEL0,

    PDMSGREG_CONNECT_ACK_END = PDMSGREG_LABEL0,

    /* Server Spawn */
    PDMSGREG_SPAWN_SYNC_REQ_END = PDMSGREG_LABEL0,

    PDMSGREG_SPAWN_SYNC_ACK_END = PDMSGREG_LABEL0,

    /* Load */
    PDMSGREG_LOAD_FUNC_VADDR = PDMSGREG_LABEL0,
    PDMSGREG_LOAD_REQ_END,

    PDMSGREG_START_ACK_END = PDMSGREG_LABEL0,

    /* Start */
    PDMSGREG_START_REQ_END = PDMSGREG_LABEL0,

    PDMSGREG_START_ACK_END = PDMSGREG_LABEL0,

    /* Disconnect / Delete*/
    PDMSGREG_DISCONNECT_REQ_END = PDMSGREG_LABEL0,

    PDMSGREG_DISCONNECT_ACK_END = PDMSGREG_LABEL0,
};

/* Per-client context maintained by the server. */
typedef struct _pd_component_registry_entry {
    pd_t pd;
    /* In our model each PD can have its own cspace. */
    seL4_CNode cspace_root;
    struct _pd_component_registry_entry *next;
    
} pd_component_registry_entry_t;

/* State maintained by the server. */
typedef struct _pd_component_context {
    simple_t *server_simple;
    vka_t *server_vka;
    seL4_CPtr server_cspace;
    vspace_t *server_vspace;
    sel4utils_thread_t server_thread;

    // The server listens on this endpoint. 
    vka_object_t server_ep_obj;

    int registry_n_entries;
    pd_component_registry_entry_t *client_registry;
} pd_component_context_t;

/** 
 * Internal library function: acts as the main() for the server thread.
 **/
void pd_component_handle(seL4_MessageInfo_t tag,
                          seL4_Word badge,
                          cspacepath_t *received_cap,
                          seL4_MessageInfo_t *reply_tag);

/* Global server instance accessor functions. */
pd_component_context_t *get_pd_component(void);

void pd_handle_allocation_request(seL4_MessageInfo_t *reply_tag);