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

/* IPC Message register values for SSMSGREG_FUNC */
enum ep_component_funcs
{
    EP_FUNC_CONNECT_REQ = 0,
    EP_FUNC_CONNECT_ACK,
};

/* Designated purposes of each message register in the mini-protocol. */
enum ep_component_msgregs
{
    /* These four are fixed headers in every serserv message. */
    EPMSGREG_FUNC = 0,
    /* This is a convenience label for IPC MessageInfo length. */
    EPMSGREG_LABEL0,

    /* Connect / New */
    EPMSGREG_CONNECT_REQ_END = EPMSGREG_LABEL0,

    EPMSGREG_CONNECT_ACK_SLOT = EPMSGREG_LABEL0,
    EPMSGREG_CONNECT_ACK_RAW_EP,
    EPMSGREG_CONNECT_ACK_END,
};

/**
 * @brief Component metadata for an endpoint, essential just a wrapper around the VKA object
 */
typedef struct _ep
{
    uint32_t id;
    seL4_CPtr endpoint_in_PD; ///< the EP in the client PD's CSpace, the RT does not have a cap to this EP,
                              ///< but it was allocated from the RT's memory pool
    seL4_Word alloc_cookie;   ///< the allocation cookie for freeing the EP from RT's memory pool
} ep_t;

/**
 * @brief Registry for endpoints
 */
typedef struct _ep_component_registry_entry
{
    resource_server_registry_node_t gen;
    ep_t ep;
} ep_component_registry_entry_t;

/* Global server instance accessor functions. */
resource_component_context_t *get_ep_component(void);

/**
 * To initialize the ep component at the beginning of execution
 */
int ep_component_initialize(simple_t *server_simple,
                            vka_t *server_vka,
                            seL4_CPtr server_cspace,
                            vspace_t *server_vspace,
                            sel4utils_thread_t server_thread,
                            vka_object_t server_ep_obj);

