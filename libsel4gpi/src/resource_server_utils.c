#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>

#include <vka/vka.h>
#include <vka/object.h>
#include <vka/capops.h>

#include <sel4gpi/gpi_server.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/error_handle.h>

/* --- Functions for managing a registry --- */

void resource_server_initialize_registry(resource_server_registry_t *registry,
                                         void (*on_delete)(resource_server_registry_node_t *, void *),
                                         void *on_delete_arg)
{
    registry->head = NULL;
    registry->on_delete = on_delete;
    registry->on_delete_arg = on_delete_arg;
    registry->id_counter = 1;
}

void resource_server_registry_insert(resource_server_registry_t *registry, resource_server_registry_node_t *node)
{
    node->count = 1;
    HASH_ADD(hh, registry->head, object_id, sizeof(node->object_id), node);
}

resource_server_registry_node_t *resource_server_registry_get_by_id(resource_server_registry_t *registry, uint64_t object_id)
{
    resource_server_registry_node_t *node;
    HASH_FIND(hh, registry->head, &object_id, sizeof(object_id), node);
    return node;
}

resource_server_registry_node_t *resource_server_registry_get_by_badge(resource_server_registry_t *registry, seL4_Word badge)
{
    resource_server_registry_get_by_id(registry, get_object_id_from_badge(badge));
}

void resource_server_registry_delete(resource_server_registry_t *registry, resource_server_registry_node_t *node)
{
    if (registry->on_delete)
    {
        registry->on_delete(node, registry->on_delete_arg);
    }

    HASH_DEL(registry->head, node);
    free(node);
}

void resource_server_registry_inc(resource_server_registry_t *registry, resource_server_registry_node_t *node)
{
    node->count++;
}

void resource_server_registry_dec(resource_server_registry_t *registry, resource_server_registry_node_t *node)
{
    node->count--;

    if (node->count == 0)
    {
        resource_server_registry_delete(registry, node);
    }
}

uint64_t resource_server_registry_insert_new_id(resource_server_registry_t *registry, resource_server_registry_node_t *node)
{
    // Find a free ID
    uint64_t new_id = registry->id_counter++;

    resource_server_registry_node_t *test_node;
    HASH_FIND(hh, registry->head, &new_id, sizeof(new_id), test_node);

    // While a node exists with this ID, try another
    while (test_node != NULL)
    {
        new_id = registry->id_counter++;

        if (new_id == 0)
        {
            // If ID is zero, we have checked every possible ID
            gpi_panic("Out of IDs for resource server registry", 1);
        }

        HASH_FIND(hh, registry->head, &new_id, sizeof(new_id), test_node);
    }

    node->object_id = new_id;
    resource_server_registry_insert(registry, node);
    return new_id;
}

/** mints a badged endpoint */
static int mint_ep(vka_t *src_vka, vka_t *dst_vka, seL4_CPtr src_ep, cspacepath_t *dest, seL4_Word badge)
{
    cspacepath_t src;
    vka_cspace_make_path(src_vka, src_ep, &src);

    if (dst_vka)
    {
        vka_cspace_alloc_path(dst_vka, dest);
    }
    else
    {
        vka_cspace_alloc_path(src_vka, dest);
    }

    return vka_cnode_mint(dest,
                          &src,
                          seL4_NoRead, // So that recipients of resources cannot receive endpoint messages
                          badge);
}

seL4_CPtr resource_server_make_badged_ep_custom(vka_t *src_vka, vka_t *dst_vka, seL4_CPtr src_ep, seL4_Word custom_badge)
{
    cspacepath_t dest = {0};
    mint_ep(src_vka, dst_vka, src_ep, &dest, custom_badge);
    return dest.capPtr;
}

seL4_CPtr resource_server_make_badged_ep(vka_t *src_vka, vka_t *dst_vka, seL4_CPtr src_ep,
                                         gpi_cap_t resource_type, uint64_t space_id, uint64_t res_id, uint64_t client_id)
{
    int error = 0;

    /* Make the badge */
    seL4_Word badge = gpi_new_badge(resource_type,
                                    0x00,
                                    client_id,
                                    space_id,
                                    res_id);

    assert(badge != 0);

    /* Mint the cap */
    cspacepath_t dest = {0};
    error = mint_ep(src_vka, dst_vka, src_ep, &dest, badge);

    return dest.capPtr;
}