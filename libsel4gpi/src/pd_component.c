/**
 * @file pd_component.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the pd server API from pd_component.h.
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <autoconf.h>

#include <stdio.h>
#include <string.h>

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vka/capops.h>

#include <utils/arith.h>
#include <utils/ansi.h>
#include <sel4utils/api.h>
#include <sel4utils/strerror.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/pd_component.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/test_init_data.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/gpi_client.h>
#include <sel4gpi/pd_creation.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/resource_space_component.h>

// Defined for utility printing macros
#define DEBUG_ID PD_DEBUG
#define SERVER_ID PDSERVS

// (XXX) Arya: to be extracted to another component
resource_server_registry_t server_registry;

resource_component_context_t *get_pd_component(void)
{
    return &get_gpi_server()->pd_component;
}

static pd_component_registry_entry_t *pd_component_registry_get_entry_by_id(seL4_Word object_id)
{
    return (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), object_id);
}

static pd_component_registry_entry_t *pd_component_registry_get_entry_by_badge(seL4_Word badge)
{
    return (pd_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_pd_component(), badge);
}

// Called when an item from the CPU registry is deleted
static void on_pd_registry_delete(resource_server_registry_node_t *node_gen)
{
    pd_component_registry_entry_t *node = (pd_component_registry_entry_t *)node_gen;

    OSDB_PRINTF("Destroying PD(%d, %s)\n", node->pd.id, node->pd.image_name);

    pd_destroy(&node->pd, get_pd_component()->server_vka, get_pd_component()->server_vspace);
}

static seL4_MessageInfo_t handle_pd_allocation(seL4_Word sender_badge)
{
    OSDB_PRINTF("Got connect request from badge %lx\n", sender_badge);
    int error = 0;
    seL4_MessageInfo_t reply_tag;
    seL4_CPtr ret_cap;
    pd_component_registry_entry_t *new_entry;
    uint32_t client_id = get_client_id_from_badge(sender_badge);

    error = resource_component_allocate(get_pd_component(), client_id, BADGE_OBJ_ID_NULL, false, NULL,
                                        (resource_server_registry_node_t **)&new_entry, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "failed to allocat a PD\n");
    new_entry->pd.pd_cap_in_RT = ret_cap;

    OSDB_PRINTF("Successfully allocated a new PD %d.\n", new_entry->pd.id);

    /* Return this badged end point in the return message. */
    seL4_SetCap(0, ret_cap);
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_CONNECT_ACK);
    reply_tag= seL4_MessageInfo_new(error, 0, 1, PDMSGREG_CONNECT_ACK_END);
    return reply_tag;

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_CONNECT_ACK);
    reply_tag = seL4_MessageInfo_new(error, 0, 0, PDMSGREG_CONNECT_ACK_END);
    return reply_tag;
}

