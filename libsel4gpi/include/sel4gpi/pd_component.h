#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/pd_obj.h>
#include <sel4gpi/test_init_data.h>

/** @file APIs for managing and interacting with the serial server thread.
 *
 * Defines the constants for the protocol, messages, and server-side state, as
 * well as the entry point and back-end routines for the server's API.
 *
 * All vka_t, vspace_t and simple_t instances that are supplied to this library
 * by the developer must persist and be functional for the lifetime of the
 * server thread.
 */

#define PDSERVS "PDServ Component: "
#define PDSERVC "PDServ Client   : "

#define PD_SERVER_BADGE_VALUE_EMPTY (0)
#define PD_SERVER_BADGE_PARENT_VALUE (0xDEADBEEF)

/* (XXX) Arya: temp definition of supported images */
#define PD_N_IMAGES 6
static const char *pd_images[PD_N_IMAGES] = {"hello", "hello_kvstore", "ramdisk_server", "fs_server", "kvstore_server", "hello_benchmark"};
static const uint64_t pd_image_heap_size[PD_N_IMAGES] = {PAGE_SIZE_4K, 100 * PAGE_SIZE_4K, 100 * PAGE_SIZE_4K, 100 * PAGE_SIZE_4K, 100 * PAGE_SIZE_4K, PAGE_SIZE_4K};

#define PD_MAX_ARGC 4

/* IPC values returned in the "label" message header. */
enum pd_component_errors
{
    PD_SERVER_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    PD_SERVER_ERROR_BIND_FAILED = seL4_NumErrors,
    PD_SERVER_ERROR_UNKNOWN
};

/* IPC Message register values for SSMSGREG_FUNC */
enum pd_component_funcs
{
    PD_FUNC_LOAD_REQ = 0,
    PD_FUNC_LOAD_ACK,

    PD_FUNC_NEXT_SLOT_REQ,
    PD_FUNC_NEXT_SLOT_ACK,

    PD_FUNC_FREE_SLOT_REQ,
    PD_FUNC_FREE_SLOT_ACK,

    PD_FUNC_ALLOC_EP_REQ,
    PD_FUNC_ALLOC_EP_ACK,

    PD_FUNC_BADGE_EP_REQ,
    PD_FUNC_BADGE_EP_ACK,

    PD_FUNC_START_REQ,
    PD_FUNC_START_ACK,

    PD_FUNC_SENDCAP_REQ,
    PD_FUNC_SENDCAP_ACK,

    PD_FUNC_DUMP_REQ,
    PD_FUNC_DUMP_ACK,

    PD_FUNC_DISCONNECT_REQ,
    PD_FUNC_DISCONNECT_ACK,

    PD_FUNC_ADD_RDE_REQ,
    PD_FUNC_ADD_RDE_ACK,

    PD_FUNC_SHARE_RDE_REQ,
    PD_FUNC_SHARE_RDE_ACK,

    PD_FUNC_REGISTER_SERV_REQ,
    PD_FUNC_REGISTER_SERV_ACK,

    PD_FUNC_REGISTER_NS_REQ,
    PD_FUNC_REGISTER_NS_ACK,

    PD_FUNC_CREATE_RES_REQ,
    PD_FUNC_CREATE_RES_ACK,

    PD_FUNC_GIVE_RES_REQ,
    PD_FUNC_GIVE_RES_ACK,

    PD_FUNC_BENCH_IPC_REQ,
    PD_FUNC_BENCH_IPC_ACK,
};

/* Designated purposes of each message register in the mini-protocol. */
enum pd_component_msgregs
{
    /* These are fixed headers in every pd message. */
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
    PDMSGREG_LOAD_FUNC_IMAGE = PDMSGREG_LABEL0,
    PDMSGREG_LOAD_REQ_END,

    PDMSGREG_LOAD_ACK_END = PDMSGREG_LABEL0,

    /* Next Slot */
    PDMSGREG_NEXT_SLOT_REQ_END = PDMSGREG_LABEL0,

