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
#include <sel4gpi/resource_space_component.h>

// Defined for utility printing macros
#define DEBUG_ID RESSPC_DEBUG
#define SERVER_ID RESSPC_SERVS

resource_component_context_t *get_resspc_component(void)
{
    return &get_gpi_server()->resspc_component;
}

// Called when an item from the MO registry is deleted
static void on_resspc_registry_delete(resource_server_registry_node_t *node_gen)
{
    resspc_component_registry_entry_t *node = (resspc_component_registry_entry_t *)node_gen;

    OSDB_PRINTF("Destroying resource space (%d)\n", node->space.id);
    OSDB_PRINTERR("Destroying resource space not implemented\n");
}

static seL4_MessageInfo_t handle_resspc_allocation_request(seL4_Word sender_badge, seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got register server request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), get_client_id_from_badge(sender_badge));

    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find target PD (%ld)\n", get_object_id_from_badge(sender_badge));

    resspc_component_registry_entry_t *space_entry;
    seL4_CPtr space_cap;

    resspc_config_t resspc_config = {
        .type = seL4_GetMR(RESSPCMSGREG_CONNECT_REQ_TYPE),
        .ep = received_cap,
        .pd = &client_data->pd};

    error = resource_component_allocate(get_resspc_component(), 0, false, (void *)&resspc_config, (resource_server_registry_node_t **)&space_entry, &space_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate a new resource space\n");

    OSDB_PRINTF("Registered server, cap is at %ld.\n", space_entry->space.server_ep);

    seL4_SetMR(RESSPCMSGREG_CONNECT_ACK_ID, space_entry->gen.object_id);
    // seL4_SetMR(RESSPCMSGREG_CONNECT_ACK_SLOT, space_entry->gen.object_id);
    seL4_SetCap(0, space_cap);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, RESSPC_FUNC_CONNECT_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  RESSPCMSGREG_CONNECT_ACK_END);
    return tag;
}

static seL4_MessageInfo_t resspc_component_handle(seL4_MessageInfo_t tag,
                                                  seL4_Word sender_badge,
                                                  seL4_CPtr received_cap,
                                                  bool *need_new_recv_cap)
{
    enum mo_component_funcs func = seL4_GetMR(MOMSGREG_FUNC);
    seL4_MessageInfo_t reply_tag;

    if (get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL)
    {
        reply_tag = handle_resspc_allocation_request(sender_badge, received_cap);
        *need_new_recv_cap = true;
    }
    else
    {
        switch (func)
        {
        default:
            gpi_panic(MOSERVS "Unknown func type.", (seL4_Word)func);
            break;
        }
    }

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
    res_space->ns_index = NSID_DEFAULT;
    res_space->pd = config->pd;

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
    resource_component_initialize(get_resspc_component(),
                                  GPICAP_TYPE_RESSPC,
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