static seL4_MessageInfo_t handle_disconnect_req(seL4_Word sender_badge,
                                                seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got disconnect request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%ld)\n", get_object_id_from_badge(sender_badge));

    uint32_t pd_id = client_data->pd.id;

    /* Remove the PD from registry, this will also destroy the PD */
    resource_server_registry_delete(&get_pd_component()->registry, (resource_server_registry_node_t *)client_data);

    // (XXX) Arya: Should we be deleting from registry or just decrementing?

    OSDB_PRINTF("Cleaned up PD %d.\n", pd_id);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_DISCONNECT_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, PDMSGREG_DISCONNECT_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_next_slot_req(seL4_Word sender_badge,
                                               seL4_MessageInfo_t old_tag)
{
    // OSDB_PRINTF("Got next slot request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%ld)\n", get_object_id_from_badge(sender_badge));

    seL4_Word slot;
    error = pd_next_slot(&client_data->pd,
                         &slot);

    seL4_SetMR(PDMSGREG_NEXT_SLOT_PD_SLOT, slot);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_NEXT_SLOT_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_NEXT_SLOT_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_free_slot_req(seL4_Word sender_badge,
                                               seL4_MessageInfo_t old_tag)
{
    // OSDB_PRINTF("Got free slot request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%ld)\n", get_object_id_from_badge(sender_badge));

    seL4_Word slot = seL4_GetMR(PDMSGREG_FREE_SLOT_REQ_SLOT);
    // OSDB_PRINTF("Freeing PD's slot %d.\n", (int)slot);

    error = pd_free_slot(&client_data->pd, slot);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_FREE_SLOT_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_FREE_SLOT_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_alloc_ep_req(seL4_Word sender_badge,
                                              seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got alloc ep request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Failed to find client badge %lx.\n", sender_badge);

    seL4_CPtr slot;
    error = pd_alloc_ep(&client_data->pd,
                        get_pd_component()->server_vka,
                        &slot);

    seL4_SetMR(PDMSGREG_ALLOC_EP_PD_SLOT, slot);
    OSDB_PRINTF("Allocated ep in slot %d\n", (int)slot);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_ALLOC_EP_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_ALLOC_EP_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_badge_ep_req(seL4_Word sender_badge,
                                              seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got badge ep request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%ld)\n", get_object_id_from_badge(sender_badge));

    seL4_Word badge = seL4_GetMR(PDMSGREG_BADGE_EP_REQ_BADGE);
    seL4_CPtr src_ep_slot = seL4_GetMR(PDMSGREG_BADGE_EP_REQ_SRC);
    seL4_Word slot;

    error = pd_badge_ep(&client_data->pd,
                        src_ep_slot,
                        badge,
                        &slot);

    seL4_SetMR(PDMSGREG_BADGE_EP_PD_SLOT, slot);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_BADGE_EP_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_BADGE_EP_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_send_cap_req(seL4_Word sender_badge,
                                              seL4_MessageInfo_t old_tag,
                                              seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got send-cap request from client badge %lx.\n", sender_badge);
    int error = 0;

    /* This only works if the extra cap is a GPI core cap (badged version of GPI server EP) */
    OSDB_PRINTF("received_cap: %lu (badge: %lx)\n", received_cap, seL4_GetBadge(0));
    OSDB_PRINTF("Unwrapped: %s\n",
                seL4_MessageInfo_get_capsUnwrapped(old_tag) ? "true" : "false");

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%ld)\n", get_object_id_from_badge(sender_badge));

    seL4_Word received_caps_badge = seL4_GetBadge(0);

    seL4_Word slot;
    error = pd_send_cap(&client_data->pd,
                        received_cap,
                        received_caps_badge,
                        &slot,
                        true);

    seL4_SetMR(PDMSGREG_SEND_CAP_PD_SLOT, slot);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SENDCAP_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_SEND_CAP_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_dump_cap_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got dump-cap request from client badge %lx.\n", sender_badge);
    int error = 0;

    assert(seL4_MessageInfo_get_extraCaps(old_tag) == 0);
    assert(seL4_MessageInfo_get_label(old_tag) == 0);

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%ld)\n", get_object_id_from_badge(sender_badge));

    error = pd_dump(&client_data->pd);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_DUMP_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_DUMP_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_share_rde_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    int error = 0;

    seL4_Word type = seL4_GetMR(PDMSGREG_SHARE_RDE_REQ_TYPE);
    seL4_Word space_id = seL4_GetMR(PDMSGREG_SHARE_RDE_REQ_SPACE_ID);

    OSDB_PRINTF("share_rde_req: Got request from client badge %lx for RDE type %ld with space %ld.\n",
                sender_badge, type, space_id);

    seL4_Word client_id = get_client_id_from_badge(sender_badge);
    pd_component_registry_entry_t *target_data = pd_component_registry_get_entry_by_badge(sender_badge);
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_id(client_id);

    SERVER_GOTO_IF_COND(target_data == NULL, "Couldn't find target PD (%ld)\n", get_object_id_from_badge(sender_badge));
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find client PD (%ld)\n", client_id);

    osmosis_rde_t *rde = pd_rde_get(&client_data->pd, type, space_id);
    SERVER_GOTO_IF_COND(rde == NULL, "share_rde_req: Failed to find RDE for type %ld and space %ld.\n", type, space_id);

    osmosis_rde_t *target_pd_rde = pd_rde_get(&target_data->pd, type, space_id);
    if (target_pd_rde != NULL)
    {
        printf("RDE already exists in target PD\n");
        goto err_goto;
    }

    resspc_component_registry_entry_t *resource_space_data = resource_space_get_entry_by_id(rde->space_id);
    SERVER_GOTO_IF_COND(resource_space_data == NULL, "share_rde_req: Failed to find resource space ID %d.\n", rde->space_id);

    rde_type_t rde_type = {.type = type};
    error = pd_add_rde(&target_data->pd,
                       rde_type,
                       rde->space_id,
                       resource_space_data->space.server_ep);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SHARE_RDE_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_SHARE_RDE_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_give_resource_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    int error = 0;

    seL4_Word server_id = get_object_id_from_badge(sender_badge);
    seL4_Word recipient_id = seL4_GetMR(PDMSGREG_GIVE_RES_REQ_CLIENT_ID);
    seL4_Word space_id = seL4_GetMR(PDMSGREG_GIVE_RES_REQ_SPACE_ID);
    seL4_Word resource_id = seL4_GetMR(PDMSGREG_GIVE_RES_REQ_RES_ID);

    // OSDB_PRINTF("Got give resource request from client badge %lx, space ID %ld, resource ID %ld.\n",
    //             sender_badge, space_id, resource_id);

    pd_component_registry_entry_t *server_data = pd_component_registry_get_entry_by_id(server_id);
    pd_component_registry_entry_t *recipient_data = pd_component_registry_get_entry_by_id(recipient_id);
    resspc_component_registry_entry_t *resource_space_data = resource_space_get_entry_by_id(space_id);

    SERVER_GOTO_IF_COND(server_data == NULL, "Couldn't find server PD (%ld)\n", server_id);
    SERVER_GOTO_IF_COND(recipient_data == NULL, "Couldn't find target PD (%ld)\n", recipient_id);
    SERVER_GOTO_IF_COND(resource_space_data == NULL, "Couldn't find resource space (%ld)\n", space_id);

    uint64_t res_node_id = gpi_new_badge(resource_space_data->space.resource_type, 0, 0, space_id, resource_id);
    pd_hold_node_t *resource_data = (pd_hold_node_t *)resource_server_registry_get_by_id(&server_data->pd.hold_registry, res_node_id);
    SERVER_GOTO_IF_COND(resource_data == NULL, "Couldn't find resource (%lx)\n", res_node_id);

    // OSDB_PRINTF("resource server %ld gives resource in space %ld with ID %ld to client %ld\n",
    //             server_id, space_id, resource_id, recipient_id);

    /* Create a new badged EP for the resource */
    seL4_CPtr dest = resource_server_make_badged_ep(get_pd_component()->server_vka, &recipient_data->pd.pd_vka,
                                                    resource_space_data->space.server_ep, resource_space_data->space.resource_type,
                                                    space_id, resource_id, recipient_id);
    seL4_SetMR(PDMSGREG_GIVE_RES_ACK_DEST, dest);

    // Add the resource to the PD object
    // (XXX) Arya: How to handle duplicate entries to the same resource?
    // The hash table is keyed by resource ID
    error = pd_add_resource(&recipient_data->pd, resource_space_data->space.resource_type, space_id, resource_id,
                            seL4_CapNull, dest, seL4_CapNull);
    SERVER_GOTO_IF_ERR(error, "Failed to add resource to PD (%ld)\n", recipient_id);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_GIVE_RES_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_GIVE_RES_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_exit_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got exit request from client badge %lx\n", sender_badge);

    handle_disconnect_req(sender_badge, old_tag);
}

static seL4_MessageInfo_t handle_ipc_bench_req(void)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_BENCH_IPC_ACK);
    bool do_cap_transfer = seL4_GetMR(PDMSGREG_BENCH_IPC_REQ_CAP_TRANSFER);

    int num_caps = 0;
    if (do_cap_transfer)
    {
        seL4_CPtr dummy_reply_cap;
        int error = vka_cspace_alloc(get_pd_component()->server_vka, &dummy_reply_cap);
        assert(error == 0);
        seL4_SetCap(0, dummy_reply_cap);
        num_caps = 1;
    }

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, num_caps, PDMSGREG_BENCH_IPC_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_runtime_setup_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got runtime setup request from client badge: ");
    BADGE_PRINT(sender_badge);

    int error = 0;

    SERVER_GOTO_IF_COND(seL4_MessageInfo_get_capsUnwrapped(old_tag) < 1, "Missing cap for target PD in capsUnwrapped\n");

    pd_component_registry_entry_t *target_pd = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_pd_component(), sender_badge);
    SERVER_GOTO_IF_COND(target_pd == NULL, "Couldn't find target PD (%ld)\n", get_object_id_from_badge(sender_badge));

    ads_component_registry_entry_t *target_ads = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), get_object_id_from_badge(seL4_GetBadge(0)));
    SERVER_GOTO_IF_COND(target_ads == NULL, "Couldn't find target ADS (%ld)\n", get_object_id_from_badge(seL4_GetBadge(0)));

    cpu_component_registry_entry_t *target_cpu = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_cpu_component(), get_object_id_from_badge(seL4_GetBadge(1)));
    SERVER_GOTO_IF_COND(target_cpu == NULL, "Couldn't find target CPU (%ld)\n", get_object_id_from_badge(seL4_GetBadge(1)));

    /* parse the arguments */
    int argc = seL4_GetMR(PDMSGREG_SETUP_REQ_ARGC);

    // These brackets limit the scope of argc/argv so we may goto err_goto
    {
        seL4_Word args[argc];

        for (int i = 0; i < argc; i++)
        {
            switch (i)
            {
            case 0:
                args[i] = seL4_GetMR(PDMSGREG_SETUP_REQ_ARG0);
                break;
            case 1:
                args[i] = seL4_GetMR(PDMSGREG_SETUP_REQ_ARG1);
                break;
            case 2:
                args[i] = seL4_GetMR(PDMSGREG_SETUP_REQ_ARG2);
                break;
            case 3:
                args[i] = seL4_GetMR(PDMSGREG_SETUP_REQ_ARG3);
                break;
            }
        }

        char string_args[argc][WORD_STRING_SIZE];
        char *argv[argc];

        for (int i = 0; i < argc; i++)
        {
            argv[i] = string_args[i];
            snprintf(argv[i], WORD_STRING_SIZE, "%" PRIuPTR "", args[i]);
        }

        // (XXX) Linh: stack_top meaning differs depending on what PD we're starting, should fix as this is not so nice
        void *stack_top = (void *)seL4_GetMR(PDMSGREG_SETUP_REQ_STACK);
        size_t stack_size = seL4_GetMR(PDMSGREG_SETUP_REQ_STACK_SZ);

        // these fields only matter if PD is a process
        target_pd->pd.proc.thread.stack_top = stack_top;
        target_pd->pd.proc.thread.stack_size = stack_size;

        void *entry_point = (void *)seL4_GetMR(PDMSGREG_SETUP_REQ_ENTRY_POINT);
        void *ipc_buf_addr = (void *)seL4_GetMR(PDMSGREG_SETUP_REQ_IPC_BUF);
        pd_setup_type_t setup_mode = (pd_setup_type_t)seL4_GetMR(PDMSGREG_SETUP_REQ_TYPE);

        switch (setup_mode)
        {
        case PD_RUNTIME_SETUP:
            void *init_stack;
            error = ads_write_arguments(&target_pd->pd.proc,
                                        (void *)target_pd->pd.init_data_in_PD,
                                        get_gpi_server()->server_vka,
                                        get_pd_component()->server_vspace,
                                        argc,
                                        argv,
                                        &init_stack);
            if (!error)
            {
                error = cpu_set_remote_context(&target_cpu->cpu, entry_point, init_stack);
            }
            break;
        case PD_REGISTER_SETUP:
            error = cpu_set_local_context(&target_cpu->cpu,
                                          entry_point,
                                          argc > 0 ? (void *)args[0] : NULL,
                                          argc > 1 ? (void *)args[1] : NULL,
                                          argc > 2 ? (void *)args[2] : NULL,
                                          stack_top);
            break;
        default:
            error = 1;
            OSDB_PRINTERR("Invalid PD setup mode specified\n");
            break;
        }
    }

    SERVER_GOTO_IF_ERR(error, "Failed to setup PD\n");
