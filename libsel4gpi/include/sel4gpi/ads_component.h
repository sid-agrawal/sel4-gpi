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

#define ADSSERVS "ADSServ Component: "
#define ADSSERVC "ADSServ Client   : "

// This needs to be removed.
#define ADS_SERVER_BADGE_VALUE_EMPTY (0)
#define ADS_SERVER_BADGE_PARENT_VALUE (0xDEADBEEF)

/* IPC values returned in the "label" message header. */
enum ads_component_errors
{
    ADS_SERVER_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    ADS_SERVER_ERROR_BIND_FAILED = seL4_NumErrors,
    ADS_SERVER_ERROR_UNKNOWN
};

/* IPC Message register values for SSMSGREG_FUNC */
enum ads_component_funcs
{
    ADS_FUNC_ATTACH_REQ = 0,
    ADS_FUNC_ATTACH_ACK,

    ADS_FUNC_SHALLOW_COPY_REQ,
    ADS_FUNC_SHALLOW_COPY_ACK,

    ADS_FUNC_RM_REQ,
    ADS_FUNC_RM_ACK,

    ADS_FUNC_ATTACH_CPU_REQ,
    ADS_FUNC_ATTACH_CPU_ACK,

    ADS_FUNC_DISCONNECT_REQ,
    ADS_FUNC_DISCONNECT_ACK,

    ADS_FUNC_TESTING_REQ,
    ADS_FUNC_TESTING_ACK,

    ADS_FUNC_GET_RR_REQ,
    ADS_FUNC_GET_RR_ACK,

    ADS_FUNC_LOAD_ELF_REQ,
    ADS_FUNC_LOAD_ELF_ACK
};

/* Designated purposes of each message register in the mini-protocol. */
enum ads_component_msgregs
{
    /* These four are fixed headers in every serserv message. */
    ADSMSGREG_FUNC = 0,
    /* This is a convenience label for IPC MessageInfo length. */
    ADSMSGREG_LABEL0,

    /* Connect / New */
    ADSMSGREG_CONNECT_REQ_END = ADSMSGREG_LABEL0,

    ADSMSGREG_CONNECT_ACK_ADS_NS = ADSMSGREG_LABEL0,
    ADSMSGREG_CONNECT_ACK_END,

    /* Get ID */
    ADSMSGREG_GETID_REQ_END = ADSMSGREG_LABEL0,

    ADSMSGREG_GETID_ACK_ID = ADSMSGREG_LABEL0,
    ADSMSGREG_GETID_ACK_END,

    /* Server Spawn */
    ADSMSGREG_SPAWN_SYNC_REQ_END = ADSMSGREG_LABEL0,

    ADSMSGREG_SPAWN_SYNC_ACK_END = ADSMSGREG_LABEL0,

    /* Attach */
    ADSMSGREG_ATTACH_REQ_VA = ADSMSGREG_LABEL0,
    ADSMSGREG_ATTACH_REQ_TYPE,
    ADSMSGREG_ATTACH_REQ_END,

    ADSMSGREG_ATTACH_ACK_VA = ADSMSGREG_LABEL0,
    ADSMSGREG_ATTACH_ACK_END,

    /* Shallow Copy */
    ADSMSGREG_SHALLOW_COPY_REQ_OMIT_VA = ADSMSGREG_LABEL0,
    ADSMSGREG_SHALLOW_COPY_REQ_END,

    ADSMSGREG_SHALLOW_COPY_ACK_END = ADSMSGREG_LABEL0,

    /* Remove */
    ADSMSGREG_RM_REQ_VA = ADSMSGREG_LABEL0,
    ADSMSGREG_RM_REQ_END,

    ADSMSGREG_RM_ACK_END = ADSMSGREG_LABEL0,

    /* Testing */
    ADSMSGREG_TESTING_REQ_END = ADSMSGREG_LABEL0,

    ADSMSGREG_TESTING_ACK_END = ADSMSGREG_LABEL0,

    /* Get RR */
    ADSMSGREG_GET_RR_REQ_BUF_VA = ADSMSGREG_LABEL0,
    ADSMSGREG_GET_RR_REQ_BUF_SZ,
    ADSMSGREG_GET_RR_REQ_END,

    ADSMSGREG_GET_RR_ACK_END = ADSMSGREG_LABEL0,

    /* Bind to CPU */
    ADSMSGREG_BIND_CPU_REQ_END = ADSMSGREG_LABEL0,

    ADSMSGREG_BIND_CPU_ACK_END = ADSMSGREG_LABEL0,

    /* Disconnect / Delete*/
    ADSMSGREG_DISCONNECT_REQ_END = ADSMSGREG_LABEL0,

    ADSMSGREG_DISCONNECT_ACK_END = ADSMSGREG_LABEL0,

    ADSMSGREG_LOAD_ELF_REQ_IMAGE = ADSMSGREG_LABEL0,
    ADSMSGREG_LOAD_ELF_REQ_END,

    ADSMSGREG_LOAD_ELF_ACK_END = ADSMSGREG_LABEL0,
};

/* Per-client context maintained by the server. */
typedef struct _ads_component_registry_entry
{
    ads_t ads;
    struct _ads_component_registry_entry *next;
} ads_component_registry_entry_t;

/* State maintained by the server. */
typedef struct _ads_component_context
{
    simple_t *server_simple;
    vka_t *server_vka;
    seL4_CPtr server_cspace;
    vspace_t *server_vspace;
    sel4utils_thread_t server_thread;

    // The server listens on this endpoint.
    vka_object_t server_ep_obj;

    int registry_n_entries;
    ads_component_registry_entry_t *client_registry;
} ads_component_context_t;

/**
 * Internal library function: acts as the main() for the server thread.
 **/
void ads_component_handle(seL4_MessageInfo_t tag,
                          seL4_Word badge,
                          cspacepath_t *received_cap,
                          seL4_MessageInfo_t *reply_tag);

/**
 * @brief handles an allocation request to create an entirely new ADS
 *
 * @param sender_badge
 * @param reply_tag
 */
void ads_handle_allocation_request(seL4_MessageInfo_t tag, seL4_Word sender_badge, cspacepath_t *received_cap, seL4_MessageInfo_t *reply_tag);

/* Global server instance accessor functions. */
ads_component_context_t *get_ads_component(void);

/**
 * @brief Given a vspace_t insert it into the ADS server's metadata and return a cap to it.
 *
 * @param vspace The vspace to insert.
 * @param vka The vka instance to use for allocating the cap.
 * @param cap_ret The cap to the vspace.
 * @return int
 */
int forge_ads_cap_from_vspace(vspace_t *vspace, vka_t *vka, uint32_t client_pd_id, seL4_CPtr *cap_ret, uint32_t *ads_obj_id_ret);

ads_component_registry_entry_t *ads_component_registry_get_entry_by_badge(seL4_Word badge);

ads_component_registry_entry_t *ads_component_registry_get_entry_by_id(seL4_Word object_ID);

/**
 * Attach an MO to an ADS by ID
 * Note: Only useable from the root task
 *       This is needed since the root task cannot send IPCs to itself
 *
 * @param ads_id ID of the ADS to attach to
 * @param mo_id ID of the MO to attach
 * @param vaddr Requested vaddr to attach at, or NULL
 * @param ret_vaddr Returns the attached vaddr
 */
int ads_component_attach(uint64_t ads_id, uint64_t mo_id, void *vaddr, void **ret_vaddr);