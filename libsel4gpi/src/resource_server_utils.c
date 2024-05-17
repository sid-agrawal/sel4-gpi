#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>

#include <vka/vka.h>
#include <vka/object.h>
#include <vka/capops.h>

#include <sel4gpi/pd_utils.h>
#include <sel4gpi/resource_server_utils.h>

/* --- Functions for managing a registry --- */

void resource_server_initialize_registry(resource_server_registry_t *registry, void (*on_delete)(resource_server_registry_node_t *))
{
    registry->head = NULL;
    registry->on_delete = on_delete;
    registry->n_entries = 0;
}

void resource_server_registry_insert(resource_server_registry_t *registry, resource_server_registry_node_t *node)
{
    registry->n_entries++;
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
        registry->on_delete(node);
    }

    HASH_DEL(registry->head, node);
    free(node);
    registry->n_entries--;
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
    uint64_t new_id = registry->n_entries + 1;
    node->object_id = new_id;
    resource_server_registry_insert(registry, node);
    return new_id;
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
    cspacepath_t src, dest;
    vka_cspace_make_path(src_vka, src_ep, &src);

    if (dst_vka)
    {
        vka_cspace_alloc_path(dst_vka, &dest);
    }
    else
    {
        vka_cspace_alloc_path(src_vka, &dest);
    }

    error = vka_cnode_mint(&dest,
                           &src,
                           seL4_AllRights,
                           badge);

    return dest.capPtr;
}