err_goto:
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, PDMSGREG_SETUP_ACK_END);
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SETUP_ACK);
    return tag;
}

static seL4_MessageInfo_t handle_share_resource_type_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    int error = 0;
    OSDB_PRINTF("Got Share Resource Type Request: ");
    BADGE_PRINT(sender_badge);

    pd_component_registry_entry_t *src_pd_data = (pd_component_registry_entry_t *)resource_component_registry_get_by_badge(get_pd_component(), sender_badge);
    SERVER_GOTO_IF_COND_BG(src_pd_data == NULL, sender_badge, "Failed to find source PD data ");

    seL4_Word dst_pd_badge = seL4_GetBadge(0);
    pd_component_registry_entry_t *dst_pd_data = (pd_component_registry_entry_t *)resource_component_registry_get_by_badge(get_pd_component(), dst_pd_badge);
    SERVER_GOTO_IF_COND_BG(dst_pd_data == NULL, dst_pd_badge, "Failed to find dest PD data ");

    SERVER_GOTO_IF_COND(src_pd_data->pd.id == dst_pd_data->pd.id, "Invalid sharing of resources between the same PD (%d -> %d)\n", src_pd_data->pd.id, dst_pd_data->pd.id);

    gpi_cap_t res_type = (gpi_cap_t)seL4_GetMR(PDMSGREG_SHARE_RES_TYPE_REQ_TYPE);
    SERVER_GOTO_IF_COND(res_type != GPICAP_TYPE_MO && res_type != GPICAP_TYPE_FILE, "Sharing of resource type %s not permitted.\n", cap_type_to_str(res_type));
    linked_list_t *resources = pd_get_resources_of_type(&src_pd_data->pd, res_type);
    error = pd_bulk_add_resource(&dst_pd_data->pd, resources);
    SERVER_GOTO_IF_ERR(error, "Error occurred during resource sharing (some may still have been successful)\n");

    OSDB_PRINTF("Shared %s resources between PDs (%d -> %d)\n", cap_type_to_str(res_type), src_pd_data->pd.id, dst_pd_data->pd.id);

