/**
 * @file endpoint_component.c
 * @author Linh Pham (phamhlinh01@gmail.com)
 * @brief Implementation for creating and tracking endpoints.
 *        An "endpoint resource space" is purely an implementation concept, to ref-count
 *        endpoints and clean them up. Endpoint are not resources in the OSmosis model.
 * @version 0.1
 * @date 2024-06-18
 *
 * @copyright Copyright (c) 2024
 *
 */
#include <sel4/sel4.h>
#include <vka/capops.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/endpoint_component.h>
#include <sel4gpi/resource_space_component.h>
#include <sel4gpi/gpi_server.h>

// Defined for utility printing macros
#define DEBUG_ID EP_DEBUG
#define SERVER_ID EPSERVS

resource_component_context_t *get_ep_component(void)
{
    return &get_gpi_server()->ep_component;
}

/* The EP component is small enough that these functions are put here, rather than in their own file */

/**
 * @brief Allocates a new endpoint in client PD's CSpace, however the memory is from the RT's pool
 *
 * @param ep the EP object
 * @param server_vka server's vka
 * @param server_vspace unused, defined for resspc component's generic callback
 * @param arg0 unused, defined for resspc component's generic callback
 *
 */
static int ep_new(ep_t *ep, vka_t *server_vka, vspace_t *server_vspace, void *arg0)
{
    return vka_alloc_endpoint(server_vka, &ep->endpoint_in_RT);
}

static void ep_destroy(ep_t *ep, vka_t *server_vka)
{
    // (XXX) Linh: the slot where this EP sits in any PD should be deleted during PD cleanup, right?
#ifdef CONFIG_DEBUG_BUILD
    seL4_Word is_last_copy = seL4_DebugCapIsLastCopy(ep->endpoint_in_RT.cptr);
    if (!is_last_copy)
    {
        OSDB_PRINTERR("Attempting to free EP (%d) with existing copies\n", ep->id);
    }
#endif
    // We need to revoke, since this endpoint might've been copied into a CPU's TCB, which may not be deleted yet
    // (XXX) Linh: It's possible for a CPU to exist beyond a PD's lifetime if other things `hold` it
    //             I think a CPU configured with a certain PD's CSpace should be destroyed along with the PD
    //             -> this needs more thought
    cspacepath_t path;
    vka_cspace_make_path(server_vka, ep->endpoint_in_RT.cptr, &path);
    int error = vka_cnode_revoke(&path);
    SERVER_PRINT_IF_ERR(error, "Failed to revoke EP (%d), future allocations will fail!\n", ep->id);

    vka_free_object(server_vka, &ep->endpoint_in_RT);
}

/* callback when an EP is deleted */
static void on_ep_registry_delete(resource_server_registry_node_t *node_gen, void *arg)
{
    ep_component_registry_entry_t *node = (ep_component_registry_entry_t *)node_gen;

    OSDB_PRINTF("Destroying EP (%d)\n", node->ep.id);

    ep_destroy(&node->ep, get_ep_component()->server_vka);
}

static int ep_component_allocate(uint32_t client_pd,
                                 seL4_CPtr *ret_ep_in_PD,
                                 seL4_CPtr *ret_badged_ep,
                                 ep_component_registry_entry_t **ret_ep_reg_entry)
{
    int error = 0;
    pd_component_registry_entry_t *pd_data = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), client_pd);
    SERVER_GOTO_IF_COND(pd_data == NULL, "Couldn't find PD (%ld)\n", client_pd);

    error = resource_component_allocate(get_ep_component(), client_pd, BADGE_OBJ_ID_NULL, false, NULL,
                                        (resource_server_registry_node_t **)ret_ep_reg_entry, ret_badged_ep);
    SERVER_GOTO_IF_COND(error || *ret_badged_ep == seL4_CapNull, "Failed to allocate new EP object\n");

    OSDB_PRINTF("Allocated new EP (%d)\n", (*ret_ep_reg_entry)->ep.id);

    cspacepath_t ep_in_pd;
    error = resource_server_transfer_cap(get_ep_component()->server_vka,
                                         pd_data->pd.pd_vka,
                                         (*ret_ep_reg_entry)->ep.endpoint_in_RT.cptr,
                                         &ep_in_pd,
                                         false, 0);
    SERVER_GOTO_IF_ERR(error, "Failed to copy raw endpoint to PD %d\n", pd_data->pd.id);
    *ret_ep_in_PD = ep_in_pd.capPtr;

err_goto:
    return error;
}

