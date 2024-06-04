#pragma once

#include <stdint.h>

#include <utils/uthash.h>
#include <sel4/sel4.h>

/** @file
 * Utility functions for all servers of GPI resources, both in RT and other PDs
 */

#define RESOURCE_SERVER_DEBUG 0
#define SERVER_UTILS "SERVER_UTILS"

// Could use the server's debug function instead
#if RESOURCE_SERVER_DEBUG
#define RESOURCE_SERVER_PRINTF(...)  \
    do                               \
    {                                \
        printf("%s ", SERVER_UTILS); \
        printf(__VA_ARGS__);         \
    } while (0);
#else
#define RESOURCE_SERVER_PRINTF(...)
#endif

/**
 * Generic resource server registry
 * Maintains per-resource metadata
 * Resource servers should 'subclass' the node type
 * by creating a struct:
 *
 * struct my_resource_server_registry_node {
 *  resource_server_registry_node_t gen;
 *  <...server specific data here...>
 * }
 */
typedef struct _resource_server_registry_node
{
    uint64_t object_id; ///< Unique ID within the registry
    uint32_t count;     ///< Reference count to this node

    UT_hash_handle hh;
} resource_server_registry_node_t;

typedef struct _resource_server_registry
{
    resource_server_registry_node_t *head; ///< Hash table of registry nodes
    uint32_t n_entries;                    ///< Number of entries in the hash table

    void (*on_delete)(resource_server_registry_node_t *, void *); ///< Function to be called before a node is deleted
                                                                  ///< or NULL
                                                                  ///< Args: node, optional arg
    void *on_delete_arg;                                          ///< Passed as the second argument to on_delete

} resource_server_registry_t;

/* --- Functions for managing a registry --- */

/**
 * Initialize a registry
 *
 * @param registry the registry to initialize
 * @param on_delete (optional) function to be called before a node is deleted
 * @param on_delete_arg (optional) to pass as the second argument to on_delete
 */
void resource_server_initialize_registry(resource_server_registry_t *registry,
                                         void (*on_delete)(resource_server_registry_node_t *, void *),
                                         void *on_delete_arg);

/**
 * Insert a new node to the registry
 *
 * @param registry
 * @param node the node to insert
 */
void resource_server_registry_insert(resource_server_registry_t *registry, resource_server_registry_node_t *node);

/**
 * Get a node from the registry by the resource ID
 *
 * @param registry
 * @param object_id id of the resource to find the corresponding node
 * @return The corresponding node, or NULL if not found
 */
resource_server_registry_node_t *resource_server_registry_get_by_id(resource_server_registry_t *registry, uint64_t object_id);

/**
 * Get a node from the registry by the gpi badge
 *
 * @param registry
 * @param badge badge of the resource to find the corresponding node
 * @return The corresponding node, or NULL if not found
 */
resource_server_registry_node_t *resource_server_registry_get_by_badge(resource_server_registry_t *registry, seL4_Word badge);

/**
 * Delete a node from the registry
 * Regardless of reference count, the node will be deleted
 *
 * @param registry
 * @param node node to delete
 */
void resource_server_registry_delete(resource_server_registry_t *registry, resource_server_registry_node_t *node);

/**
 * Increment the reference count of a node in the registry
 *
 * @param registry
 * @param node the node to increment
 */
void resource_server_registry_inc(resource_server_registry_t *registry, resource_server_registry_node_t *node);

/**
 * Decrement the reference count of a node in the registry
 * If this reduces reference count to 0, the node will be deleted
 *
 * @param registry
 * @param node the node to decrement
 */
void resource_server_registry_dec(resource_server_registry_t *registry, resource_server_registry_node_t *node);

/**
 * Assign an object id for a new registry entry before inserting
 *
 * @param registry
 * @param node new node to insert and assign an ID to
 * @return the assigned object id of the node
 */
uint64_t resource_server_registry_insert_new_id(resource_server_registry_t *registry, resource_server_registry_node_t *node);

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
 * @return the new resource's EP cap
 */
seL4_CPtr resource_server_make_badged_ep(vka_t *src_vka, vka_t *dst_vka, seL4_CPtr src_ep,
                                         gpi_cap_t resource_type, uint64_t space_id, uint64_t res_id, uint64_t client_id);