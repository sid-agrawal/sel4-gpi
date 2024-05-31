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
#include <sel4gpi/resource_server_rt_utils.h>
#include <sel4gpi/resource_space_clientapi.h>

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

    PD_FUNC_CONNECT_REQ = 0,
    PD_FUNC_CONNECT_ACK,

    PD_FUNC_NEXT_SLOT_REQ,
    PD_FUNC_NEXT_SLOT_ACK,

    PD_FUNC_FREE_SLOT_REQ,
    PD_FUNC_FREE_SLOT_ACK,

    PD_FUNC_ALLOC_EP_REQ,
    PD_FUNC_ALLOC_EP_ACK,

    PD_FUNC_START_REQ,
    PD_FUNC_START_ACK,

    PD_FUNC_SENDCAP_REQ,
    PD_FUNC_SENDCAP_ACK,

    PD_FUNC_DUMP_REQ,
    PD_FUNC_DUMP_ACK,

    PD_FUNC_DISCONNECT_REQ,
    PD_FUNC_DISCONNECT_ACK,

    PD_FUNC_SHARE_RDE_REQ,
    PD_FUNC_SHARE_RDE_ACK,

    PD_FUNC_GIVE_RES_REQ,
    PD_FUNC_GIVE_RES_ACK,

    PD_FUNC_MAP_RES_REQ,
    PD_FUNC_MAP_RES_ACK,

    PD_FUNC_EXIT_REQ,

    PD_FUNC_BENCH_IPC_REQ,
    PD_FUNC_BENCH_IPC_ACK,

    PD_FUNC_SETUP_REQ,
    PD_FUNC_SETUP_ACK,

    PD_FUNC_SHARE_RES_TYPE_REQ,
    PD_FUNC_SHARE_RES_TYPE_ACK
};

/* Designated purposes of each message register in the mini-protocol. */
enum pd_component_msgregs
{
    /* These are fixed headers in every pd message. */
    PDMSGREG_FUNC = 0,

    /* This is a convenience label for IPC MessageInfo length. */
    PDMSGREG_LABEL0,

    /* Connect / New */
    PDMSGREG_CONNECT_REQ_TYPE = PDMSGREG_LABEL0,
    PDMSGREG_CONNECT_REQ_END,

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

    /* Send Cap */
    PDMSGREG_SEND_CAP_REQ_IS_CORE = PDMSGREG_LABEL0,
    PDMSGREG_SEND_CAP_REQ_END,

    PDMSGREG_SEND_CAP_PD_SLOT = PDMSGREG_LABEL0,
    PDMSGREG_SEND_CAP_ACK_END,

    /* Dump Cap */
    PDMSGREG_DUMP_REQ_BUF_VA = PDMSGREG_LABEL0,
    PDMSGREG_DUMP_REQ_BUF_SZ,
    PDMSGREG_DUMP_REQ_END,

    PDMSGREG_DUMP_ACK_END = PDMSGREG_LABEL0,

    /* Disconnect / Delete*/
    PDMSGREG_DISCONNECT_REQ_END = PDMSGREG_LABEL0,

    PDMSGREG_DISCONNECT_ACK_END = PDMSGREG_LABEL0,

    /* Share RDE */
    PDMSGREG_SHARE_RDE_REQ_TYPE = PDMSGREG_LABEL0,
    PDMSGREG_SHARE_RDE_REQ_SPACE_ID,
    PDMSGREG_SHARE_RDE_REQ_END,

    PDMSGREG_SHARE_RDE_ACK_END = PDMSGREG_LABEL0,

    /* Give Resource */
    PDMSGREG_GIVE_RES_REQ_SPACE_ID = PDMSGREG_LABEL0,
    PDMSGREG_GIVE_RES_REQ_CLIENT_ID,
    PDMSGREG_GIVE_RES_REQ_RES_ID,
    PDMSGREG_GIVE_RES_REQ_END,

    PDMSGREG_GIVE_RES_ACK_DEST = PDMSGREG_LABEL0,
    PDMSGREG_GIVE_RES_ACK_END,