err_goto:
    if (resources)
    {
        linked_list_destroy(resources);
    }

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, PDMSGREG_SHARE_RES_TYPE_ACK_END);
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SHARE_RES_TYPE_ACK);

    return tag;
}

static seL4_MessageInfo_t pd_component_handle(seL4_MessageInfo_t tag,
                                              seL4_Word sender_badge,
                                              seL4_CPtr received_cap,
                                              bool *need_new_recv_cap)
{
    int error = 0; // unused, to appease the error handling macros
    enum pd_component_funcs func = seL4_GetMR(PDMSGREG_FUNC);
    seL4_MessageInfo_t reply_tag;

    if (get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL)
    {
        SERVER_GOTO_IF_COND(func != PD_FUNC_CONNECT_REQ,
                            "Received invalid request on the allocation endpoint: %d\n", func);
        reply_tag = handle_pd_allocation(sender_badge);
    }
    else
    {
        switch (func)
        {
        case PD_FUNC_DISCONNECT_REQ:
            reply_tag = handle_disconnect_req(sender_badge, tag);
            break;
        case PD_FUNC_NEXT_SLOT_REQ:
            reply_tag = handle_next_slot_req(sender_badge, tag);
            break;
        case PD_FUNC_FREE_SLOT_REQ:
            reply_tag = handle_free_slot_req(sender_badge, tag);
            break;
        case PD_FUNC_ALLOC_EP_REQ:
            reply_tag = handle_alloc_ep_req(sender_badge, tag);
            break;
        case PD_FUNC_BADGE_EP_REQ:
            reply_tag = handle_badge_ep_req(sender_badge, tag);
            break;
        case PD_FUNC_SENDCAP_REQ:
            reply_tag = handle_send_cap_req(sender_badge, tag, received_cap);
            *need_new_recv_cap = true;
            break;
        case PD_FUNC_DUMP_REQ:
            reply_tag = handle_dump_cap_req(sender_badge, tag);
            break;
        case PD_FUNC_SHARE_RDE_REQ:
            reply_tag = handle_share_rde_req(sender_badge, tag);
            break;
        case PD_FUNC_GIVE_RES_REQ:
            reply_tag = handle_give_resource_req(sender_badge, tag);
            break;
        case PD_FUNC_EXIT_REQ:
            reply_tag = handle_exit_req(sender_badge, tag);
            break;
        case PD_FUNC_BENCH_IPC_REQ:
            reply_tag = handle_ipc_bench_req();
            break;
        case PD_FUNC_SETUP_REQ:
            reply_tag = handle_runtime_setup_req(sender_badge, tag);
            break;
        case PD_FUNC_SHARE_RES_TYPE_REQ:
            reply_tag = handle_share_resource_type_req(sender_badge, tag);
            break;
        default:
            SERVER_GOTO_IF_COND(1, "Unknown request received: %d\n", func);
            break;
        }
    }

    return reply_tag;

err_goto:
    seL4_MessageInfo_t err_tag = seL4_MessageInfo_set_label(reply_tag, 1);
    return err_tag;
}

