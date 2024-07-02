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
#include <sel4gpi/gpi_rpc.h>
#include <ep_component_rpc.pb.h>

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
    /* Revoking here because this endpoint might've been copied into a CPU's TCB (as a fault EP),
     * which may not be deleted yet. The TCB will be bounded to an invalid fault endpoint, which is fine
     * because no PDs `hold` this EP anymore, rendering it effectively useless
     *
     * Additionally, if this was the the listening endpoint for a resource server PD, it's expected that the
     * badged versions (representing resources) got deleted when client PDs and the resource server PD is destroyed
     *
     */
    cspacepath_t path;
    vka_cspace_make_path(server_vka, ep->endpoint_in_RT.cptr, &path);
    int error = vka_cnode_revoke(&path);
    if (error)
    {
        OSDB_PRINTERR("Failed to revoke EP (%d), future allocations will fail!\n", ep->id);
    }

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
    pd_component_registry_entry_t *pd_data = pd_component_registry_get_entry_by_id(client_pd);
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

static void handle_ep_allocation(seL4_Word sender_badge, EpAllocMessage *msg, EpReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got EP allocation request from: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    ep_component_registry_entry_t *new_entry;
    seL4_CPtr badged_ep = 0;
    seL4_CPtr ep_in_PD = 0;
    uint32_t client_id = get_client_id_from_badge(sender_badge);

    error = ep_component_allocate(client_id, &ep_in_PD, &badged_ep, &new_entry);

    reply_msg->msg.alloc.raw_ep_slot = ep_in_PD;
    reply_msg->msg.alloc.slot = badged_ep;

err_goto:
    reply_msg->which_msg = EpReturnMessage_alloc_tag;
    reply_msg->errorCode = error;
}

static void handle_get_raw_endpoint(seL4_Word sender_badge, EpGetMessage *msg, EpReturnMessage *reply_msg)
{
    int error = 0;
    OSDB_PRINTF("Get Raw endpoint request from: ");
    BADGE_PRINT(sender_badge);

    pd_component_registry_entry_t *pd_data = NULL;
    if (msg->for_other_PD)
    {
        pd_data = (pd_component_registry_entry_t *)
            resource_component_registry_get_by_badge(get_pd_component(), seL4_GetBadge(0));
    }
    else
    {
        pd_data = (pd_component_registry_entry_t *)
            resource_component_registry_get_by_id(get_pd_component(), get_client_id_from_badge(sender_badge));
    }

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

    reply_msg->msg.get.slot = dest.capPtr;

err_goto:
    reply_msg->which_msg = EpReturnMessage_get_tag;
    reply_msg->errorCode = error;
}

/** this should only be called by the test PDs */
static void handle_forge_req(seL4_Word sender_badge, EpForgeMessage *msg,
                             EpReturnMessage *reply_msg, seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got EP forge request from: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
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

    reply_msg->msg.alloc.slot = badged_ep;

err_goto:
    reply_msg->which_msg = EpReturnMessage_alloc_tag;
    reply_msg->errorCode = error;
}

static void ep_component_handle(void *msg_p,
                                seL4_Word sender_badge,
                                seL4_CPtr received_cap,
                                void *reply_msg_p,
                                bool *need_new_recv_cap,
                                bool *should_reply)
{
    int error = 0; // unused, to appease the error handling macros
    EpMessage *msg = (EpMessage *)msg_p;
    EpReturnMessage *reply_msg = (EpReturnMessage *)reply_msg_p;

    SERVER_GOTO_IF_COND(get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL &&
                            msg->which_msg != EpMessage_alloc_tag &&
                            msg->which_msg != EpMessage_forge_tag,
                        "Received invalid request on the allocation endpoint\n");

    switch (msg->which_msg)
    {
    case EpMessage_alloc_tag:
        handle_ep_allocation(sender_badge, &msg->msg.alloc, reply_msg);
        break;
    case EpMessage_get_tag:
        handle_get_raw_endpoint(sender_badge, &msg->msg.get, reply_msg);
        *need_new_recv_cap = msg->msg.get.for_other_PD;
        break;
    case EpMessage_forge_tag:
        handle_forge_req(sender_badge, &msg->msg.forge, reply_msg, received_cap);
        *need_new_recv_cap = true;
        break;
    default:
        SERVER_GOTO_IF_COND(1, "Unknown request received: %d\n", msg->which_msg);
        break;
    }

    OSDB_PRINTF("Returning from EP component with error code %d\n", reply_msg->errorCode);
    return;

err_goto:
    OSDB_PRINTF("Returning from EP component with error code %d\n", error);
    reply_msg->errorCode = error;
}

int ep_component_initialize(vka_t *server_vka,
                            vspace_t *server_vspace,
                            vka_object_t server_ep_obj)
{
    int error = 0;

    // Create the default PD resource space
    resspc_component_registry_entry_t *space_entry;

    resspc_config_t resspc_config = {
        .type = GPICAP_TYPE_EP,
        .ep = get_gpi_server()->server_ep_obj.cptr,
        .pd_id = get_gpi_server()->rt_pd_id,
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
                                  server_vka,
                                  server_vspace,
                                  server_ep_obj.cptr,
                                  &EpMessage_msg,
                                  &EpReturnMessage_msg);
}
