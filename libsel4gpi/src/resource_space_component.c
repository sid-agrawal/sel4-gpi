/**
 * @file resource_space_component.c
 * @author Arya Stevinson (arya.stevinson@gmail.com)
 * @brief Implements the resource space server API
 * @version 0.1
 * @date 2024-05-15
 *
 * @copyright Copyright (c) 2024
 */

#include <autoconf.h>

#include <stdio.h>
#include <string.h>

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vka/capops.h>

#include <sel4gpi/gpi_server.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/linked_list.h>
#include <sel4gpi/resource_space_component.h>

// Defined for utility printing macros
#define DEBUG_ID RESSPC_DEBUG
#define SERVER_ID RESSPC_SERVS

resource_component_context_t *get_resspc_component(void)
{
    return &get_gpi_server()->resspc_component;
}

// Called when an item from the MO registry is deleted
static void on_resspc_registry_delete(resource_server_registry_node_t *node_gen, void *arg)
{
    int error = 0;

    resspc_component_registry_entry_t *node = (resspc_component_registry_entry_t *)node_gen;

    OSDB_PRINTF("Destroying resource space (%d)\n", node->space.id);

    // Cleanup PDs according to cleanup policy
    error = pd_component_space_cleanup(node->space.resource_type, node->space.id);
    SERVER_GOTO_IF_ERR(error, "failed to cleanup PDs for deleted resource space (%d)\n", node->space.id);

    return;

err_goto:
    OSDB_PRINTERR("Failed to delete resource space (%d)\n", node->space.id);
}

static seL4_MessageInfo_t handle_resspc_allocation_request(seL4_Word sender_badge, seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got resource space allocation request from client badge %lx.\n", sender_badge);
    int error = 0;
    seL4_MessageInfo_t reply_tag;

    uint64_t caller_id = get_client_id_from_badge(sender_badge);
    uint64_t client_id = seL4_GetMR(RESSPCMSGREG_CONNECT_REQ_CLIENT_ID);

    // Find the resource server PD
    pd_component_registry_entry_t *server_pd = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), caller_id);
    SERVER_GOTO_IF_COND(server_pd == NULL, "Couldn't find resource server PD (%ld)\n", caller_id);

    // Find the client PD (to receive the resource space RDE)
    pd_component_registry_entry_t *client_pd = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), client_id);
    SERVER_GOTO_IF_COND(client_pd == NULL, "Couldn't find client PD (%ld)\n", client_id);

    // Attach the MO containing resource type name
    uint64_t mo_id = get_object_id_from_badge(seL4_GetBadge(1));

    char *resource_type_name;
    error = ads_component_attach_to_rt(mo_id, (void *)&resource_type_name);
    SERVER_GOTO_IF_ERR(error, "Failed to attach MO from client to root task\n");

    // Get the resource type code
    gpi_cap_t type = get_resource_type_code(resource_type_name);

    OSDB_PRINTF("Creating resource space for type (%s, %d).\n", resource_type_name, type);

    // Allocate the resource space
    resspc_component_registry_entry_t *space_entry;
    seL4_CPtr space_cap;

    resspc_config_t resspc_config = {
        .type = type,
        .ep = received_cap,
        .pd = &server_pd->pd};

    error = resource_component_allocate(get_resspc_component(), server_pd->pd.id, BADGE_OBJ_ID_NULL,
                                        false, (void *)&resspc_config,
                                        (resource_server_registry_node_t **)&space_entry, &space_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate a new resource space\n");

    OSDB_PRINTF("Registered resource space, server cap is at %ld, ID: %ld.\n",
                space_entry->space.server_ep, space_entry->gen.object_id);

    uint64_t space_id = space_entry->gen.object_id;

    // Add the RDE to the client PD
    rde_type_t rde_type = {
        .type = type,
    };
    error = pd_add_rde(&client_pd->pd, rde_type, resource_type_name, space_id, received_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to add RDE to new resource space\n");

    // Add the type name to the server PD
    pd_add_type_name(&server_pd->pd, rde_type, resource_type_name);

    // Remove the MO
    error = ads_component_remove_from_rt((void *)resource_type_name);
    SERVER_GOTO_IF_ERR(error, "Failed to remove MO from root task\n");

    seL4_SetMR(PDMSGREG_FUNC, RESSPC_FUNC_CONNECT_ACK);
    seL4_SetMR(RESSPCMSGREG_CONNECT_ACK_ID, space_id);
    seL4_SetMR(RESSPCMSGREG_CONNECT_ACK_TYPE, type);
    seL4_SetMR(RESSPCMSGREG_CONNECT_ACK_SLOT, space_cap);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, RESSPC_FUNC_CONNECT_ACK);
    reply_tag = seL4_MessageInfo_new(error, 0, 0,
                                     RESSPCMSGREG_CONNECT_ACK_END);
    return reply_tag;
}

