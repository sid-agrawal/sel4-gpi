#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/counter_obj.h>

/** @file APIs for managing and interacting with the serial server thread.
 *
 * Defines the constants for the protocol, messages, and server-side state, as
 * well as the entry point and back-end routines for the server's API.
 *
 * All vka_t, vspace_t and simple_t instances that are supplied to this library
 * by the developer must persist and be functional for the lifetime of the
 * server thread.
 */

#define COUNTERSERVS     "CounterServ Server: "
#define COUNTERSERVC     "CounterServ Client: "

#define COUNTER_SERVER_BADGE_VALUE_EMPTY (0)
#define COUNTER_SERVER_BADGE_PARENT_VALUE (0xDEADBEEF)


/* IPC values returned in the "label" message header. */
enum counter_server_errors {
    COUNTER_SERVER_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    COUNTER_SERVER_ERROR_BIND_FAILED = seL4_NumErrors,
    COUNTER_SERVER_ERROR_UNKNOWN
};

/* IPC Message register values for SSMSGREG_FUNC */
enum counter_server_funcs {
    FUNC_CONNECT_REQ = 0,
    FUNC_CONNECT_ACK,

    FUNC_SERVER_SPAWN_SYNC_REQ,
    FUNC_SERVER_SPAWN_SYNC_ACK,

    FUNC_INCREMENT_REQ,
    FUNC_INCREMENT_ACK,

    FUNC_DECREMENT_REQ,
    FUNC_DECREMENT_ACK,

    FUNC_DISCONNECT_REQ,
    FUNC_DISCONNECT_ACK,

};

/* Designated purposes of each message register in the mini-protocol. */
enum counter_server_msgregs {
    /* These four are fixed headers in every serserv message. */
    CSMSGREG_FUNC = 0,
    /* This is a convenience label for IPC MessageInfo length. */
    CSMSGREG_LABEL0,

    CSMSGREG_CONNECT_REQ_END = CSMSGREG_LABEL0,
    CSMSGREG_CONNECT_ACK_END = CSMSGREG_LABEL0,

    CSMSGREG_SPAWN_SYNC_REQ_END = CSMSGREG_LABEL0,
    CSMSGREG_SPAWN_SYNC_ACK_END = CSMSGREG_LABEL0,

    CSMSGREG_INCREMENT_REQ_END = CSMSGREG_LABEL0,
    CSMSGREG_INCREMENT_ACK_END = CSMSGREG_LABEL0,
    
    CSMSGREG_DECREMENT_REQ_END = CSMSGREG_LABEL0,
    CSMSGREG_DECREMENT_ACK_END = CSMSGREG_LABEL0,

    CSMSGREG_DISCONNECT_REQ_END = CSMSGREG_LABEL0,
    CSMSGREG_DISCONNECT_ACK_END = CSMSGREG_LABEL0,
};

/* Per-client context maintained by the server. */
typedef struct _counter_server_registry_entry {
    seL4_Word badge_value;

    // This is the actual counter value.
    counter_t counter;
    struct _counter_server_registry_entry *next;
    
} counter_server_registry_entry_t;

/* State maintained by the server. */
typedef struct _counter_server_context {
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
    counter_server_registry_entry_t *client_registry;
} counter_server_context_t;

/* Global server instance accessor functions. */
counter_server_context_t *get_counter_server(void);

/** Internal library function: acts as the main() for the server thread.
 */
void counter_server_main(void);