    /* Map Resource */
    PDMSGREG_MAP_RES_REQ_SRC_ID = PDMSGREG_LABEL0,
    PDMSGREG_MAP_RES_REQ_DEST_ID,
    PDMSGREG_MAP_RES_REQ_END,

    PDMSGREG_MAP_RES_ACK_END = PDMSGREG_LABEL0,

    /* Exit PD */
    PDMSGREG_EXIT_REQ_END = PDMSGREG_LABEL0,

    /* Benchmark IPC */
    PDMSGREG_BENCH_IPC_REQ_CAP_TRANSFER = PDMSGREG_LABEL0,
    PDMSGREG_BENCH_IPC_REQ_END,

    PDMSGREG_BENCH_IPC_ACK_END = PDMSGREG_LABEL0,

    /* PD runtime setup */
    /* (XXX) For now,  we only pass upt to 4 args, which may need fixing */
    PDMSGREG_SETUP_REQ_ARGC = PDMSGREG_LABEL0,
    PDMSGREG_SETUP_REQ_STACK,
    PDMSGREG_SETUP_REQ_ENTRY_POINT,
    PDMSGREG_SETUP_REQ_IPC_BUF,
    PDMSGREG_SETUP_REQ_TYPE,
    PDMSGREG_SETUP_REQ_ARG0,
    PDMSGREG_SETUP_REQ_ARG1,
    PDMSGREG_SETUP_REQ_ARG2,
    PDMSGREG_SETUP_REQ_ARG3,
    PDMSGREG_SETUP_REQ_END,

    PDMSGREG_SETUP_ACK_END = PDMSGREG_LABEL0,

    /* PD share resource type */
    PDMSGREG_SHARE_RES_TYPE_REQ_TYPE = PDMSGREG_LABEL0,
    PDMSGREG_SHARE_RES_TYPE_REQ_END,

    PDMSGREG_SHARE_RES_TYPE_ACK_END = PDMSGREG_LABEL0,
};

enum _pd_setup_type
{
    PD_RUNTIME_SETUP = 1, // sets up the entire C runtime from scratch, writes any arguments onto the stack
    PD_REGISTER_SETUP     // writes a limited number of arguments into registers only
};
typedef enum _pd_setup_type pd_setup_type_t;

// Registry of PDs maintained by the server
typedef struct _pd_component_registry_entry
{
    resource_server_registry_node_t gen;
    pd_t pd;
} pd_component_registry_entry_t;

/**
 * To initialize the pd component at the beginning of execution
 */
int pd_component_initialize(simple_t *server_simple,
                            vka_t *server_vka,
                            seL4_CPtr server_cspace,
                            vspace_t *server_vspace,
                            sel4utils_thread_t server_thread,
                            vka_object_t server_ep_obj);

/* Global server instance accessor functions. */
resource_component_context_t *get_pd_component(void);

// Creates a dummy PD object for the root task
void forge_pd_for_root_task(uint64_t rt_id);

/**
 * Only used to forge the test process' PD cap
 *
 * @param init_data the test driver's init data
 * @param test_process test process struct
 * @param osm_init_data returns the vaddr of the osmosis init data in the test PD
 */
void forge_pd_cap_from_init_data(test_init_data_t *init_data, sel4utils_process_t *test_process, void **osm_init_data);

/**
 * To be called to cleanup a forged test PD object
 */
void destroy_test_pd(void);

/**
 * Add a resource to a PD
 * (XXX) Arya: Exposed for the cpu and mo components. Is there a better way?
 */
int pd_add_resource_by_id(uint32_t pd_id,
                          gpi_cap_t cap_type,
                          uint32_t space_id,
                          uint32_t res_id,
                          seL4_CPtr slot_in_RT,
                          seL4_CPtr slot_in_PD,
                          seL4_CPtr slot_in_serverPD);

/***
 * Map one resource to another
 * (XXX) Arya: At the moment this does nothing but check that the mapping is valid
 * 
 * @param client_pd_id ID of the PD that is requesting the mapping
 * @param src_res_id the universal ID of the source resource
 * @param dest_res_id the universal ID of the destination resource
 * @return 0 on success, error otherwise
*/
int pd_component_map_resources(uint32_t client_pd_id, uint64_t src_res_id, uint64_t dest_res_id);
