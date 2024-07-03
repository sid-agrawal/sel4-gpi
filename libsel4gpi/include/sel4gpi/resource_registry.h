#pragma once

#include <stdint.h>

#include <utils/uthash.h>
#include <sel4/sel4.h>

/** @file
 * Utility functions for all servers of GPI resources, both in RT and other PDs
 */


/**
 * Generic resource server registry
 * Maintains per-resource metadata
 * Resource servers should 'subclass' the node type
 * by creating a struct:
 *
 * struct my_resource_registry_node {
 *  resource_registry_node_t gen;
 *  <...server specific data here...>
 * }
 */
typedef struct _resource_registry_node
{
    uint64_t object_id; ///< Unique ID within the registry
    uint32_t count;     ///< Reference count to this node

    UT_hash_handle hh;
} resource_registry_node_t;

typedef struct _resource_registry
{
    resource_registry_node_t *head; ///< Hash table of registry nodes
    uint32_t id_counter;                   ///< Next ID to assign for an object in the registry

    void (*on_delete)(resource_registry_node_t *, void *); ///< Function to be called before a node is deleted
                                                                  ///< or NULL
                                                                  ///< Args: node, optional arg
    void *on_delete_arg;                                          ///< Passed as the second argument to on_delete

} resource_registry_t;

/* --- Functions for managing a registry --- */

/**
 * Initialize a registry
 *
 * @param registry the registry to initialize
 * @param on_delete (optional) function to be called before a node is deleted
 * @param on_delete_arg (optional) to pass as the second argument to on_delete
 */
void resource_registry_initialize(resource_registry_t *registry,
                                         void (*on_delete)(resource_registry_node_t *, void *),
                                         void *on_delete_arg);

/**
 * Insert a new node to the registry
 *
 * @param registry
 * @param node the node to insert
 */
void resource_registry_insert(resource_registry_t *registry, resource_registry_node_t *node);

/**
 * Get a node from the registry by the resource ID
 *
 * @param registry
 * @param object_id id of the resource to find the corresponding node
 * @return The corresponding node, or NULL if not found
 */
resource_registry_node_t *resource_registry_get_by_id(resource_registry_t *registry, uint64_t object_id);

/**
 * Get a node from the registry by the gpi badge
 *
 * @param registry
 * @param badge badge of the resource to find the corresponding node
 * @return The corresponding node, or NULL if not found
 */
resource_registry_node_t *resource_registry_get_by_badge(resource_registry_t *registry, seL4_Word badge);

/**
 * Delete a node from the registry
 * Regardless of reference count, the node will be deleted
 *
 * @param registry
 * @param node node to delete
 */
void resource_registry_delete(resource_registry_t *registry, resource_registry_node_t *node);

/**
 * Increment the reference count of a node in the registry
 *
 * @param registry
 * @param node the node to increment
 */
void resource_registry_inc(resource_registry_t *registry, resource_registry_node_t *node);

/**
 * Decrement the reference count of a node in the registry
 * If this reduces reference count to 0, the node will be deleted
 *
 * @param registry
 * @param node the node to decrement
 */
void resource_registry_dec(resource_registry_t *registry, resource_registry_node_t *node);

/**
 * Assign an object id for a new registry entry before inserting
 *
 * @param registry
 * @param node new node to insert and assign an ID to
 * @return the assigned object id of the node
 */
uint64_t resource_registry_insert_new_id(resource_registry_t *registry, resource_registry_node_t *node);