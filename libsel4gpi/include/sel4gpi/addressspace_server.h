#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/ads_obj.h>

/** @file APIs for managing and interacting with the serial server thread.
 *
 * Defines the constants for the protocol, messages, and server-side state, as
 * well as the entry point and back-end routines for the server's API.
 *
 * All vka_t, vspace_t and simple_t instances that are supplied to this library
 * by the developer must persist and be functional for the lifetime of the
 * server thread.
 */

#define ADSSERVS     "ADSServ Server: "
#define ADSSERVC     "ADSServ Client: "
#define ADSSERVP     "ADSServ Parent: "

#define ADS_SERVER_BADGE_VALUE_EMPTY (0)
#define ADS_SERVER_BADGE_PARENT_VALUE (0xDEADBEEF)


/* IPC values returned in the "label" message header. */
enum counter_server_errors {
    ADS_SERVER_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    ADS_SERVER_ERROR_BIND_FAILED = seL4_NumErrors,
    ADS_SERVER_ERROR_UNKNOWN
};

/* IPC Message register values for SSMSGREG_FUNC */
enum counter_server_funcs {
    FUNC_CONNECT_REQ = 0,
    FUNC_CONNECT_ACK,

    FUNC_SERVER_SPAWN_SYNC_REQ,
    FUNC_SERVER_SPAWN_SYNC_ACK,

    FUNC_ATTACH_REQ,
    FUNC_ATTACH_ACK,

    FUNC_RM_REQ,
    FUNC_RM_ACK,

    FUNC_ATTACH_CPU_REQ,
    FUNC_ATTACH_CPU_ACK,

    FUNC_DISCONNECT_REQ,
    FUNC_DISCONNECT_ACK,
};

/* Designated purposes of each message register in the mini-protocol. */
enum counter_server_msgregs {
    /* These four are fixed headers in every serserv message. */
    CSMSGREG_FUNC = 0,
    /* This is a convenience label for IPC MessageInfo length. */
    CSMSGREG_LABEL0,

    /* Connect / New */
    CSMSGREG_CONNECT_REQ_END = CSMSGREG_LABEL0,

    CSMSGREG_CONNECT_ACK_END = CSMSGREG_LABEL0,


    /* Server Spawn */
    CSMSGREG_SPAWN_SYNC_REQ_END = CSMSGREG_LABEL0,

    CSMSGREG_SPAWN_SYNC_ACK_END = CSMSGREG_LABEL0,



    /* Attach */
    CSMSGREG_ATTACH_REQ_CA = CSMSGREG_LABEL0,
    CSMSGREG_ATTACH_REQ_END,

    CSMSGREG_ATTACH_ACK_END = CSMSGREG_LABEL0,
    


    /* Remove */
    CSMSGREG_RM_REQ_VA = CSMSGREG_LABEL0,
    CSMSGREG_RM_REQ_END,

    CSMSGREG_RM_ACK_END = CSMSGREG_LABEL0,



    /* Bind to CPU */
    CSMSGREG_BIND_CPU_REQ_END = CSMSGREG_LABEL0,

    CSMSGREG_BIND_CPU_ACK_END = CSMSGREG_LABEL0,



    /* Disconnect / Delete*/
    CSMSGREG_DISCONNECT_REQ_END = CSMSGREG_LABEL0,

    CSMSGREG_DISCONNECT_ACK_END = CSMSGREG_LABEL0,
};

/* Per-client context maintained by the server. */
typedef struct _ads_server_registry_entry {
    // This is the actual address space
    vspace_t *vspace;
    struct _ads_server_registry_entry *next;
    
} ads_server_registry_entry_t;

/* State maintained by the server. */
typedef struct _ads_server_context {
    simple_t *server_simple;
    vka_t *server_vka;
    seL4_CPtr server_cspace;
    vspace_t *server_vspace;
    sel4utils_thread_t server_thread;

    // The server listens on this endpoint. 
    vka_object_t server_ep_obj;

    // Parent's badge value.
    // There is only 1 parent and hence only 1 badge value.
    seL4_Word parent_badge_value;
    cspacepath_t _badged_server_ep_cspath;

    int registry_n_entries;
    ads_server_registry_entry_t *client_registry;
} ads_server_context_t;

/** 
 * Internal library function: acts as the main() for the server thread.
 **/
void ads_server_main(void);

/* Global server instance accessor functions. */
ads_server_context_t *get_ads_server(void);
