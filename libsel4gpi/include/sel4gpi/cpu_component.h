#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/cpu_obj.h>

/** @file APIs for managing and interacting with the serial server thread.
 *
 * Defines the constants for the protocol, messages, and server-side state, as
 * well as the entry point and back-end routines for the server's API.
 *
 * All vka_t, vspace_t and simple_t instances that are supplied to this library
 * by the developer must persist and be functional for the lifetime of the
 * server thread.
 */

#define CPUSERVS     "CPUServ Component: "
#define CPUSERVC     "CPUServ Client   : "

#define CPU_SERVER_BADGE_VALUE_EMPTY (0)
#define CPU_SERVER_BADGE_PARENT_VALUE (0xDEADBEEF)


/* IPC values returned in the "label" message header. */
enum cpu_component_errors {
    CPU_SERVER_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    CPU_SERVER_ERROR_BIND_FAILED = seL4_NumErrors,
    CPU_SERVER_ERROR_UNKNOWN
};

/* IPC Message register values for SSMSGREG_FUNC */
enum cpu_component_funcs {
    CPU_FUNC_START_REQ = 0,
    CPU_FUNC_START_ACK,

    CPU_FUNC_CONFIG_REQ,
    CPU_FUNC_CONFIG_ACK,

    CPU_FUNC_DISCONNECT_REQ,
    CPU_FUNC_DISCONNECT_ACK,
};

/* Designated purposes of each message register in the mini-protocol. */
enum cpu_component_msgregs
{
    /* These four are fixed headers in every serserv message. */
    CPUMSGREG_FUNC = 0,
    /* This is a convenience label for IPC MessageInfo length. */
    CPUMSGREG_LABEL0,

    /* Connect / New */
    CPUMSGREG_CONNECT_REQ_END = CPUMSGREG_LABEL0,

    CPUMSGREG_CONNECT_ACK_END = CPUMSGREG_LABEL0,

    /* Server Spawn */
    CPUMSGREG_SPAWN_SYNC_REQ_END = CPUMSGREG_LABEL0,

    CPUMSGREG_SPAWN_SYNC_ACK_END = CPUMSGREG_LABEL0,

    /* Start */
    CPUMSGREG_START_FUNC_VADDR = CPUMSGREG_LABEL0,
    CPUMSGREG_START_REQ_END,

    CPUMSGREG_START_ACK_END = CPUMSGREG_LABEL0,

    /* Config */
    CPUMSGREG_CONFIG_REQ_END = CPUMSGREG_LABEL0,

    CPUMSGREG_CONFIG_ACK_END = CPUMSGREG_LABEL0,

    /* Disconnect / Delete*/
    CPUMSGREG_DISCONNECT_REQ_END = CPUMSGREG_LABEL0,

    CPUMSGREG_DISCONNECT_ACK_END = CPUMSGREG_LABEL0,
};

/* Per-client context maintained by the server. */
typedef struct _cpu_component_registry_entry {
    cpu_t cpu;
    /* In our model each CPU can have its own cspace. */
    seL4_CNode cspace_root;
    struct _cpu_component_registry_entry *next;
    
} cpu_component_registry_entry_t;

/* State maintained by the server. */
typedef struct _cpu_component_context {
    simple_t *server_simple;
    vka_t *server_vka;
    seL4_CPtr server_cspace;
    vspace_t *server_vspace;
    sel4utils_thread_t server_thread;

    // The server listens on this endpoint. 
    vka_object_t server_ep_obj;

    int registry_n_entries;
    cpu_component_registry_entry_t *client_registry;
} cpu_component_context_t;

/** 
 * Internal library function: acts as the main() for the server thread.
 **/
void cpu_component_handle(seL4_MessageInfo_t tag,
                          seL4_Word badge,
                          cspacepath_t *received_cap,
                          seL4_MessageInfo_t *reply_tag);

/* Global server instance accessor functions. */
cpu_component_context_t *get_cpu_component(void);

void cpu_handle_allocation_request(seL4_MessageInfo_t *reply_tag);