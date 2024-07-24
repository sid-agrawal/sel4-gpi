/**
 * @file endpoint_component.c
 * @author Linh Pham (phamhlinh01@gmail.com)
 * @brief Definitions for creating and tracking endpoints.
 *        An "endpoint resource space" is purely an implementation concept, to ref-count
 *        endpoints and clean them up. Endpoint are not resources in the OSmosis model.
 * @version 0.1
 * @date 2024-06-18
 *
 * @copyright Copyright (c) 2024
 *
 */

#pragma once

#include <stdint.h>
#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <simple/simple.h>
#include <vspace/vspace.h>
#include <sel4utils/thread.h>
#include <sel4gpi/resource_space_component.h>

#define EPSERVS "EPServ Component: "
#define EPSERVC "EPServ Client   : "

/**
 * @brief Component metadata for an endpoint, essential just a wrapper around the VKA object
 */
typedef struct _ep
{
    gpi_obj_id_t id;
    vka_object_t endpoint_in_RT; ///< the EP in the RT's CSpace, it exists here
                                 ///< for convenience during transfer and freeing
} ep_t;

/**
 * @brief Registry for endpoints
 */
typedef struct _ep_component_registry_entry
{
    resource_registry_node_t gen;
    ep_t ep;
} ep_component_registry_entry_t;

/* Global server instance accessor functions. */
resource_component_context_t *get_ep_component(void);

/**
 * To initialize the ep component at the beginning of execution
 */
int ep_component_initialize(vka_t *server_vka,
                            vspace_t *server_vspace,
                            vka_object_t server_ep_obj);

/**
 * Allocate an endpoint resource from the root task
 * 
 * @param client_pd ID of the client PD to allocate the endpoint for
 * @param ret_ep_in_PD returns the slot of the raw endpoint in the client PD's cspace
 * @param ret_badged_ep returns the slot of the badged endpoint resource in the client PD's cspace
 * @param ret_ep returns the allocated endpoint object
 */
int ep_component_allocate(gpi_obj_id_t client_pd,
                          seL4_CPtr *ret_ep_in_PD,
                          seL4_CPtr *ret_badged_ep,
                          ep_t **ret_ep);