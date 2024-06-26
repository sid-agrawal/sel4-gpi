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
#include <sel4gpi/resource_component_utils.h>

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
    MOMSGREG_CONNECT_REQ_PADDR,
    MOMSGREG_CONNECT_REQ_PAGE_BITS,
    MOMSGREG_CONNECT_REQ_END,

    MOMSGREG_CONNECT_ACK_ID = MOMSGREG_LABEL0,
    MOMSGREG_CONNECT_ACK_SLOT,
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

/**
 * To initialize the mo component at the beginning of execution
 */
int mo_component_initialize(vka_t *server_vka,
                            vspace_t *server_vspace,
                            vka_object_t server_ep_obj);

/* Global server instance accessor functions. */
resource_component_context_t *get_mo_component(void);

/**
 * @brief forges an MO resource given the list of frames.
 * Since this is currently only used for forging ELF regions, assume that page sizes are 4KB
 * (XXX) Arya: Eventually we should be able to remove this entirely.
 * For now, it is used when forging ADS attachments only.
 *
 * @param frame_caps the frames belonging to the MO
 * @param num_pages total number of pages
 * @param client_pd_id the PD which holds the MO
 * @param cap_ret returns the MO cap
 * @param mo_ref returns the MO handle
 * @return int 0 on success
 */
int forge_mo_cap_from_frames(seL4_CPtr *frame_caps,
                             uint32_t num_pages,
                             uint32_t client_pd_id,
                             seL4_CPtr *cap_ret,
                             mo_t **mo_ref);

/**
 * @brief Allocate an MO for the root task's use
 * 
 * @param num_pages number of pages for the MO
 * @param ret_mo returns the allocated MO
 * @return 0 on success, error otherwise
*/
int mo_component_allocate_rt(int num_pages, mo_t **ret_mo);
