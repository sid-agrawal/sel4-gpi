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
#include <sel4gpi/gpi_rpc.h>
#include <resspc_component_rpc.pb.h>

// Defined for utility printing macros
#define DEBUG_ID RESSPC_DEBUG
#define SERVER_ID RESSPC_SERVS

resource_component_context_t *get_resspc_component(void)
{
    return &get_gpi_server()->resspc_component;
}

static int resspc_component_space_cleanup(resspc_component_registry_entry_t *node)
{
    int error = 0;
    int depth = node->space.deletion_depth;

    OSDB_PRINTF("Execute resource space cleanup policy for space (%d), depth %d\n", node->space.id, depth);

    if (GPI_CLEANUP_RESOURCE_SPACE_DEPTH != -1 & depth >= GPI_CLEANUP_RESOURCE_SPACE_DEPTH)
    {
        // Past the maximum depth
        return 0;
    }

    // Iterate over all live resource spaces to check if they should be deleted
    resource_registry_node_t *curr, *tmp;
    HASH_ITER(hh, get_resspc_component()->registry.head, curr, tmp)
    {
        resspc_component_registry_entry_t *space_entry = (resspc_component_registry_entry_t *)curr;

        if (space_entry->space.map_spaces.count == 0 || space_entry->space.to_delete)
        {
            // Skip a space with no map edges, or a space currently being deleted
            continue;
        }

        // Check if we should delete this resource space
        if (GPI_CLEANUP_RESOURCE_SPACE_DEPTH == -1 || depth + 1 <= GPI_CLEANUP_RESOURCE_SPACE_DEPTH)
        {
            // Within resource space deletion depth
            // Check if the space has a map edge for the deleted space
            if (resspc_check_map(space_entry->space.id, node->space.id))
            {
                OSDB_PRINTF("Mark resource space (%d) for deletion due to policy, depth %d\n",
                            space_entry->space.id, depth + 1);
                space_entry->space.deletion_depth = depth + 1;
                space_entry->space.to_delete = true;
                space_entry->space.cleanup_policy = true;
            }
        }
    }

err_goto:
    return error;
}

// Called when an item from the MO registry is deleted
static void on_resspc_registry_delete(resource_registry_node_t *node_gen, void *arg)
{
    int error = 0;

    // Find the resource space
    resspc_component_registry_entry_t *node = (resspc_component_registry_entry_t *)node_gen;
    OSDB_PRINTF("Deleting resource space (%d)\n", node->space.id);
    node->space.deleting = true;

    resource_component_remove_from_rt(get_resspc_component(), node->space.id);

    // We could define a custom cleanup policy here
    if (node->space.cleanup_policy)
    {
        error = resspc_component_space_cleanup(node);
        SERVER_GOTO_IF_ERR(error, "Failed to execute cleanup policy on resource space %d\n", node->space.id);
    }

    // Cleanup the resource space from PDs
    error = pd_component_space_cleanup(node->space.pd_id, node->space.resource_type,
                                       node->space.id, node->space.cleanup_policy);
    SERVER_GOTO_IF_ERR(error, "failed to cleanup PDs for deleted resource space (%d)\n", node->space.id);

    return;

err_goto:
    OSDB_PRINTERR("Error while deleting resource space %d\n", node->space.id);
}