static seL4_MessageInfo_t handle_ep_allocation(seL4_Word sender_badge)
{
    OSDB_PRINTF("Got EP allocation request from: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    seL4_MessageInfo_t reply_tag;
    ep_component_registry_entry_t *new_entry;
    seL4_CPtr badged_ep = 0;
    seL4_CPtr ep_in_PD = 0;
    uint32_t client_id = get_client_id_from_badge(sender_badge);

    error = ep_component_allocate(client_id, &ep_in_PD, &badged_ep, &new_entry);

    seL4_SetMR(EPMSGREG_CONNECT_ACK_SLOT, badged_ep);
    seL4_SetMR(EPMSGREG_CONNECT_ACK_RAW_EP, ep_in_PD);

err_goto:
    seL4_SetMR(EPMSGREG_FUNC, EP_FUNC_CONNECT_ACK);
    reply_tag = seL4_MessageInfo_new(error, 0, 0, EPMSGREG_CONNECT_ACK_END);
    return reply_tag;
}

static seL4_MessageInfo_t handle_get_raw_endpoint(seL4_MessageInfo_t old_tag, seL4_Word sender_badge)
{
    int error = 0;
    OSDB_PRINTF("Get Raw endpoint request from: ");
    BADGE_PRINT(sender_badge);

    seL4_Uint64 extra_caps = seL4_MessageInfo_get_extraCaps(old_tag);
    seL4_Word pd_badge = sender_badge;
    if (extra_caps > 0)
    {
        pd_badge = seL4_GetBadge(0);
        OSDB_PRINTF("Extra caps detected, retrieving raw endpoint in target PD%d\n", get_client_id_from_badge(pd_badge));
    }

    pd_component_registry_entry_t *pd_data = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), get_client_id_from_badge(pd_badge));
    SERVER_GOTO_IF_COND(pd_data == NULL, "Cannot find PD data\n");

    ep_component_registry_entry_t *ep_data = (ep_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_ep_component(), sender_badge);
    SERVER_GOTO_IF_COND(ep_data == NULL, "Cannot find EP data\n");

    cspacepath_t dest;
    error = resource_server_transfer_cap(get_ep_component()->server_vka,
                                         pd_data->pd.pd_vka,
                                         ep_data->ep.endpoint_in_RT.cptr,
                                         &dest, false, 0);
    SERVER_GOTO_IF_ERR(error, "Failed to copy raw endpoint cap to PD %d\n", pd_data->pd.id);

    seL4_SetMR(EPMSGREG_GET_RAW_ENDPOINT_ACK_SLOT, dest.capPtr);
err_goto:
    seL4_SetMR(EPMSGREG_FUNC, EP_FUNC_GET_RAW_ENDPOINT_ACK);
    seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(error, 0, 0, EPMSGREG_GET_RAW_ENDPOINT_ACK_END);
    return reply_tag;
}

/** this should only be called by the test PDs */
static seL4_MessageInfo_t handle_forge_req(seL4_MessageInfo_t old_tag, seL4_Word sender_badge, seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got EP forge request from: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    seL4_MessageInfo_t reply_tag;
    ep_component_registry_entry_t *new_entry;
    seL4_CPtr badged_ep = 0;
    uint32_t client_id = get_client_id_from_badge(sender_badge);

    error = resource_component_allocate(get_ep_component(),
                                        client_id,
                                        BADGE_OBJ_ID_NULL,
                                        true,
                                        NULL,
                                        &new_entry,
                                        &badged_ep);

    seL4_SetMR(EPMSGREG_FORGE_ACK_SLOT, badged_ep);

err_goto:
    seL4_SetMR(EPMSGREG_FUNC, EP_FUNC_FORGE_ACK);
    reply_tag = seL4_MessageInfo_new(error, 0, 0, EPMSGREG_FORGE_ACK_END);
    return reply_tag;
}

static seL4_MessageInfo_t ep_component_handle(seL4_MessageInfo_t tag,
                                              seL4_Word sender_badge,
                                              seL4_CPtr received_cap,
                                              bool *need_new_recv_cap)
{
    int error = 0; // unused, to appease the error handling macros
    enum ep_component_funcs func = seL4_GetMR(EPMSGREG_FUNC);
    seL4_MessageInfo_t reply_tag;

    SERVER_GOTO_IF_COND(get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL &&
                            func != EP_FUNC_CONNECT_REQ && func != EP_FUNC_FORGE_REQ,
                        "Received invalid request on the allocation endpoint\n");

    switch (func)
    {
    case EP_FUNC_CONNECT_REQ:
        reply_tag = handle_ep_allocation(sender_badge);
        break;
    case EP_FUNC_GET_RAW_ENDPOINT_REQ:
        reply_tag = handle_get_raw_endpoint(tag, sender_badge);
        break;
    case EP_FUNC_FORGE_REQ:
        reply_tag = handle_forge_req(tag, sender_badge, received_cap);
        *need_new_recv_cap = true;
        break;
    default:
        SERVER_GOTO_IF_COND(1, "Unknown request received: %d\n", func);
        break;
    }

    return reply_tag;

err_goto:
    seL4_MessageInfo_t err_tag = seL4_MessageInfo_set_label(reply_tag, 1);
    return err_tag;
}

int ep_component_initialize(simple_t *server_simple,
                            vka_t *server_vka,
                            seL4_CPtr server_cspace,
                            vspace_t *server_vspace,
                            sel4utils_thread_t server_thread,
                            vka_object_t server_ep_obj)
{
    int error = 0;

    // Create the default PD resource space
    resspc_component_registry_entry_t *space_entry;

    resspc_config_t resspc_config = {
        .type = GPICAP_TYPE_EP,
        .ep = get_gpi_server()->server_ep_obj.cptr,
    };

    error = resource_component_allocate(get_resspc_component(), get_gpi_server()->rt_pd_id,
                                        BADGE_OBJ_ID_NULL, false, (void *)&resspc_config,
                                        (resource_server_registry_node_t **)&space_entry, NULL);
    assert(error == 0);

    // Initialize the component
    resource_component_initialize(get_ep_component(),
                                  GPICAP_TYPE_EP,
                                  space_entry->space.id,
                                  ep_component_handle,
                                  (int (*)(resource_component_object_t *, vka_t *, vspace_t *, void *))ep_new,
                                  on_ep_registry_delete,
                                  sizeof(ep_component_registry_entry_t),
                                  server_simple,
                                  server_vka,
                                  server_cspace,
                                  server_vspace,
                                  server_thread,
                                  server_ep_obj.cptr);
}
