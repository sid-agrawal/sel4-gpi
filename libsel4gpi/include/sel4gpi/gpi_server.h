#pragma once
/**
 * @file gpi_server.h
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief API for a parent to spawn a GPI server.
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <sel4utils/process.h>
#include <sync/mutex.h>
#include <sel4gpi/ads_component.h>
#include <sel4gpi/mo_component.h>
#include <sel4gpi/pd_component.h>
#include <sel4gpi/cpu_component.h>
#include <sel4gpi/cap_tracking.h>
#include <sel4gpi/resource_types.h>
#include <sel4gpi/gpi_options.h>

#define GPI_SERVER_DEFAULT_PRIORITY (seL4_MaxPrio - 1)

#define GPISERVP "GPIServ Parent: "
#define GPISERVS "GPIServ Server: "

#define GPI_SERVER_BADGE_PARENT_VALUE 0xdeadbeef // Change this to something which will not violate the badge range

/** @file API for allowing a thread to act as the parent to a GPI server
 * thread.
 *
 * Provides the APIs for spawning the server thread.
 */

/** Spawns the server thread. Server thread is spawned within the VSpace and
 *  CSpace of the thread that spawned it.
 *
 * CAUTION:
 * All vka_t, vpsace_t, and simple_t instances passed to this library by
 * reference must remain functional throughout the lifetime of the server.
 *
 * @param parent_simple Initialized simple_t for the parent process that is
 *                      spawning the server thread.
 * @param parent_vka Initialized vka_t for the parent process that is spawning
 *                   the server thread.
 * @param parent_vspace Initialized vspace_t for the parent process that is
 *                      spawning the server thread.
 * @param priority Server thread's priority.
 * @param server_endpoint Server thread's endpoint cap.
 * @param mx an initialized and managed mutex for synchronization between the test driver and gpi server
 * @return seL4_Error value.
 */
seL4_Error gpi_server_parent_spawn_thread(simple_t *parent_simple,
                                          vka_t *parent_vka,
                                          vspace_t *parent_vspace,
                                          uint8_t priority,
                                          seL4_CPtr *server_endpoint,
                                          sync_mutex_t *mx);

/*
Context of the server
*/
typedef struct _gpi_server_context
{
    bool is_root; ///< True iff the current process is the root task

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

    /* Context of each component */
    resource_component_context_t ads_component;
    resource_component_context_t mo_component;
    resource_component_context_t cpu_component;
    resource_component_context_t pd_component;
    resource_component_context_t resspc_component;
    resource_component_context_t ep_component;

    /* Track the GPI resource types */
    resource_registry_t resource_types;

    osmosis_cap_t *osm_caps;
    osmosis_cap_t *osm_caps_tail;

    // ID of the root task's PD and ADS
    gpi_obj_id_t rt_pd_id;
    gpi_obj_id_t rt_ads_id;

    /* Track a pending model extraction */
    bool pending_extraction;          ///< True if a model extraction is currently in progress
    model_state_t *model_state;       ///< Partial model state for a pending model extraction
    seL4_CPtr model_extraction_reply; ///< The reply cap for the pending model extraction
    int model_extraction_n_missing;   ///< Number of missing replies before model state is complete

    /* Track a pending PD termination */
    /* (XXX) Arya: For now, we can only do one PD termination at a time */
    bool starting_termination;      ///< True if we are currently setting up a PD termination operation
    bool pending_termination;       ///< True if a PD termination is currently in progress
    seL4_CPtr pd_termination_reply; ///< The reply cap for the pending pd termination
    int pd_termination_n_missing;   ///< Number of missing replies before cleanup is complete

    gpi_obj_id_t test_proc_id; ///< Use this to warn if we try to clean up the test process

    sync_mutex_t *mx; ///< mutex for synchronization between the test driver and GPI server
} gpi_server_context_t;

/**
 * Internal library function: acts as the main() for the server thread.
 **/
void gpi_server_main(void);

gpi_server_context_t *get_gpi_server(void);

/**
 * Used for an unrecoverable fault in the gpi server
 */
void gpi_panic(char *reason, uint64_t code);

/**
 * Debug function prints all core resources in existence
 */
void gpi_debug_print_resources(void);