/** --- Functions callable by root task --- **/

int pd_component_initialize(simple_t *server_simple,
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
        .type = GPICAP_TYPE_PD,
        .ep = get_gpi_server()->server_ep_obj.cptr,
    };

    error = resource_component_allocate(get_resspc_component(), get_gpi_server()->rt_pd_id, BADGE_OBJ_ID_NULL, false, (void *)&resspc_config,
                                        (resource_server_registry_node_t **)&space_entry, NULL);
    assert(error == 0);

    // Initialize the component
    resource_component_initialize(get_pd_component(),
                                  GPICAP_TYPE_PD,
                                  space_entry->space.id,
                                  pd_component_handle,
                                  (int (*)(resource_component_object_t *, vka_t *, vspace_t *, void *))pd_new,
                                  on_pd_registry_delete,
                                  sizeof(pd_component_registry_entry_t),
                                  server_simple,
                                  server_vka,
                                  server_cspace,
                                  server_vspace,
                                  server_thread,
                                  server_ep_obj.cptr);

    resource_server_initialize_registry(&server_registry, NULL);
}

void forge_pd_for_root_task(uint64_t rt_id)
{
    pd_component_registry_entry_t *rt_entry = malloc(sizeof(pd_component_registry_entry_t));
    rt_entry->gen.object_id = rt_id;
    rt_entry->pd.id = rt_id;
    resource_server_registry_insert(&get_pd_component()->registry, (resource_server_registry_node_t *)rt_entry);
}

