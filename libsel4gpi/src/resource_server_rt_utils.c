#pragma once

#include <sel4gpi/gpi_server.h>
#include <sel4gpi/resource_server_rt_utils.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/pd_component.h>

#define DEBUG_ID GPI_DEBUG
#define SERVER_ID GPISERVS

static void resource_component_reply(resource_component_context_t *component, seL4_MessageInfo_t tag)
{
    api_reply(component->server_thread.reply.cptr, tag);
}

int resource_component_initialize(
    resource_component_context_t *component,
    gpi_cap_t resource_type,
    uint64_t space_id,
    seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr, bool *),
    int (*new_obj)(resource_component_object_t *, vka_t *, vspace_t *, void *),
    void (*on_registry_delete)(resource_server_registry_node_t *, void *),
    size_t reg_entry_size,
    simple_t *server_simple,
    vka_t *server_vka,
    seL4_CPtr server_cspace,
    vspace_t *server_vspace,
    sel4utils_thread_t server_thread,
    seL4_CPtr server_ep)
{
    component->resource_type = resource_type;
    component->space_id = space_id;
    component->request_handler = request_handler;
    component->new_obj = new_obj;
    component->reg_entry_size = reg_entry_size;
    component->server_simple = server_simple;
    component->server_vka = server_vka;
    component->server_cspace = server_cspace;
    component->server_vspace = server_vspace;
    component->server_thread = server_thread;
    component->server_ep = server_ep;

    resource_server_initialize_registry(&component->registry, on_registry_delete, NULL);

    OSDB_PRINTF("Initialized resource component %s\n", cap_type_to_str(resource_type));
}

void resource_component_handle(resource_component_context_t *component,
                               seL4_MessageInfo_t tag,
                               seL4_Word sender_badge,
                               cspacepath_t *received_cap)
{
    OSDB_PRINTF("Resource component handle: %s\n", cap_type_to_str(component->resource_type));

    int error = 0;
    bool needs_new_receive_slot = false;

    // Handle the message
    seL4_MessageInfo_t reply_tag = component->request_handler(tag, sender_badge, received_cap->capPtr, &needs_new_receive_slot);

    // Send the reply
    resource_component_reply(component, reply_tag);

    // Allocate a new receive slot if needed
    if (needs_new_receive_slot)
    {
        error = vka_cspace_alloc_path(component->server_vka, received_cap);
        assert(error == 0);
    }
}

