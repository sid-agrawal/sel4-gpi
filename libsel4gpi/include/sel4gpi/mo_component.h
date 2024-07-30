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

/* Per-client context maintained by the server. */
typedef struct _mo_component_registry_entry
{
    resource_registry_node_t gen;
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
 * This is used when forging ADS attachments to root task only.
 *
 * @param frame_caps the frames belonging to the MO, will be copied
 * @param num_pages total number of pages
 * @param client_pd_id the PD which holds the MO
 * @param cap_ret returns the MO cap
 * @param mo_ref returns the MO handle
 * @return int 0 on success
 */
int forge_mo_cap_from_frames(seL4_CPtr *frame_caps,
                             uint32_t num_pages,
                             gpi_obj_id_t client_pd_id,
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