    PDMSGREG_NEXT_SLOT_PD_SLOT = PDMSGREG_LABEL0,
    PDMSGREG_NEXT_SLOT_ACK_END,

    /* Free Slot */
    PDMSGREG_FREE_SLOT_REQ_SLOT = PDMSGREG_LABEL0,
    PDMSGREG_FREE_SLOT_REQ_END,

    PDMSGREG_FREE_SLOT_ACK_END = PDMSGREG_LABEL0,

    /* Alloc EP */
    PDMSGREG_ALLOC_EP_REQ_END = PDMSGREG_LABEL0,

    PDMSGREG_ALLOC_EP_PD_SLOT = PDMSGREG_LABEL0,
    PDMSGREG_ALLOC_EP_ACK_END,

    /* Badge EP */
    PDMSGREG_BADGE_EP_REQ_BADGE = PDMSGREG_LABEL0,
    PDMSGREG_BADGE_EP_REQ_SRC,
    PDMSGREG_BADGE_EP_REQ_END,

    PDMSGREG_BADGE_EP_PD_SLOT = PDMSGREG_LABEL0,
    PDMSGREG_BADGE_EP_ACK_END,

    /* Send Cap */
    PDMSGREG_SEND_CAP_REQ_END = PDMSGREG_LABEL0,

    PDMSGREG_SEND_CAP_PD_SLOT = PDMSGREG_LABEL0,
    PDMSGREG_SEND_CAP_ACK_END,

    /* Dump Cap */
    PDMSGREG_DUMP_REQ_BUF_VA = PDMSGREG_LABEL0,
    PDMSGREG_DUMP_REQ_BUF_SZ,
    PDMSGREG_DUMP_REQ_END,

    PDMSGREG_DUMP_ACK_END = PDMSGREG_LABEL0,

    /* Start */
    /* (XXX) For now,  we only pass upt to 4 args, which may need fixing */
    PDMSGREG_START_ARGC = PDMSGREG_LABEL0,
    PDMSGREG_START_ARG0,
    PDMSGREG_START_ARG1,
    PDMSGREG_START_ARG2,
    PDMSGREG_START_ARG3,
    PDMSGREG_START_REQ_END,

    PDMSGREG_START_ACK_END = PDMSGREG_LABEL0,

    /* Disconnect / Delete*/
    PDMSGREG_DISCONNECT_REQ_END = PDMSGREG_LABEL0,

    PDMSGREG_DISCONNECT_ACK_END = PDMSGREG_LABEL0,

    /* Share RDE */
    PDMSGREG_SHARE_RDE_REQ_TYPE = PDMSGREG_LABEL0,
    PDMSGREG_SHARE_RDE_REQ_NS,
    PDMSGREG_SHARE_RDE_REQ_END,

    PDMSGREG_SHARE_RDE_ACK_END = PDMSGREG_LABEL0,

    /* Add RDE */
    PDMSGREG_ADD_RDE_REQ_MANAGER_ID = PDMSGREG_LABEL0,
    PDMSGREG_ADD_RDE_REQ_NSID,
    PDMSGREG_ADD_RDE_REQ_END,

    PDMSGREG_ADD_RDE_ACK_END = PDMSGREG_LABEL0,

    /* Register Resource Manager */
    PDMSGREG_REGISTER_SERV_REQ_TYPE = PDMSGREG_LABEL0,
    PDMSGREG_REGISTER_SERV_REQ_END,

    PDMSGREG_REGISTER_SERV_ACK_ID = PDMSGREG_LABEL0,
    PDMSGREG_REGISTER_SERV_ACK_END,

    /* Register Namespace */
    PDMSGREG_REGISTER_NS_REQ_MANAGER_ID = PDMSGREG_LABEL0,
    PDMSGREG_REGISTER_NS_REQ_CLIENT_ID,
    PDMSGREG_REGISTER_NS_REQ_END,

    PDMSGREG_REGISTER_NS_ACK_NSID = PDMSGREG_LABEL0,
    PDMSGREG_REGISTER_NS_ACK_END,