static void handle_resspc_allocation_request(seL4_Word sender_badge,
                                             ResSpcAllocMessage *msg, ResSpcReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got resource space allocation request from client badge %lx.\n", sender_badge);
    int error = 0;
    uint64_t caller_id = get_client_id_from_badge(sender_badge);
    uint64_t client_id = msg->client_id;
    SERVER_GOTO_IF_COND(!sel4gpi_rpc_check_cap(GPICAP_TYPE_EP), "Did not receive EP cap\n");

    // Find the resource server PD
    pd_component_registry_entry_t *server_pd = pd_component_registry_get_entry_by_id(caller_id);
    SERVER_GOTO_IF_COND(server_pd == NULL, "Couldn't find resource server PD (%ld)\n", caller_id);

    // Find the client PD (to receive the resource space RDE)
    pd_component_registry_entry_t *client_pd = pd_component_registry_get_entry_by_id(client_id);
    SERVER_GOTO_IF_COND(client_pd == NULL, "Couldn't find client PD (%ld)\n", client_id);

    ep_component_registry_entry_t *ep_data = (ep_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_ep_component(), seL4_GetBadge(0));
    SERVER_GOTO_IF_COND(ep_data == NULL, "Couldn't find resource server's tracked EP\n");

    // Get the resource type code
    gpi_cap_t type = get_resource_type_code(msg->type_name);

    OSDB_PRINTF("Creating resource space for type (%s, %d).\n", msg->type_name, type);

    // Allocate the resource space
    resspc_component_registry_entry_t *space_entry;
    seL4_CPtr space_cap;

    // We don't add the tracked endpoint handle to this config, because it now represents resource endpoints
    resspc_config_t resspc_config = {
        .type = type,
        .ep = ep_data->ep.endpoint_in_RT.cptr,
        .pd_id = server_pd->pd.id};

    error = resource_component_allocate(get_resspc_component(), server_pd->pd.id, BADGE_OBJ_ID_NULL,
                                        false, (void *)&resspc_config,
                                        (resource_registry_node_t **)&space_entry, &space_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate a new resource space\n");

    OSDB_PRINTF("Registered resource space, server cap is at %ld, ID: %ld.\n",
                space_entry->space.server_ep, space_entry->gen.object_id);

    uint64_t space_id = space_entry->gen.object_id;

    // Add the RDE to the client PD
    rde_type_t rde_type = {
        .type = type,
    };
    error = pd_add_rde(&client_pd->pd, rde_type, msg->type_name, space_id, ep_data->ep.endpoint_in_RT.cptr);
    SERVER_GOTO_IF_ERR(error, "Failed to add RDE to new resource space\n");

    // Add the type name to the server PD
    pd_add_type_name(&server_pd->pd, rde_type, msg->type_name);

    reply_msg->msg.alloc.id = space_id;
    reply_msg->msg.alloc.type_code = type;
    reply_msg->msg.alloc.slot = space_cap;

err_goto:
    reply_msg->which_msg = ResSpcReturnMessage_alloc_tag;
    reply_msg->errorCode = error;
}

int resspc_component_mark_delete(uint64_t space_id, bool execute_cleanup_policy)
{
    int error = 0;

    OSDB_PRINTF("Mark resource space (%ld) for deletion, execute_cleanup_policy (%d) \n",
                space_id, execute_cleanup_policy);

    // Find the resource space
    resspc_component_registry_entry_t *space_entry = resource_space_get_entry_by_id(space_id);
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
    space_entry->space.to_delete = true;
    space_entry->space.cleanup_policy = execute_cleanup_policy;

err_goto:
    return error;
}

int resspc_component_sweep(void)
{
    // Find any spaces marked for deletion, then delete them
    resource_registry_node_t *curr, *tmp;
    HASH_ITER(hh, get_resspc_component()->registry.head, curr, tmp)
    {
        resspc_component_registry_entry_t *entry = (resspc_component_registry_entry_t *)curr;
        if (entry->space.to_delete && !entry->space.deleting)
        {
            resource_registry_delete(&get_resspc_component()->registry, curr);
        }
    }
}

static void handle_create_resource_request(seL4_Word sender_badge,
                                           ResSpcCreateResourceMessage *msg, ResSpcReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got create resource request from client badge %lx.\n", sender_badge);
    int error = 0;

    uint64_t client_id = get_client_id_from_badge(sender_badge);
    uint64_t space_id = get_object_id_from_badge(sender_badge);
    uint64_t res_id = msg->resource_id;

    // Find the resource space
    resspc_component_registry_entry_t *space_entry = resource_space_get_entry_by_id(space_id);
    SERVER_GOTO_IF_COND(space_entry == NULL, "Couldn't find resource space (%ld)\n", space_id);

    // Find the resource server PD
    pd_component_registry_entry_t *server_pd = pd_component_registry_get_entry_by_id(client_id);
    SERVER_GOTO_IF_COND(server_pd == NULL, "Couldn't find resource server PD (%ld)\n", client_id);

    gpi_cap_t resource_type = space_entry->space.resource_type;

    OSDB_PRINTF("resource server %d creates resource in space %d with ID %ld\n",
                server_pd->pd.id, space_entry->space.id, res_id);

    // Resource does not exist as a cap anywhere yet
    error = pd_add_resource(&server_pd->pd,
                            make_res_id(resource_type, space_entry->space.id, res_id),
                            seL4_CapNull, seL4_CapNull, seL4_CapNull);

err_goto:
    reply_msg->which_msg = ResSpcReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_delete_resource_request(seL4_Word sender_badge,
                                           ResSpcDeleteResourceMessage *msg, ResSpcReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got delete resource request from client badge %lx.\n", sender_badge);
    int error = 0;

    uint64_t client_id = get_client_id_from_badge(sender_badge);
    uint64_t space_id = get_object_id_from_badge(sender_badge);
    uint64_t res_id = msg->resource_id;

    // Find the resource space
    resspc_component_registry_entry_t *space_entry = resource_space_get_entry_by_id(space_id);
    SERVER_GOTO_IF_COND(space_entry == NULL, "Couldn't find resource space (%ld)\n", space_id);
    gpi_cap_t resource_type = space_entry->space.resource_type;

    OSDB_PRINTF("resource server %ld deletes resource in space %d with ID %ld\n",
                client_id, space_entry->space.id, res_id);

    // Remove the resource from all PDs
    error = pd_component_resource_cleanup(make_res_id(resource_type, space_entry->space.id, res_id));

err_goto:
    reply_msg->which_msg = ResSpcReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_revoke_resource_request(seL4_Word sender_badge,
                                           ResSpcRevokeResourceMessage *msg, ResSpcReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got delete resource request from client badge %lx.\n", sender_badge);
    int error = 0;

    uint64_t client_id = get_client_id_from_badge(sender_badge);
    uint64_t space_id = get_object_id_from_badge(sender_badge);
    uint64_t res_id = msg->resource_id;

    // Find the resource space
    resspc_component_registry_entry_t *space_entry = resource_space_get_entry_by_id(space_id);
    SERVER_GOTO_IF_COND(space_entry == NULL, "Couldn't find resource space (%ld)\n", space_id);
    gpi_cap_t resource_type = space_entry->space.resource_type;

    // Find the target PD
    pd_component_registry_entry_t *target_pd = pd_component_registry_get_entry_by_id(msg->target_pd_id);
    SERVER_GOTO_IF_COND(target_pd == NULL, "Couldn't find resource server PD (%d)\n", msg->target_pd_id);

    OSDB_PRINTF("resource server %ld revokes resource in space %d with ID %ld from PD (%d)\n",
                client_id, space_entry->space.id, res_id, msg->target_pd_id);

    // Remove the resource from all PDs
    error = pd_remove_resource(&target_pd->pd,
                               make_res_id(resource_type, space_entry->space.id, res_id));

err_goto:
    reply_msg->which_msg = ResSpcReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

int resspc_component_map_space(uint64_t src_spc_id, uint64_t dest_spc_id)
{
    int error = 0;

    OSDB_PRINTF("Mapping resource space (%ld) to resource space (%ld)\n", src_spc_id, dest_spc_id);

    // Find the source resource space
    resspc_component_registry_entry_t *src_space_entry = resource_space_get_entry_by_id(src_spc_id);
    SERVER_GOTO_IF_COND(src_space_entry == NULL, "Couldn't find resource space (%ld)\n", src_spc_id);

    // Find the destination resource space
    resspc_component_registry_entry_t *dst_space_entry = resource_space_get_entry_by_id(dest_spc_id);
    SERVER_GOTO_IF_COND(dst_space_entry == NULL, "Couldn't find resource space (%ld)\n", dest_spc_id);

    // Track the mapping
    linked_list_insert(&src_space_entry->space.map_spaces, (void *)&dst_space_entry->space);

err_goto:
    return error;
}

static void handle_map_space_request(seL4_Word sender_badge,
                                     ResSpcMapMessage *msg, ResSpcReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got map space request from client badge %lx.\n", sender_badge);
    int error = 0;

    uint64_t src_spc_id = get_object_id_from_badge(sender_badge);
    uint64_t dest_spc_id = msg->space_id;

    error = resspc_component_map_space(src_spc_id, dest_spc_id);

err_goto:
    reply_msg->which_msg = ResSpcReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_destroy_space_request(seL4_Word sender_badge,
                                         ResSpcDestroyMessage *msg, ResSpcReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got destroy space request from client badge %lx.\n", sender_badge);
    int error = 0;

    uint64_t spc_id = get_object_id_from_badge(sender_badge);

    // Find the source resource space
    resspc_component_registry_entry_t *src_space_entry = resource_space_get_entry_by_id(spc_id);
    SERVER_GOTO_IF_COND(src_space_entry == NULL, "Couldn't find resource space (%ld)\n", spc_id);

    SERVER_GOTO_IF_COND(src_space_entry->space.to_delete, "Space is already being deleted\n");

    src_space_entry->space.to_delete = true;
    src_space_entry->space.cleanup_policy = false;
    error = resource_component_delete(get_resspc_component(), spc_id);

err_goto:
    reply_msg->which_msg = ResSpcReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

int resspc_check_map(uint64_t src_space_id, uint64_t dest_space_id)
{
    int error = 0;

    // Find the source resource space
    resspc_component_registry_entry_t *src_space_entry = resource_space_get_entry_by_id(src_space_id);
    SERVER_GOTO_IF_COND(src_space_entry == NULL, "Couldn't find resource space (%ld)\n", src_space_id);

    // Check the mappings
    for (linked_list_node_t *curr = src_space_entry->space.map_spaces.head; curr != NULL; curr = curr->next)
    {
        if (((res_space_t *)curr->data)->id == dest_space_id)
        {
            return 1;
        }
    }

err_goto:
    return 0;
}

static void resspc_component_handle(void *msg_p,
                                    seL4_Word sender_badge,
                                    seL4_CPtr received_cap,
                                    void *reply_msg_p,
                                    bool *need_new_recv_cap,
                                    bool *should_reply)
{
    int error = 0;
    ResSpcMessage *msg = (ResSpcMessage *)msg_p;
    ResSpcReturnMessage *reply_msg = (ResSpcReturnMessage *)reply_msg_p;

    if (get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL)
    {
        SERVER_GOTO_IF_COND(msg->which_msg != ResSpcMessage_alloc_tag,
                            "Received invalid request on the allocation endpoint\n");
        handle_resspc_allocation_request(sender_badge, &msg->msg.alloc, reply_msg);
        *need_new_recv_cap = true;
    }
    else
    {
        switch (msg->which_msg)
        {
        case ResSpcMessage_create_resource_tag:
            handle_create_resource_request(sender_badge, &msg->msg.create_resource, reply_msg);
            break;
        case ResSpcMessage_map_tag:
            handle_map_space_request(sender_badge, &msg->msg.map, reply_msg);
            break;
        case ResSpcMessage_destroy_tag:
            handle_destroy_space_request(sender_badge, &msg->msg.destroy, reply_msg);
            break;
        case ResSpcMessage_delete_resource_tag:
            handle_delete_resource_request(sender_badge, &msg->msg.delete_resource, reply_msg);
            break;
        case ResSpcMessage_revoke_resource_tag:
            handle_revoke_resource_request(sender_badge, &msg->msg.revoke_resource, reply_msg);
            break;
        default:
            SERVER_GOTO_IF_COND(1, "Unknown request received: %d\n", msg->which_msg);
            break;
        }
    }

    OSDB_PRINTF("Returning from ResSpc component with error code %d\n", reply_msg->errorCode);
    return;

err_goto:
    OSDB_PRINTF("Returning from ResSpc component with error code %d\n", error);
    reply_msg->errorCode = error;
}

// Keeping here instead of a separate resource space object file
// since the resource space object does not have much functionality
static int resspc_new(res_space_t *res_space,
                      vka_t *server_vka,
                      vspace_t *server_vspace,
                      resspc_config_t *config)
{
    int error = 0;

    memset(&res_space->map_spaces, 0, sizeof(linked_list_t));
    res_space->resource_type = config->type;
    res_space->server_ep = config->ep;
    res_space->pd_id = config->pd_id;
    res_space->data = config->data;
    res_space->to_delete = false;
    res_space->deleting = false;
    res_space->deletion_depth = 0;

    return error;
}

int resspc_component_initialize(vka_t *server_vka,
                                vspace_t *server_vspace,
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
                                  server_vka,
                                  server_vspace,
                                  server_ep_obj.cptr,
                                  &ResSpcMessage_msg,
                                  &ResSpcReturnMessage_msg);

    // Treat the "resource space of resource spaces" as a special registry entry
    resspc_component_registry_entry_t *reg_entry = calloc(1, get_resspc_component()->reg_entry_size);
    assert(reg_entry != 0);

    reg_entry->gen.object_id = RESSPC_SPACE_ID;
    reg_entry->space.id = RESSPC_SPACE_ID;
    reg_entry->space.server_ep = server_ep_obj.cptr;
    resource_registry_insert(&get_resspc_component()->registry, (resource_registry_node_t *)reg_entry);
}

resspc_component_registry_entry_t *resource_space_get_entry_by_id(seL4_Word space_id)
{
    return (resspc_component_registry_entry_t *)resource_component_registry_get_by_id(get_resspc_component(), space_id);
}

gpi_model_node_t *resspc_dump_rr(res_space_t *space, model_state_t *ms, gpi_model_node_t *pd_node)
{
    gpi_model_node_t *root_node = get_root_node(ms);

    // Add the resource space
    gpi_model_node_t *space_node = get_resource_space_node(ms, space->resource_type, space->id);

    if (!space_node)
    {
        space_node = add_resource_space_node(ms, space->resource_type, space->id, false);
    }

    if (!space_node->extracted)
    {
        // Add any map edges
        res_space_t *maps_to;
        char maps_to_id[CSV_MAX_STRING_SIZE];
        for (linked_list_node_t *curr = space->map_spaces.head; curr != NULL; curr = curr->next)
        {
            maps_to = (res_space_t *)curr->data;

            get_resource_space_id(maps_to->resource_type, maps_to->id, maps_to_id);
            add_edge_by_id(ms, GPI_EDGE_TYPE_MAP, space_node->id, maps_to_id);
        }
        space_node->extracted = true;
    }

    return space_node;
}