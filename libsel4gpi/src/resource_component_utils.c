#pragma once

#include <vka/object.h>
#include <vka/capops.h>

#include <sel4gpi/gpi_server.h>
#include <sel4gpi/resource_component_utils.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/pd_component.h>
#include <sel4gpi/gpi_rpc.h>

#define DEBUG_ID GPI_DEBUG
#define SERVER_ID GPISERVS

// Generic buffer size for RPC messages, in bytes
// Must be larger than any RPC message in the system
// We could use the generated Message_size constants instead, if we wanted to be more precise
#define RPC_MSG_MAX_SIZE 256

static void resource_component_reply(resource_component_context_t *component, seL4_MessageInfo_t tag)
{
    api_reply(component->mcs_reply, tag);
}

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
    pb_msgdesc_t *reply_msgdesc)
{
    component->resource_type = resource_type;
    component->space_id = space_id;
    component->request_handler = request_handler;
    component->new_obj = new_obj;
    component->reg_entry_size = reg_entry_size;
    component->server_vka = server_vka;
    component->server_vspace = server_vspace;
    component->server_ep = server_ep;
    sel4gpi_rpc_env_init(&component->rpc_env, request_msgdesc, reply_msgdesc);

    resource_registry_initialize(&component->registry, on_registry_delete, NULL);

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
    bool should_reply = true;

    char rpc_msg_buf[RPC_MSG_MAX_SIZE] = {0};
    char rpc_reply_buf[RPC_MSG_MAX_SIZE] = {0};

    error = sel4gpi_rpc_recv(&component->rpc_env, (void *)rpc_msg_buf);
    assert(error == 0);

    if (MESSAGE_DEBUG_ENABLED) {
        printf("Message to %s component: \n", cap_type_to_str(component->resource_type));
        sel4gpi_rpc_print_request(&component->rpc_env, (void *)rpc_msg_buf);
    }

    // Handle the message
    component->request_handler(
        (void *)rpc_msg_buf,
        sender_badge,
        received_cap->capPtr,
        (void *)rpc_reply_buf,
        &needs_new_receive_slot,
        &should_reply);

    // Send the reply
    if (should_reply)
    {
        seL4_MessageInfo_t reply_tag;
        error = sel4gpi_rpc_reply(&component->rpc_env, (void *)rpc_reply_buf, &reply_tag);
        assert(error == 0);
        resource_component_reply(component, reply_tag);
    }

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
                                resource_registry_node_t **ret_entry,
                                seL4_CPtr *ret_cap)
{
    int error = 0;

    /* Create the registry entry */
    resource_component_registry_entry_t *reg_entry = calloc(1, component->reg_entry_size);
    GOTO_IF_COND(reg_entry == NULL, "Couldn't allocate new %s reg entry\n", cap_type_to_str(component->resource_type));

    uint64_t resource_id;
    if (object_id == BADGE_OBJ_ID_NULL)
    {
        resource_id = resource_registry_insert_new_id(&component->registry, (resource_registry_node_t *)reg_entry);
    }
    else
    {
        resource_id = object_id;
        reg_entry->gen.object_id = object_id;
        resource_registry_insert(&component->registry, (resource_registry_node_t *)reg_entry);
    }
    *ret_entry = (resource_registry_node_t *)reg_entry;
    reg_entry->object.id = resource_id;

    /* Create the object */
    if (!forge)
    {
        error = component->new_obj(&reg_entry->object, component->server_vka, component->server_vspace, arg0);
        GOTO_IF_ERR(error, "Failed to initialize new %s object\n", cap_type_to_str(component->resource_type));
    }

    if (ret_cap != NULL)
    {
        // Find the client PD
        pd_component_registry_entry_t *pd_data = pd_component_registry_get_entry_by_id(client_id);
        SERVER_GOTO_IF_COND(pd_data == NULL, "Couldn't find PD (%ld)\n", client_id);

        vka_t *client_vka = pd_data->pd.pd_vka;

        /* Create the badged endpoint */
        *ret_cap = resource_component_make_badged_ep(component->server_vka, client_vka, component->server_ep,
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
        seL4_CPtr slot_in_rt = ret_cap && client_id == get_gpi_server()->rt_pd_id ? *ret_cap : seL4_CapNull;
        seL4_CPtr slot_in_pd = ret_cap && client_id != get_gpi_server()->rt_pd_id ? *ret_cap : seL4_CapNull;

        error = pd_add_resource_by_id(client_id,
                                      make_res_id(component->resource_type, component->space_id, resource_id),
                                      slot_in_rt, slot_in_pd, slot_in_rt);
        GOTO_IF_ERR(error, "Failed to add %s  esource to PD\n", cap_type_to_str(component->resource_type));
    }

err_goto:
    return error;
}

resource_component_registry_entry_t *resource_component_registry_get_by_badge(resource_component_context_t *component,
                                                                              seL4_Word badge)
{
    return (resource_component_registry_entry_t *)resource_registry_get_by_badge(&component->registry, badge);
}

resource_component_registry_entry_t *resource_component_registry_get_by_id(resource_component_context_t *component,
                                                                           seL4_Word object_id)
{
    return (resource_component_registry_entry_t *)resource_registry_get_by_id(&component->registry, object_id);
}

int resource_component_inc(resource_component_context_t *component,
                           uint64_t object_id)
{
    int error = 0;

    resource_component_registry_entry_t *reg_entry = resource_component_registry_get_by_id(component, object_id);
    GOTO_IF_COND(reg_entry == NULL, "Couldn't find %s (%ld)\n", cap_type_to_str(component->resource_type), object_id);

    OSDB_PRINTF("inc refcount %s (%d), new count %d\n",
                cap_type_to_str(component->resource_type), object_id, reg_entry->gen.count + 1);

    resource_registry_inc(&component->registry, (resource_registry_node_t *)reg_entry);

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

    resource_registry_dec(&component->registry, (resource_registry_node_t *)reg_entry);

err_goto:
    return error;
}

void resource_component_debug_print(resource_component_context_t *component)
{
    resource_registry_node_t *curr;
    for (curr = component->registry.head; curr != NULL; curr = curr->hh.next)
    {
        printf(" - %s (%d), refcount %d\n", cap_type_to_str(component->resource_type), curr->object_id, curr->count);
    }
}

int resource_component_transfer_cap(vka_t *src_vka,
                                    vka_t *dst_vka,
                                    seL4_CPtr src_ep,
                                    cspacepath_t *dest,
                                    bool mint,
                                    seL4_Word badge)
{
    int error = 0;
    cspacepath_t src;
    vka_cspace_make_path(src_vka, src_ep, &src);

    if (dst_vka)
    {
        error = vka_cspace_alloc_path(dst_vka, dest);
    }
    else
    {
        error = vka_cspace_alloc_path(src_vka, dest);
    }

    GOTO_IF_ERR(error, "Failed to allocate slot\n");

    if (mint)
    {
        return vka_cnode_mint(dest,
                              &src,
                              seL4_NoRead, // So that recipients of resources cannot receive endpoint messages
                              badge);
    }
    else
    {
        return vka_cnode_copy(dest, &src, seL4_AllRights);
    }

err_goto:
    return error;
}

seL4_CPtr resource_component_make_badged_ep_custom(vka_t *src_vka,
                                                   vka_t *dst_vka,
                                                   seL4_CPtr src_ep,
                                                   seL4_Word custom_badge)
{
    cspacepath_t dest = {0};
    int error = resource_component_transfer_cap(src_vka, dst_vka, src_ep, &dest, true, custom_badge);
    WARN_IF_COND(error, "Could not make custom badged endpoint\n");

    return dest.capPtr;
}

seL4_CPtr resource_component_make_badged_ep(vka_t *src_vka, vka_t *dst_vka, seL4_CPtr src_ep,
                                            gpi_cap_t resource_type, uint64_t space_id, uint64_t res_id, uint64_t client_id)
{
    int error = 0;

    /* Make the badge */
    seL4_Word badge = gpi_new_badge(resource_type,
                                    0x00,
                                    client_id,
                                    space_id,
                                    res_id);

    GOTO_IF_COND(badge == 0, "Failed to make badge\n");

    /* Mint the cap */
    cspacepath_t dest = {0};
    error = resource_component_transfer_cap(src_vka, dst_vka, src_ep, &dest, true, badge);

    return dest.capPtr;

err_goto:
    return seL4_CapNull;
}