    /* Create Resource */
    PDMSGREG_CREATE_RES_REQ_MANAGER_ID = PDMSGREG_LABEL0,
    PDMSGREG_CREATE_RES_REQ_RES_ID,
    PDMSGREG_CREATE_RES_REQ_END,

    PDMSGREG_CREATE_RES_ACK_DEST = PDMSGREG_LABEL0,
    PDMSGREG_CREATE_RES_ACK_END,

    /* Give Resource */
    PDMSGREG_GIVE_RES_REQ_MANAGER_ID = PDMSGREG_LABEL0,
    PDMSGREG_GIVE_RES_REQ_NS_ID,
    PDMSGREG_GIVE_RES_REQ_CLIENT_ID,
    PDMSGREG_GIVE_RES_REQ_RES_ID,
    PDMSGREG_GIVE_RES_REQ_END,

    PDMSGREG_GIVE_RES_ACK_DEST = PDMSGREG_LABEL0,
    PDMSGREG_GIVE_RES_ACK_END,
};

/* Per-client context maintained by the server. */
typedef struct _pd_component_registry_entry
{
    pd_t pd;
    /* In our model each PD can have its own cspace. */
    seL4_CNode cspace_root;
    seL4_CPtr raw_cap_in_root;
    struct _pd_component_registry_entry *next;

} pd_component_registry_entry_t;

/* Tracks resource servers that have registered with the pd component */
typedef struct _pd_component_resource_manager_entry
{
    uint32_t manager_id;
    gpi_cap_t resource_type;
    seL4_CPtr server_ep;
    pd_t *pd;
    uint64_t ns_index; // Tracks the last allocated namespace ID

    struct _pd_component_resource_manager_entry *next;
} pd_component_resource_manager_entry_t;

/* State maintained by the server. */
typedef struct _pd_component_context
{
    simple_t *server_simple;
    vka_t *server_vka;
    seL4_CPtr server_cspace;
    vspace_t *server_vspace;
    sel4utils_thread_t server_thread;

    // The server listens on this endpoint.
    vka_object_t server_ep_obj;

    // Registry data
    int registry_n_entries;
    pd_component_registry_entry_t *client_registry;
    int resource_manager_n_entries;
    pd_component_resource_manager_entry_t *server_registry;

    // Root task's PD
    pd_t rt_pd;
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

void pd_handle_allocation_request(seL4_Word sender_badge, seL4_MessageInfo_t *reply_tag);

// Only used to forge the test process' PD cap
int forge_pd_cap_from_init_data(
    test_init_data_t *init_data, // Change this to something else
    vka_t *vka);

// Only used to update the test process' PD cap
void update_forged_pd_cap_from_init_data(test_init_data_t *init_data, sel4utils_process_t *test_process);

/**
 * Only used for starting the test process, maps the init data
 * into the test process vspace
 * (XXX) Arya: Ideally we use something better
 *
 * @param test_vspace The test process vspace
 * Returns the address where init data was mapped
 */
void *get_osmosis_pd_init_data(vspace_t *test_vspace);

/**
 * @brief Insert a new resource manager into the resource manager registry Linked List.
 * Returns a new ID assigned to the resource manager
 *
 * @param new_node
 */
int pd_component_resource_manager_insert(pd_component_resource_manager_entry_t *new_node);

/**
 * @brief Lookup the client registry entry for the given badge.
 *
 * @param badge
 * @return pd_component_registry_entry_t*
 */
pd_component_registry_entry_t *pd_component_registry_get_entry_by_badge(seL4_Word badge);

/**
 * @brief Lookup the resource server registry entry for the given object id.
 * (XXX) Arya: This needs to be exposed for pd_obj to use it. Is there a better way?
 *
 * @param object_id
 * @return pd_component_resource_manager_entry_t*
 */
pd_component_resource_manager_entry_t *pd_component_resource_manager_get_entry_by_id(seL4_Word manager_id);

osmosis_pd_cap_t *pd_add_resource_by_id(uint32_t client_id, gpi_cap_t cap_type, uint32_t res_id);

pd_component_registry_entry_t *pd_component_registry_get_entry_by_id(seL4_Word object_id);