#pragma once

#include <stdint.h>

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>
#include <sel4utils/process.h>

#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/resource_registry.h>
#include <sel4gpi/gpi_rpc.h>

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
    resource_registry_node_t gen;
    resource_component_object_t object;
} resource_component_registry_entry_t;

/**
 * Generic resource component context
 * For components in the root task
 */
typedef struct _resource_component_context
{
    gpi_cap_t resource_type; ///< The type of resoruce this component serves

    void (*request_handler)(     ///< Callback to serve requests
        void *rpc_msg,           ///< The incoming RPC message
        seL4_Word badge,         ///< Sender badge
        seL4_CPtr cap,           ///< Received cap
        void *rpc_reply,         ///< A buffer to fill with the reply RPC message
        bool *need_new_recv_cap, ///< Handler should set to true if it used the receive cap (default false)
        bool *should_reply);     ///< Handler should set to false if no reply should be sent (default true)

    int (*new_obj)(                       ///< Callback to allocate a new obj
        resource_component_object_t *obj, ///< The new generic object
        vka_t *server_vka,                ///< Server's vka
        vspace_t *server_vspace,          ///< Server's vspace
        void *arg0);                      ///< Optional argument

    uint64_t space_id;                   //< Component's default resource space ID
    resource_registry_t registry; ///< Registry of the component's resources
    size_t reg_entry_size;               ///< Size in bits of a registry entry

    vka_t *server_vka;
    vspace_t *server_vspace;

    seL4_CPtr server_ep; ///< The component listens on this endpoint.
    seL4_CPtr mcs_reply; ///< Unused

    sel4gpi_rpc_env_t rpc_env; ///< Stores the message descriptions for RPC messages to this component
} resource_component_context_t;

/**
 * To initialize the component at the beginning of execution
 *
 * @param component the component to initialize
 * @param resource_type the type of resource served by the component
 * @param space_id ID of the resource space this component manages
 * @param request_handler handler for requests to this component, see resource_component_context_t
 * @param new_obj function called to allocate a new object from the component, see resource_component_context_t
 * @param on_registry_delete function to be called when registry entry is deleted
 * @param reg_entry_size the size of a registry entry for this component
 * @param server_vka the vka to use for the component's operations,
 *                   should be configured to the cspace where the componenent is running
 * @param server_vspace the vspace of the context where the component is running
 * @param server_ep the endpoint that this component listens on
 * @param
 */
int resource_component_initialize(
    resource_component_context_t *component,
    gpi_cap_t resource_type,
    uint64_t space_id,
    void (*request_handler)(void *, seL4_Word, seL4_CPtr, void *, bool *, bool *),
    int (*new_obj)(resource_component_object_t *, vka_t *, vspace_t *, void *),
    void (*on_registry_delete)(resource_registry_node_t *, void *),
    size_t reg_entry_size,
    vka_t *server_vka,
    vspace_t *server_vspace,
    seL4_CPtr server_ep,
    pb_msgdesc_t *request_msgdesc,
    pb_msgdesc_t *reply_msgdesc);

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
 * @param object_id ID to use for the object, or BADGE_OBJ_ID_NULL to allocate a new ID
 * @param forge if true, does not allocate a new object, only the badge and registry entry
 * @param arg0 optional, passed as last argument to the new_obj function
 * @param ret_entry returns the new registry entry for the object
 * @param ret_cap returns the new badged endpoint for the object
 *                if NULL, does not make a badged endpoint
 */
int resource_component_allocate(resource_component_context_t *component,
                                uint64_t client_id,
                                uint64_t object_id,
                                bool forge,
                                void *arg0,
                                resource_registry_node_t **ret_entry,
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

/**
 * Debug function to print the existing resources in a resource component
 */
void resource_component_debug_print(resource_component_context_t *component);

/**
 * @brief Transfers a cap between the CSpaces in src_vka and dst_vka
 *
 * @param src_vka vka for the source endpoint
 * @param dst_vka vka for the destination (or NULL, to use src vka for destination)
 * @param src_ep source endpoint to badge
 * @param dest empty cspacepath_t to fill in with destination slot
 * @param mint if true, the cap being transfered is an endpoint cap, and will be minted rather than copied
 * @param badge OPTIONAL a badge to mint onto the endpoint cap - only applies if mint = true,
 * @return int
 */
int resource_component_transfer_cap(vka_t *src_vka,
                                    vka_t *dst_vka,
                                    seL4_CPtr src_ep,
                                    cspacepath_t *dest,
                                    bool mint,
                                    seL4_Word badge);

/**
 * Creates a badged version of an endpoint with a custom badge given
 * Used for an arbitrary non-OSmosis endpoint
 *
 * @param src_vka vka for the source endpoint
 * @param dst_vka vka for the destination (or NULL, to use src vka for destination)
 * @param src_ep source endpoint to badge
 * @param custom_badge a custom given badge
 * @return the new resource's EP cap, null cap if failed
 */
seL4_CPtr resource_component_make_badged_ep_custom(vka_t *src_vka, vka_t *dst_vka, seL4_CPtr src_ep, seL4_Word custom_badge);

/**
 * Creates a badged version of an endpoint for a particular resource
 *
 * @param src_vka vka for the source endpoint
 * @param dst_vka vka for the destination (or NULL, to use src vka for destination)
 * @param src_ep source endpoint to badge
 * @param resource_type type of resource
 * @param space_id ID of the resource space
 * @param res_id ID of the resource, unique to the resource space
 * @param client_id client the badge is meant for
 * @return the new resource's EP cap in the CSpace managed by dst_vka (or src_vka if dst_vka is NULL)
 */
seL4_CPtr resource_component_make_badged_ep(vka_t *src_vka, vka_t *dst_vka, seL4_CPtr src_ep,
                                            gpi_cap_t resource_type, uint64_t space_id, uint64_t res_id, uint64_t client_id);