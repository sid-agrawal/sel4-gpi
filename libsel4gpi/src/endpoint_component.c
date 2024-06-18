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
 * @param server_vka unused, defined for resspc component's generic callback
 * @param server_vspace unused, defined for resspc component's generic callback
 * @param client_pd the PD where the endpoint will be installed
 *
 */
static int ep_new(ep_t *ep, vka_t *server_vka, vspace_t *server_vspace, pd_t *client_pd)
{
    int error = 0;
    cspacepath_t dest;

    error = vka_cspace_alloc_path(client_pd->pd_vka, &dest);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate slot in client PD\n");

    // alloc ep from gpi server's untyped
    error = vka_utspace_alloc(server_vka, &dest, seL4_EndpointObject, seL4_EndpointBits, &ep->alloc_cookie);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate endpoint from RT's untyped\n");

    ep->endpoint_in_PD = dest.capPtr;
err_goto:
    return error;
}

static void ep_destroy(ep_t *ep, vka_t *server_vka)
{
    // (XXX) Linh: the slot where this EP sits in any PD should be deleted during PD cleanup, right?
    vka_utspace_free(server_vka, seL4_EndpointObject, seL4_EndpointBits, ep->alloc_cookie);
}

/* callback when an EP is deleted */
static void on_ep_registry_delete(resource_server_registry_node_t *node_gen, void *arg)
{
    ep_component_registry_entry_t *node = (ep_component_registry_entry_t *)node_gen;

    OSDB_PRINTF("Destroying EP (%d)\n", node->ep.id);

    ep_destroy(&node->ep, get_ep_component()->server_vka);
}

static seL4_MessageInfo_t handle_ep_allocation(seL4_Word sender_badge)
{
    OSDB_PRINTF("Got EP allocation request from: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    seL4_MessageInfo_t reply_tag;
    ep_component_registry_entry_t *new_entry;
    seL4_CPtr ret_cap = 0;
    uint32_t client_id = get_client_id_from_badge(sender_badge);

    pd_component_registry_entry_t *pd_data = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), client_id);
    SERVER_GOTO_IF_COND(pd_data == NULL, "Couldn't find PD (%ld)\n", client_id);

    error = resource_component_allocate(get_ep_component(), client_id, BADGE_OBJ_ID_NULL, false, (void *)&pd_data->pd,
                                        (resource_server_registry_node_t **)&new_entry, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new EP object\n");

    OSDB_PRINTF("Allocated new EP (%d)\n", new_entry->ep.id);

    seL4_SetMR(EPMSGREG_CONNECT_ACK_SLOT, ret_cap);
    seL4_SetMR(EPMSGREG_CONNECT_ACK_RAW_EP, new_entry->ep.endpoint_in_PD);

err_goto:
    seL4_SetMR(EPMSGREG_FUNC, EP_FUNC_CONNECT_ACK);
    reply_tag = seL4_MessageInfo_new(error, 0, 0, EPMSGREG_CONNECT_ACK_END);
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

    if (get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL)
    {
        SERVER_GOTO_IF_COND(func != EP_FUNC_CONNECT_REQ,
                            "Received invalid request on the allocation endpoint\n");
        reply_tag = handle_ep_allocation(sender_badge);
    }
    else
    {
        SERVER_GOTO_IF_COND(1, "Unknown request received: %d\n", func);
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