int resspc_component_mark_delete(uint64_t space_id)
{
    int error = 0;

    // Find the resource space
    resspc_component_registry_entry_t *space_entry = (resspc_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_resspc_component(), space_id);
    SERVER_GOTO_IF_COND(space_entry == NULL, "Couldn't find resource space (%ld)\n", space_id);

    // (XXX) Arya: We can't clean up the server endpoint because it might be used for another resource space
    // This needs some more thought
#if 0
    // Cancel badged sends for this resspc
    cspacepath_t server_ep_path;
    pd_make_path(node->space.pd, node->space.server_ep, &server_ep_path);
    error = seL4_CNode_CancelBadgedSends(server_ep_path.root, server_ep_path.capPtr, server_ep_path.capDepth);
    SERVER_GOTO_IF_ERR(error, "Failed to cancel badged sends on resource space (%d)\n", space_id);

    // Revoke the server EP
    error = vka_cnode_revoke(&server_ep_path);
    SERVER_GOTO_IF_ERR(error, "Failed to revoke endpoint for resource space (%d)\n", space_id);

    // Delete the server EP
    error = vka_cnode_delete(&server_ep_path);
    SERVER_GOTO_IF_ERR(error, "Failed to revoke endpoint for resource space (%d)\n", space_id);
#endif

    // Mark it for deletion
    space_entry->space.deleted = true;

err_goto:
    return error;
}

int resspc_component_sweep(void)
{
    // Find any spaces marked for deletion, then delete them
    resource_server_registry_node_t *curr, *tmp;
    HASH_ITER(hh, get_resspc_component()->registry.head, curr, tmp)
    {
        if (((resspc_component_registry_entry_t *)curr)->space.deleted)
        {
            resource_server_registry_delete(&get_resspc_component()->registry, curr);
        }
    }
}

static seL4_MessageInfo_t handle_create_resource_request(seL4_Word sender_badge)
{
    OSDB_PRINTF("Got create resource request from client badge %lx.\n", sender_badge);
    int error = 0;

    uint64_t client_id = get_client_id_from_badge(sender_badge);
    uint64_t space_id = get_object_id_from_badge(sender_badge);
    uint64_t res_id = seL4_GetMR(RESSPCMSGREG_CREATE_RES_REQ_RES_ID);

    // Find the resource space
    resspc_component_registry_entry_t *space_entry = (resspc_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_resspc_component(), space_id);
    SERVER_GOTO_IF_COND(space_entry == NULL, "Couldn't find resource space (%ld)\n", space_id);

    // Find the resource server PD
    pd_component_registry_entry_t *server_pd = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), client_id);
    SERVER_GOTO_IF_COND(server_pd == NULL, "Couldn't find resource server PD (%ld)\n", client_id);

    gpi_cap_t resource_type = space_entry->space.resource_type;

    OSDB_PRINTF("resource server %ld creates resource in space %ld with ID %ld\n",
                server_pd->pd.id, space_entry->space.id, res_id);

    // Resource does not exist as a cap anywhere yet
    error = pd_add_resource(&server_pd->pd, resource_type, space_entry->space.id, res_id,
                            seL4_CapNull, seL4_CapNull, seL4_CapNull);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, RESSPC_FUNC_CREATE_RES_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  RESSPCMSGREG_CREATE_RES_ACK_END);
    return tag;
}

int resspc_component_map_space(uint64_t src_spc_id, uint64_t dest_spc_id)
{
    int error = 0;

    OSDB_PRINTF("Mapping resource space (%d) to resource space (%d)\n", src_spc_id, dest_spc_id);

    // Find the source resource space
    resspc_component_registry_entry_t *src_space_entry = (resspc_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_resspc_component(), src_spc_id);
    SERVER_GOTO_IF_COND(src_space_entry == NULL, "Couldn't find resource space (%ld)\n", src_spc_id);

    // Find the destination resource space
    resspc_component_registry_entry_t *dst_space_entry = (resspc_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_resspc_component(), dest_spc_id);
    SERVER_GOTO_IF_COND(dst_space_entry == NULL, "Couldn't find resource space (%ld)\n", dest_spc_id);

    // Track the mapping
    linked_list_insert(src_space_entry->space.map_spaces, (void *)&dst_space_entry->space);

err_goto:
    return error;
}

