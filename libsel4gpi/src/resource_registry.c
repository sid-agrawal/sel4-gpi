#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>

#include <sel4gpi/gpi_server.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/resource_registry.h>
#include <sel4gpi/error_handle.h>

/* --- Functions for managing a registry --- */

void resource_registry_initialize(resource_registry_t *registry,
                                         void (*on_delete)(resource_registry_node_t *, void *),
                                         void *on_delete_arg,
                                         uint64_t max_object_id)
{
    assert(registry != NULL);

    registry->head = NULL;
    registry->on_delete = on_delete;
    registry->on_delete_arg = on_delete_arg;
    registry->id_counter = 0;
    registry->max_object_id = max_object_id;
}

void resource_registry_insert(resource_registry_t *registry, resource_registry_node_t *node)
{
    assert(registry != NULL);
    assert(node != NULL);

    node->count = 1;
    assert(node->object_id <= registry->max_object_id);
    HASH_ADD(hh, registry->head, object_id, sizeof(node->object_id), node);
}

resource_registry_node_t *resource_registry_get_by_id(resource_registry_t *registry, uint64_t object_id)
{
    assert(registry != NULL);

    resource_registry_node_t *node;
    HASH_FIND(hh, registry->head, &object_id, sizeof(object_id), node);
    return node;
}

resource_registry_node_t *resource_registry_get_by_badge(resource_registry_t *registry, seL4_Word badge)
{
    assert(registry != NULL);

    resource_registry_get_by_id(registry, get_object_id_from_badge(badge));
}

void resource_registry_delete(resource_registry_t *registry, resource_registry_node_t *node)
{
    assert(registry != NULL);
    assert(node != NULL);

    if (registry->on_delete)
    {
        registry->on_delete(node, registry->on_delete_arg);
    }

    HASH_DEL(registry->head, node);
    free(node);
}

void resource_registry_inc(resource_registry_t *registry, resource_registry_node_t *node)
{
    assert(registry != NULL);
    assert(node != NULL);

    node->count++;
}

void resource_registry_dec(resource_registry_t *registry, resource_registry_node_t *node)
{
    assert(registry != NULL);
    assert(node != NULL);

    node->count--;

    if (node->count == 0)
    {
        resource_registry_delete(registry, node);
    }
}

uint64_t resource_registry_insert_new_id(resource_registry_t *registry, resource_registry_node_t *node)
{
    assert(registry != NULL);
    assert(node != NULL);
    
    // Find a free ID
    resource_registry_node_t *test_node = NULL;
    uint64_t new_id, start_id = registry->id_counter;

    do
    {
        registry->id_counter++;
        new_id = registry->id_counter;

        if (new_id > registry->max_object_id) {
            // We reached the maximum ID, wrap around to zero and check for a free ID
            new_id = registry->id_counter = 1;
        } else if (new_id == start_id) {
            // We have checked every possible ID
            gpi_panic("Out of IDs for resource server registry", registry->max_object_id);
        }

        HASH_FIND(hh, registry->head, &new_id, sizeof(new_id), test_node);

        // While a node exists with this ID, try another
    } while (test_node != NULL);

    node->object_id = new_id;
    resource_registry_insert(registry, node);
    return new_id;
}