int resource_component_allocate(resource_component_context_t *component,
                                uint64_t client_id,
                                uint64_t object_id,
                                bool forge,
                                void *arg0,
                                resource_server_registry_node_t **ret_entry,
                                seL4_CPtr *ret_cap)
{
    int error = 0;

    /* Create the registry entry */
    resource_component_registry_entry_t *reg_entry = calloc(1, component->reg_entry_size);
    GOTO_IF_COND(reg_entry == NULL, "Couldn't allocate new %s reg entry\n", cap_type_to_str(component->resource_type));

    uint64_t resource_id;
    if (object_id == BADGE_OBJ_ID_NULL)
    {
        resource_id = resource_server_registry_insert_new_id(&component->registry, (resource_server_registry_node_t *)reg_entry);
    }
    else
    {
        resource_id = object_id;
        reg_entry->gen.object_id = object_id;
        resource_server_registry_insert(&component->registry, (resource_server_registry_node_t *)reg_entry);
    }
    *ret_entry = (resource_server_registry_node_t *)reg_entry;
    reg_entry->object.id = resource_id;

    /* Create the object */
    if (!forge)
    {
        error = component->new_obj(&reg_entry->object, component->server_vka, component->server_vspace, arg0);
        GOTO_IF_ERR(error, "Failed to initialize new %s object\n", cap_type_to_str(component->resource_type));
    }

    if (ret_cap != NULL)
    {
        vka_t *client_vka;

        /* Eventually all will use the client vka */
        if (component->resource_type == GPICAP_TYPE_MO || component->resource_type == GPICAP_TYPE_ADS)
        {
            // Find the client PD
            pd_component_registry_entry_t *pd_data = (pd_component_registry_entry_t *)
                resource_component_registry_get_by_id(get_pd_component(), client_id);
            SERVER_GOTO_IF_COND(pd_data == NULL, "Couldn't find PD (%ld)\n", client_id);

            client_vka = pd_data->pd.pd_vka;
        }
        else
        {
            client_vka = component->server_vka;
        }

        /* Create the badged endpoint */
        *ret_cap = resource_server_make_badged_ep(component->server_vka, client_vka, component->server_ep,
                                                  component->resource_type, component->space_id,
                                                  resource_id, client_id);
        GOTO_IF_COND(ret_cap == seL4_CapNull, "Failed to make badged ep for new %s\n",
                     cap_type_to_str(component->resource_type));
        OSDB_PRINTF("Made badged EP for resource space\n");
    }

    // (XXX) Arya:
    // Can't add a resource space resource to the root task since the PD component may not be initialized
    // These hold edges (from root task to core component resource spaces) will be reflected at extraction time
    if (!(client_id == get_gpi_server()->rt_pd_id && component->resource_type == GPICAP_TYPE_RESSPC))
    {
        /* Add the resource to the client */
        seL4_CPtr slot_in_rt = ret_cap ? *ret_cap : seL4_CapNull;
        error = pd_add_resource_by_id(client_id, component->resource_type, component->space_id, resource_id,
                                      slot_in_rt, seL4_CapNull, slot_in_rt);
        GOTO_IF_ERR(error, "Failed to add %s  esource to PD\n", cap_type_to_str(component->resource_type));
    }

err_goto:
    return error;
}

resource_component_registry_entry_t *resource_component_registry_get_by_badge(resource_component_context_t *component,
                                                                              seL4_Word badge)
{
    return (resource_component_registry_entry_t *)resource_server_registry_get_by_badge(&component->registry, badge);
}

resource_component_registry_entry_t *resource_component_registry_get_by_id(resource_component_context_t *component,
                                                                           seL4_Word object_id)
{
    return (resource_component_registry_entry_t *)resource_server_registry_get_by_id(&component->registry, object_id);
}

int resource_component_inc(resource_component_context_t *component,
                           uint64_t object_id)
{
    int error = 0;

    resource_component_registry_entry_t *reg_entry = resource_component_registry_get_by_id(component, object_id);
    GOTO_IF_COND(reg_entry == NULL, "Couldn't find %s (%ld)\n", cap_type_to_str(component->resource_type), object_id);

    OSDB_PRINTF("inc refcount %s (%d), new count %d\n",
                cap_type_to_str(component->resource_type), object_id, reg_entry->gen.count + 1);

    resource_server_registry_inc(&component->registry, (resource_server_registry_node_t *)reg_entry);

err_goto:
    return error;
}

int resource_component_dec(resource_component_context_t *component,
                           uint64_t object_id)
{
    int error = 0;

    resource_component_registry_entry_t *reg_entry = resource_component_registry_get_by_id(component, object_id);
    GOTO_IF_COND(reg_entry == NULL, "Couldn't find %s (%ld)\n", cap_type_to_str(component->resource_type), object_id);

    OSDB_PRINTF("dec refcount %s (%d), new count %d\n",
                cap_type_to_str(component->resource_type), object_id, reg_entry->gen.count - 1);

    resource_server_registry_dec(&component->registry, (resource_server_registry_node_t *)reg_entry);

err_goto:
    return error;
}

void resource_component_debug_print(resource_component_context_t *component)
{
    resource_server_registry_node_t *curr;
    for (curr = component->registry.head; curr != NULL; curr = curr->hh.next)
    {
        printf(" - %s (%d), refcount %d\n", cap_type_to_str(component->resource_type), curr->object_id, curr->count);
    }
}