static seL4_MessageInfo_t handle_map_space_request(seL4_Word sender_badge)
{
    OSDB_PRINTF("Got map space request from client badge %lx.\n", sender_badge);
    int error = 0;

    uint64_t src_spc_id = get_object_id_from_badge(sender_badge);
    uint64_t dest_spc_id = seL4_GetMR(RESSPCMSGREG_MAP_SPACE_REQ_SPACE_ID);

    error = resspc_component_map_space(src_spc_id, dest_spc_id);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, RESSPC_FUNC_MAP_SPACE_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  RESSPCMSGREG_MAP_SPACE_ACK_END);
    return tag;
}

int resspc_check_map(uint64_t src_space_id, uint64_t dest_space_id)
{
    int error = 0;

    // Find the source resource space
    resspc_component_registry_entry_t *src_space_entry = (resspc_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_resspc_component(), src_space_id);
    SERVER_GOTO_IF_COND(src_space_entry == NULL, "Couldn't find resource space (%ld)\n", src_space_id);

    // Check the mappings
    for (linked_list_node_t *curr = src_space_entry->space.map_spaces->head; curr != NULL; curr = curr->next)
    {
        if (((res_space_t *)curr->data)->id == dest_space_id)
        {
            return 1;
        }
    }

err_goto:
    return 0;
}

static seL4_MessageInfo_t resspc_component_handle(seL4_MessageInfo_t tag,
                                                  seL4_Word sender_badge,
                                                  seL4_CPtr received_cap,
                                                  bool *need_new_recv_cap)
{
    int error = 0;
    enum mo_component_funcs func = seL4_GetMR(RESSPCMSGREG_FUNC);
    seL4_MessageInfo_t reply_tag;

    if (get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL)
    {
        SERVER_GOTO_IF_COND(func != RESSPC_FUNC_CONNECT_REQ,
                            "Received invalid request on the allocation endpoint\n");
        reply_tag = handle_resspc_allocation_request(sender_badge, received_cap);
        *need_new_recv_cap = true;
    }
    else
    {
        switch (func)
        {
        case RESSPC_FUNC_CREATE_RES_REQ:
            reply_tag = handle_create_resource_request(sender_badge);
            break;
        case RESSPC_FUNC_MAP_SPACE_REQ:
            reply_tag = handle_map_space_request(sender_badge);
            break;
        default:
            SERVER_GOTO_IF_COND(1, "Unknown request received: %d\n", func);
            break;
        }
    }

err_goto:
    reply_tag = seL4_MessageInfo_set_label(reply_tag, error);
    return reply_tag;
}

// Keeping here instead of a separate resource space object file
// since the resource space object does not have much functionality
static int resspc_new(res_space_t *res_space,
                      vka_t *server_vka,
                      vspace_t *server_vspace,
                      resspc_config_t *config)
{
    int error = 0;

    res_space->resource_type = config->type;
    res_space->server_ep = config->ep;
    res_space->pd = config->pd;
    res_space->data = config->data;
    res_space->map_spaces = linked_list_new();
    res_space->deleted = false;

    // (XXX) Arya: todo, allow new type creation

    return error;
}

int resspc_component_initialize(simple_t *server_simple,
                                vka_t *server_vka,
                                seL4_CPtr server_cspace,
                                vspace_t *server_vspace,
                                sel4utils_thread_t server_thread,
                                vka_object_t server_ep_obj)
{
    // Initialize the component
    resource_component_initialize(get_resspc_component(),
                                  GPICAP_TYPE_RESSPC,
                                  RESSPC_SPACE_ID,
                                  resspc_component_handle,
                                  (int (*)(resource_component_object_t *, vka_t *, vspace_t *, void *))resspc_new,
                                  on_resspc_registry_delete,
                                  sizeof(resspc_component_registry_entry_t),
                                  server_simple,
                                  server_vka,
                                  server_cspace,
                                  server_vspace,
                                  server_thread,
                                  server_ep_obj.cptr);

    // Treat the "resource space of resource spaces" as a special registry entry
    resspc_component_registry_entry_t *reg_entry = calloc(1, get_resspc_component()->reg_entry_size);
    assert(reg_entry != 0);

    reg_entry->gen.object_id = RESSPC_SPACE_ID;
    reg_entry->space.id = RESSPC_SPACE_ID;
    reg_entry->space.server_ep = server_ep_obj.cptr;
    resource_server_registry_insert(&get_resspc_component()->registry, (resource_server_registry_node_t *)reg_entry);
}

resspc_component_registry_entry_t *resource_space_get_entry_by_id(seL4_Word space_id)
{
    return (resspc_component_registry_entry_t *)resource_component_registry_get_by_id(get_resspc_component(), space_id);
}