// (XXX) Arya: hack to store the test PD ID for destroying it later
uint64_t test_pd_id;

void forge_pd_cap_from_init_data(test_init_data_t *init_data, sel4utils_process_t *test_process, void **osm_init_data)
{
    assert(init_data != NULL);

    int error = 0;
    seL4_CPtr ret_cap;
    pd_component_registry_entry_t *new_entry;

    /* Allocate the PD object */
    error = resource_component_allocate(get_pd_component(), get_gpi_server()->rt_pd_id, BADGE_OBJ_ID_NULL, false, NULL,
                                        (resource_server_registry_node_t **)&new_entry, &ret_cap);
    ZF_LOGF_IFERR(error, "Failed to allocate PD for forging");
    assert(error == 0);
    pd_t *pd = &new_entry->pd;
    test_pd_id = pd->id;
    pd->pd_cap_in_RT = ret_cap;
    pd->init_data->pd_conn.id = test_pd_id;

    /* Update the PD object from init data */
    // pd_new(pd,
    //        get_pd_component()->server_vka,
    //        get_pd_component()->server_vspace,
    //       NULL);

    // Split the test process' cspace and initialize a vka with half
    seL4_CPtr mid_slot = DIV_ROUND_UP(init_data->free_slots.start + init_data->free_slots.end, 2);
    error = pd_bootstrap_allocator(pd, test_process->cspace.cptr,
                                   mid_slot, init_data->free_slots.end,
                                   init_data->cspace_size_bits,
                                   // seL4_WordBits - init_data->cspace_size_bits);
                                   0);
    ZF_LOGF_IFERR(error, "Failed to initialize PD VKA");
    init_data->free_slots.end = mid_slot - 1;

    // Add the basic RDEs
    rde_type_t resspc_type = {.type = GPICAP_TYPE_RESSPC};
    pd_add_rde(pd, resspc_type, RESSPC_SPACE_ID, get_gpi_server()->server_ep_obj.cptr);

    rde_type_t ads_type = {.type = GPICAP_TYPE_ADS};
    pd_add_rde(pd, ads_type, get_ads_component()->space_id, get_gpi_server()->server_ep_obj.cptr);

    rde_type_t cpu_type = {.type = GPICAP_TYPE_CPU};
    pd_add_rde(pd, cpu_type, get_cpu_component()->space_id, get_gpi_server()->server_ep_obj.cptr);

    rde_type_t mo_type = {.type = GPICAP_TYPE_MO};
    pd_add_rde(pd, mo_type, get_mo_component()->space_id, get_gpi_server()->server_ep_obj.cptr);

    rde_type_t pd_type = {.type = GPICAP_TYPE_PD};
    pd_add_rde(pd, pd_type, get_pd_component()->space_id, get_gpi_server()->server_ep_obj.cptr);

    // Forge ADS cap
    seL4_CPtr child_as_cap_in_parent;
    uint32_t ads_id;
    error = forge_ads_cap_from_vspace(&test_process->vspace, get_pd_component()->server_vka, pd->id, &child_as_cap_in_parent, &ads_id);
    ZF_LOGF_IFERR(error, "Failed to forge child's as cap");
    pd->init_data->ads_conn.id = ads_id;

    // Forge CPU cap
    seL4_CPtr child_cpu_cap_in_parent;
    uint32_t cpu_id;
    error = forge_cpu_cap_from_tcb(test_process, get_pd_component()->server_vka, pd->id, &child_cpu_cap_in_parent, &cpu_id);
    ZF_LOGF_IFERR(error, "Failed to forge child's CPU cap");

    // Copy the ADS/CPU/PD caps to the test process
    // The refcount of each is 1
    error = copy_cap_to_pd(pd, child_as_cap_in_parent, &pd->init_data->ads_conn.badged_server_ep_cspath.capPtr);
    assert(error == 0);
    pd_add_resource(pd, GPICAP_TYPE_ADS, get_ads_component()->space_id, ads_id,
                    child_as_cap_in_parent, pd->init_data->ads_conn.badged_server_ep_cspath.capPtr, child_as_cap_in_parent);

    error = copy_cap_to_pd(pd, pd->pd_cap_in_RT, &pd->init_data->pd_conn.badged_server_ep_cspath.capPtr);
    assert(error == 0);
    pd_add_resource(pd, GPICAP_TYPE_PD, get_pd_component()->space_id, pd->id,
                    pd->pd_cap_in_RT, pd->init_data->pd_conn.badged_server_ep_cspath.capPtr, pd->pd_cap_in_RT);

    error = copy_cap_to_pd(pd, child_cpu_cap_in_parent, &pd->init_data->cpu_conn.badged_server_ep_cspath.capPtr);
    assert(error == 0);
    pd_add_resource(pd, GPICAP_TYPE_CPU, get_cpu_component()->space_id, cpu_id,
                    child_cpu_cap_in_parent, pd->init_data->cpu_conn.badged_server_ep_cspath.capPtr, child_cpu_cap_in_parent);

    // Attach the init data to test PD
    void *init_data_vaddr = (void *)0x50000000;
    error = ads_component_attach(ads_id, pd->init_data_mo_id, SEL4UTILS_RES_TYPE_GENERIC, init_data_vaddr, &init_data_vaddr);
    assert(error == 0);

    *osm_init_data = init_data_vaddr;
    pd->init_data_in_PD = init_data_vaddr;
    OSDB_PRINTF("Test process init data is at %p\n", pd->init_data_in_PD);
}

