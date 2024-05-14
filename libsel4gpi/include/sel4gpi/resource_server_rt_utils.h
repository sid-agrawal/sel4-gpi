#pragma once

#include <stdint.h>

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>
#include <sel4utils/process.h>

#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/resource_space_clientapi.h>
#include <sel4gpi/resource_server_utils.h>

/** @file
 * Utility functions for RT components that serve GPI resources
 */

/**
 * Generic resource component object
 * Used to access the id field
 *
 * All resource component object definitions
 * must match these entries
 */
typedef struct _resource_component_object
{
    uint32_t id;
} resource_component_object_t;

/**
 * Generic resource component registry entry
 * Used to access the object field
 *
 * All resource component registry entry definitions
 * must match these entries
 */
typedef struct _resource_component_registry_entry
{
    resource_server_registry_node_t gen;
    resource_component_object_t object;
} resource_component_registry_entry_t;

/**
 * Generic resource component context
 * For components in the root task
 */
typedef struct _resource_component_context
{
    gpi_cap_t resource_type;

    // Run to serve requests
    seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr, bool *);

    // Run to allocate a new obj
    int (*new_obj)(resource_component_object_t *, vka_t *, vspace_t *);

    // Registry of the component's resources
    resource_server_registry_t registry;
    size_t reg_entry_size;

    // Handles to root task vka, etc.
    simple_t *server_simple;
    vka_t *server_vka;
    seL4_CPtr server_cspace;
    vspace_t *server_vspace;
    sel4utils_thread_t server_thread;

    // The server listens on this endpoint.
    seL4_CPtr server_ep;

    // Other
    seL4_CPtr mcs_reply;
} resource_component_context_t;

/**
 * To initialize the component at the beginning of execution
 *
 * @param component the component to initialize
 * @param resource_type the type of resource served by the component
 * @param request_handler handler for requests to this component
 * @param new_obj function called to allocate a new object from the component
 * @param on_registry_delete function to be called when registry entry is deleted
 * @param reg_entry_size the size of a registry entry for this component
 * @param server_simple
 * @param server_vka
 * @param server_cspace
 * @param server_vspace
 * @param server_thread
 * @param server_ep
 */
int resource_component_initialize(
    resource_component_context_t *component,
    gpi_cap_t resource_type,
    seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr, bool *),
    int (*new_obj)(resource_component_object_t *, vka_t *, vspace_t *),
    void (*on_registry_delete)(resource_server_registry_node_t *),
    size_t reg_entry_size,
    simple_t *server_simple,
    vka_t *server_vka,
    seL4_CPtr server_cspace,
    vspace_t *server_vspace,
    sel4utils_thread_t server_thread,
    seL4_CPtr server_ep);

/**
 * Handle a message to a resource component
 *
 * @param component
 * @param tag the message tag
 * @param sender_badge the message badge
 * @param received_cap path of the received cap
 *                     if the path is used, the slot is replaced
 *                     with a free slot
 */
void resource_component_handle(resource_component_context_t *component,
                               seL4_MessageInfo_t tag,
                               seL4_Word sender_badge,
                               cspacepath_t *received_cap);

/**
 * Allocate a resource from a resource component
 *
 * @param component
 * @param client_id the PD ID of the client requesting the allocation
 * @param forge if true, does not allocate a new object, only the badge and registry entry
 * @param ret_entry returns the new registry entry for the object
 * @param ret_cap returns the new badged endpoint for the object
 */
int resource_component_allocate(resource_component_context_t *component,
                                uint64_t client_id,
                                bool forge,
                                resource_server_registry_node_t **ret_entry,
                                seL4_CPtr *ret_cap);

/**
 * Get a resource registry entry from a resource component
 *
 * @param component
 * @param badge the badge containing object ID to search for in the registry
 * @return the registry entry, or NULL if not found
 */
resource_component_registry_entry_t *resource_component_registry_get_by_badge(resource_component_context_t *component,
                                                                              seL4_Word badge);

/**
 * Get a resource registry entry from a resource component
 *
 * @param component
 * @param object_id object ID to search for in the registry
 * @return the registry entry, or NULL if not found
 */
resource_component_registry_entry_t *resource_component_registry_get_by_id(resource_component_context_t *component,
                                                                           seL4_Word object_id);

/**
 * Increment the reference count to a resource object
 * If the count reaches zero, the object is destroyed
 * 
 * @param component
 * @param object_id ID of the object
*/
int resource_component_inc(resource_component_context_t *component,
                           uint64_t object_id);

/**
 * Decrement the reference count to a resource object
 * If the count reaches zero, the object is destroyed
 * 
 * @param component
 * @param object_id ID of the object
*/
int resource_component_dec(resource_component_context_t *component,
                           uint64_t object_id);