void destroy_test_pd(void)
{
    int error = 0;

    pd_component_registry_entry_t *client_pd_data = pd_component_registry_get_entry_by_id(test_pd_id);
    SERVER_GOTO_IF_COND(client_pd_data == NULL, "Couldn't find test PD (%ld) to destroy it\n", test_pd_id);

    /* Remove the PD from registry, this will also destroy the PD */
    resource_server_registry_delete(&get_pd_component()->registry, (resource_server_registry_node_t *)client_pd_data);

    return;

err_goto:
    ZF_LOGF("Failed to cleanup test PD\n");
}

int pd_add_resource_by_id(uint32_t pd_id,
                          gpi_cap_t cap_type,
                          uint32_t space_id,
                          uint32_t res_id,
                          seL4_CPtr slot_in_RT,
                          seL4_CPtr slot_in_PD,
                          seL4_CPtr slot_in_serverPD)
{
    int error = 0;

    pd_component_registry_entry_t *client_pd_data = pd_component_registry_get_entry_by_id(pd_id);
    SERVER_GOTO_IF_COND(client_pd_data == NULL, "Couldn't find PD (%d) to add resource \n", pd_id);

    error = pd_add_resource(&client_pd_data->pd, cap_type, space_id, res_id, slot_in_RT, slot_in_PD, slot_in_serverPD);

err_goto:
    return error;
}
