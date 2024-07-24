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
#include <sel4gpi/pd_creation.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/resource_space_component.h>
#include <sel4gpi/endpoint_component.h>
#include <sel4gpi/gpi_rpc.h>

// Defined for utility printing macros
#define DEBUG_ID PD_DEBUG
#define SERVER_ID PDSERVS

// The RPC message structures
static sel4gpi_rpc_env_t rpc_env = {
    .request_desc = &PdMessage_msg,
    .reply_desc = &PdReturnMessage_msg,
};

resource_component_context_t *get_pd_component(void)
{
    return &get_gpi_server()->pd_component;
}

pd_component_registry_entry_t *pd_component_registry_get_entry_by_id(gpi_obj_id_t object_id)
{
    return (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), object_id);
}

pd_component_registry_entry_t *pd_component_registry_get_entry_by_badge(seL4_Word badge)
{
    return (pd_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_pd_component(), badge);
}

// Called when an item from the PD registry is deleted
static void on_pd_registry_delete(resource_registry_node_t *node_gen, void *arg)
{
    pd_component_registry_entry_t *node = (pd_component_registry_entry_t *)node_gen;

    resource_component_remove_from_rt(get_pd_component(), node->pd.id);

    // Destroy PD
    pd_destroy(&node->pd, get_pd_component()->server_vka, get_pd_component()->server_vspace);

    // Clear any pending work
    get_gpi_server()->model_extraction_n_missing -= node->pending_model_state->count;
    linked_list_destroy(node->pending_destroy, true);
    linked_list_destroy(node->pending_frees, true);
    linked_list_destroy(node->pending_model_state, true);
}

int pd_component_allocate(gpi_obj_id_t client_id, mo_t *init_data_mo, pd_t **ret_pd, seL4_CPtr *ret_cap)
{
    int error = 0;
    pd_component_registry_entry_t *new_entry;

    /* Allocate a new PD */
    error = resource_component_allocate(get_pd_component(), client_id, BADGE_OBJ_ID_NULL, false, init_data_mo,
                                        (resource_registry_node_t **)&new_entry, ret_cap);
    SERVER_GOTO_IF_ERR(error, "failed to allocate a PD\n");

    OSDB_PRINTF("Successfully allocated a new PD %u.\n", new_entry->pd.id);

    /* Initialize the registry entry */
    new_entry->pending_destroy = linked_list_new();
    new_entry->pending_frees = linked_list_new();
    new_entry->pending_model_state = linked_list_new();

    *ret_pd = &new_entry->pd;

err_goto:
    return error;
}

static void handle_pd_allocation(seL4_Word sender_badge, PdReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got connect request from badge %lx\n", sender_badge);
    int error = 0;
    seL4_CPtr ret_cap;
    pd_t *pd;
    gpi_obj_id_t client_id = get_client_id_from_badge(sender_badge);
    SERVER_GOTO_IF_COND(!sel4gpi_rpc_check_cap(GPICAP_TYPE_MO), "Did not receive MO cap\n");

    /* Find the MO to use for PD's OSmosis data */
    mo_component_registry_entry_t *osm_mo_entry = (mo_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_mo_component(), seL4_GetBadge(0));
    SERVER_GOTO_IF_COND_BG(osm_mo_entry == NULL, seL4_GetBadge(0), "Failed to find MO for OSmosis data: ");

    /* Allocate a new PD */
    error = pd_component_allocate(client_id, &osm_mo_entry->mo, &pd, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "failed to allocate a PD\n");

    OSDB_PRINTF("Successfully allocated a new PD %u.\n", pd->id);

    /* Return this badged end point in the return message. */
    reply_msg->msg.alloc.slot = ret_cap;
    reply_msg->msg.alloc.id = pd->id;

err_goto:
    reply_msg->which_msg = PdReturnMessage_alloc_tag;
    reply_msg->errorCode = error;
}

int pd_component_terminate(gpi_obj_id_t pd_id)
{
    int error = 0;

    /* Find the target PD */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_id(pd_id);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%u)\n", pd_id);

    /* Remove the PD from registry, this will also destroy the PD */
    client_data->pd.exit_code = PD_TERMINATED_CODE;
    client_data->pd.deletion_depth = 0; // This PD is the root of a deletion tree
    resource_registry_delete(&get_pd_component()->registry, (resource_registry_node_t *)client_data);

    OSDB_PRINTF("Cleaned up PD %u.\n", pd_id);

err_goto:
    return error;
}

static void handle_terminate_req(seL4_Word sender_badge, PdTerminateMessage *msg, PdReturnMessage *reply_msg,
                                 bool *should_reply)
{
    OSDB_PRINTF("Got terminate request from client badge %lx.\n", sender_badge);
    int error = 0;
    *should_reply = true;

    /* Check if a PD cleanup is already in progress */
    SERVER_GOTO_IF_COND(get_gpi_server()->pending_termination, "PD cleanup is already in progress\n");

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%u)\n", get_object_id_from_badge(sender_badge));

    /* Start the termination */
    get_gpi_server()->pending_termination = true;
    error = pd_component_terminate(get_object_id_from_badge(sender_badge));
    SERVER_GOTO_IF_ERR(error, "PD termination failed\n");

    /* If we are waiting on missing pieces, bookkeep and don't reply yet */
    if (get_gpi_server()->pd_termination_n_missing > 0)
    {
        printf("TEMPA waiting for %d pieces for termination\n", get_gpi_server()->pd_termination_n_missing);
        cspacepath_t reply_path;
        vka_cspace_alloc_path(get_pd_component()->server_vka, &reply_path);
        seL4_CNode_SaveCaller(reply_path.root, reply_path.capPtr, reply_path.capDepth);
        get_gpi_server()->pd_termination_reply = reply_path.capPtr;

        *should_reply = false;
    } else {
        printf("TEMPA reply immediately to termination\n");
        get_gpi_server()->pending_termination = false;
    }

err_goto:
    if (should_reply)
    {
        reply_msg->which_msg = PdReturnMessage_basic_tag;
        reply_msg->errorCode = error;
    }
}

static void handle_next_slot_req(seL4_Word sender_badge, PdNextSlotMessage *msg, PdReturnMessage *reply_msg)
{
    OSDB_PRINT_VERBOSE("Got next slot request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%u)\n", get_object_id_from_badge(sender_badge));

    seL4_CPtr slot;
    error = pd_next_slot(&client_data->pd,
                         &slot);

    reply_msg->msg.next_slot.slot = slot;

err_goto:
    reply_msg->which_msg = PdReturnMessage_next_slot_tag;
    reply_msg->errorCode = error;
}

static void handle_free_slot_req(seL4_Word sender_badge, PdFreeSlotMessage *msg, PdReturnMessage *reply_msg)
{
    OSDB_PRINT_VERBOSE("Got free slot request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%u)\n", get_object_id_from_badge(sender_badge));

    seL4_CPtr slot = msg->slot;

    // Ignore error from clear slot, error occurs if the slot was already empty
    pd_clear_slot(&client_data->pd, slot);
    error = pd_free_slot(&client_data->pd, slot);

err_goto:
    reply_msg->which_msg = PdReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_clear_slot_req(seL4_Word sender_badge, PdClearSlotMessage *msg, PdReturnMessage *reply_msg)
{
    OSDB_PRINT_VERBOSE("Got clear slot request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%u)\n", get_object_id_from_badge(sender_badge));

    seL4_CPtr slot = msg->slot;

    error = pd_clear_slot(&client_data->pd, slot);

err_goto:
    reply_msg->which_msg = PdReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_send_cap_req(seL4_Word sender_badge, PdSendCapMessage *msg, PdReturnMessage *reply_msg,
                                seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got send-cap request from client badge %lx.\n", sender_badge);
    int error = 0;

    /* This only works if the extra cap is a GPI core cap (badged version of GPI server EP) */
    OSDB_PRINT_VERBOSE("received_cap: %lu (badge: %lx)\n", received_cap, seL4_GetBadge(0));

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%u)\n", get_object_id_from_badge(sender_badge));

    /* Get the cap to send */
    seL4_Word received_caps_badge = seL4_GetBadge(0);
    bool is_core_cap = msg->is_core_cap;

    /* Send the cap to the target */
    seL4_CPtr slot;
    error = pd_send_cap(&client_data->pd,
                        received_cap,
                        received_caps_badge,
                        &slot,
                        true,
                        is_core_cap);

    reply_msg->msg.send_cap.slot = slot;

err_goto:
    reply_msg->which_msg = PdReturnMessage_send_cap_tag;
    reply_msg->errorCode = error;
}

static void handle_dump_cap_req(seL4_Word sender_badge, PdDumpMessage *msg,
                                PdReturnMessage *reply_msg, bool *should_reply)
{
    OSDB_PRINTF("Got dump-cap request from client badge %lx.\n", sender_badge);
    int error = 0;

    /* Check if a model extraction is already in progress */
    SERVER_GOTO_IF_COND(get_gpi_server()->pending_extraction, "Model extraction is already in progress\n");

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%u)\n", get_object_id_from_badge(sender_badge));

    /* Initialize the model state */
    model_state_t *ms = calloc(1, sizeof(model_state_t));
    init_model_state(ms, NULL, 0);
    get_gpi_server()->model_extraction_n_missing = 0;

    /* Start the extraction */
    error = pd_dump(&client_data->pd, ms);
    SERVER_GOTO_IF_ERR(error, "PD dump failed\n");

    /* If we are waiting on missing pieces, bookkeep the state and don't reply yet */
    if (get_gpi_server()->model_extraction_n_missing > 0)
    {
        get_gpi_server()->pending_extraction = true;
        get_gpi_server()->model_state = ms;

        cspacepath_t reply_path;
        vka_cspace_alloc_path(get_pd_component()->server_vka, &reply_path);
        seL4_CNode_SaveCaller(reply_path.root, reply_path.capPtr, reply_path.capDepth);
        get_gpi_server()->model_extraction_reply = reply_path.capPtr;

        *should_reply = false;
    }
    else
    {
        /* Print and free the model state */
        print_model_state(ms);
        destroy_model_state(ms);
    }

err_goto:
    reply_msg->which_msg = PdReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_share_rde_req(seL4_Word sender_badge, PdShareRDEMessage *msg, PdReturnMessage *reply_msg)
{
    int error = 0;

    gpi_cap_t type = msg->res_type;
    gpi_space_id_t space_id = msg->space_id;

    OSDB_PRINTF("share_rde_req: Got request from client badge %lx for RDE type %s with space %u.\n",
                sender_badge, cap_type_to_str(type), space_id);

    /* Find the source PD */
    gpi_obj_id_t client_id = get_client_id_from_badge(sender_badge);
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_id(client_id);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find client PD (%u)\n", client_id);

    /* Find the destination PD */
    pd_component_registry_entry_t *target_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(target_data == NULL, "Couldn't find target PD (%u)\n", get_object_id_from_badge(sender_badge));

    /* Find the source RDE */
    osmosis_rde_t *rde = pd_rde_get(&client_data->pd, type, space_id);
    SERVER_GOTO_IF_COND(rde == NULL, "share_rde_req: Failed to find RDE for type %u and space %u.\n", type, space_id);

    /* Check if RDE already exists in target */
    osmosis_rde_t *target_pd_rde = pd_rde_get(&target_data->pd, type, space_id);
    if (target_pd_rde != NULL)
    {
        printf("RDE already exists in target PD\n");
        goto err_goto;
    }

    /* Find the space for the RDE */
    resspc_component_registry_entry_t *resource_space_data = resource_space_get_entry_by_id(rde->space_id);
    SERVER_GOTO_IF_COND(resource_space_data == NULL,
                        "share_rde_req: Failed to find resource space ID %u.\n",
                        rde->space_id);

    /* Copy the RDE */
    rde_type_t rde_type = {.type = type};
    error = pd_add_rde(&target_data->pd,
                       rde_type,
                       client_data->pd.shared_data->type_names[type],
                       rde->space_id,
                       resource_space_data->space.server_ep);

err_goto:
    reply_msg->which_msg = PdReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_remove_rde_req(seL4_Word sender_badge, PdRemoveRDEMessage *msg, PdReturnMessage *reply_msg)
{
    int error = 0;

    gpi_cap_t type = msg->res_type;
    gpi_space_id_t space_id = msg->space_id;

    OSDB_PRINTF("remove_rde_req: Got request from client badge %lx for RDE type %u with space %u.\n",
                sender_badge, type, space_id);

    /* Find the client PD */
    gpi_obj_id_t client_id = get_client_id_from_badge(sender_badge);
    pd_component_registry_entry_t *target_data = pd_component_registry_get_entry_by_badge(sender_badge);

    SERVER_GOTO_IF_COND(target_data == NULL, "Couldn't find target PD (%u)\n", get_object_id_from_badge(sender_badge));

    /* Remove the RDE */
    rde_type_t rde_type = {.type = type};
    error = pd_remove_rde(&target_data->pd,
                          rde_type,
                          space_id);

err_goto:
    reply_msg->which_msg = PdReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_give_resource_req(seL4_Word sender_badge, PdGiveResourceMessage *msg, PdReturnMessage *reply_msg)
{
    int error = 0;

    gpi_obj_id_t server_id = get_object_id_from_badge(sender_badge);
    gpi_obj_id_t recipient_id = msg->pd_id;
    gpi_space_id_t space_id = msg->space_id;
    gpi_obj_id_t resource_id = msg->object_id;

    OSDB_PRINT_VERBOSE("Got give resource request from client badge %lx, space ID %u, resource ID %u.\n",
                       sender_badge, space_id, resource_id);

    /* Find the resource server PD */
    pd_component_registry_entry_t *server_data = pd_component_registry_get_entry_by_id(server_id);
    SERVER_GOTO_IF_COND(server_data == NULL, "Couldn't find server PD (%u)\n", server_id);

    /* Find the recipient PD */
    pd_component_registry_entry_t *recipient_data = pd_component_registry_get_entry_by_id(recipient_id);
    SERVER_GOTO_IF_COND(recipient_data == NULL, "Couldn't find target PD (%u)\n", recipient_id);

    /* Find the resource space */
    resspc_component_registry_entry_t *resource_space_data = resource_space_get_entry_by_id(space_id);
    SERVER_GOTO_IF_COND(resource_space_data == NULL, "Couldn't find resource space (%u)\n", space_id);

    /* Find the resource */
    gpi_badge_t res_node_id = compact_res_id(resource_space_data->space.resource_type, space_id, resource_id);
    pd_hold_node_t *resource_data = (pd_hold_node_t *)
        resource_registry_get_by_id(&server_data->pd.hold_registry, res_node_id);
    SERVER_GOTO_IF_COND(resource_data == NULL, "Couldn't find resource (%lx)\n", res_node_id);

    OSDB_PRINT_VERBOSE("resource server %u gives resource in space %u with ID %u to client %u\n",
                       server_id, space_id, resource_id, recipient_id);

    /* Create a new badged EP for the resource */
    seL4_CPtr dest = resource_component_make_badged_ep(get_pd_component()->server_vka, recipient_data->pd.pd_vka,
                                                       resource_space_data->space.server_ep,
                                                       resource_space_data->space.resource_type,
                                                       space_id, resource_id, recipient_id);
    reply_msg->msg.give_resource.slot = dest;

    // Add the resource to the PD object
    // (XXX) Arya: How to handle duplicate entries to the same resource?
    // The hash table is keyed by resource ID
    error = pd_add_resource(&recipient_data->pd,
                            make_res_id(resource_space_data->space.resource_type, space_id, resource_id),
                            seL4_CapNull, dest, seL4_CapNull);
    SERVER_GOTO_IF_ERR(error, "Failed to add resource to PD (%u)\n", recipient_id);

err_goto:
    reply_msg->which_msg = PdReturnMessage_give_resource_tag;
    reply_msg->errorCode = error;
}

#if TRACK_MAP_RELATIONS
int pd_component_map_resources(gpi_obj_id_t client_pd_id, gpi_obj_id_t src_res_id, gpi_obj_id_t dest_res_id)
{
    int error = 0;

    // Find the server PD
    pd_component_registry_entry_t *server_data = pd_component_registry_get_entry_by_id(client_pd_id);
    SERVER_GOTO_IF_COND(server_data == NULL, "Couldn't find server PD (%u)\n", client_pd_id);

    // Find the resources
    pd_hold_node_t *src_res = (pd_hold_node_t *)resource_registry_get_by_id(&server_data->pd.hold_registry,
                                                                            src_res_id);
    SERVER_GOTO_IF_COND(src_res == NULL, "Couldn't find resource (%lx)\n", src_res_id);
    pd_hold_node_t *dest_res = (pd_hold_node_t *)resource_registry_get_by_id(&server_data->pd.hold_registry,
                                                                             dest_res_id);
    SERVER_GOTO_IF_COND(dest_res == NULL, "Couldn't find resource (%lx)\n", dest_res_id);

    // Find the source space
    resspc_component_registry_entry_t *src_space_data = resource_space_get_entry_by_id(src_res->space_id);
    SERVER_GOTO_IF_COND(src_space_data == NULL, "Couldn't find resource space (%u)\n", src_res->space_id);

    // Confirm the mapping is valid
    SERVER_GOTO_IF_COND(client_pd_id != get_gpi_server()->rt_pd_id && src_space_data->space.pd->id != client_pd_id,
                        "PD (%u) can't map resource from a space (%u) it doesn't manage.\n",
                        client_pd_id, src_res->space_id);

    SERVER_GOTO_IF_COND(resspc_check_map(src_res->space_id, dest_res->space_id) != 1,
                        "Mapping a resource in space (%u) to a resource in space (%u) is not valid.\n",
                        src_res->space_id, dest_res->space_id);

    // (XXX) Arya: should we also track the mapping?

err_goto:
    return error;
}
#endif

#if TRACK_MAP_RELATIONS
static void handle_map_resource_req(seL4_Word sender_badge, PdMapResourceMessage *msg, PdReturnMessage *reply_msg)
{
    int error = 0;

    gpi_obj_id_t server_id = get_object_id_from_badge(sender_badge);
    gpi_obj_id_t src_res_id = msg->src_resource;
    gpi_obj_id_t dest_res_id = msg->dest_resource;

    OSDB_PRINTF("Got map resource request from client badge %lx, srd ID %u, dest ID %u.\n",
                sender_badge, src_res_id, dest_res_id);

    error = pd_component_map_resources(server_id, src_res_id, dest_res_id);

err_goto:
    reply_msg->which_msg = PdReturnMessage_give_resource_tag;
    reply_msg->errorCode = error;
}
#endif

static void handle_exit_req(seL4_Word sender_badge, PdExitMessage *msg)
{
    OSDB_PRINTF("Got exit request from client badge %lx, exit code %ld\n", sender_badge, msg->exit_code);
    int error = 0;

    /* Find the target PD */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%u)\n", get_object_id_from_badge(sender_badge));

    gpi_obj_id_t pd_id = client_data->pd.id;

    /* Remove the PD from registry, this will also destroy the PD */
    client_data->pd.exit_code = msg->exit_code;
    client_data->pd.deletion_depth = 0; // This PD is the root of a deletion tree
    resource_registry_delete(&get_pd_component()->registry, (resource_registry_node_t *)client_data);

    OSDB_PRINTF("Cleaned up exited PD (%u)\n", pd_id);
    return;

err_goto:
    OSDB_PRINTERR("Error while cleaning up exited PD (%u)\n", pd_id);
}

static void handle_ipc_bench_req(PdBenchIPCMessage *msg, PdReturnMessage *reply_msg, seL4_CPtr received_cap)
{
    int error = 0;

    // nothing to do
    cspacepath_t path;
    vka_cspace_make_path(get_pd_component()->server_vka, received_cap, &path);
    error = vka_cnode_delete(&path);
    SERVER_GOTO_IF_ERR(error, "failed to delete cap sent for IPC bench\n");

err_goto:
    reply_msg->which_msg = PdReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

int pd_component_runtime_setup(pd_t *pd,
                               ads_t *ads,
                               cpu_t *cpu,
                               PdSetupType setup_mode,
                               int argc,
                               seL4_Word *args,
                               void *stack_top,
                               void *entry_point,
                               void *ipc_buf_addr,
                               void *osm_shared_data)
{
    int error = 0;

    pd->shared_data_in_PD = osm_shared_data;

    if (setup_mode == PdSetupType_PD_RUNTIME_SETUP)
    {
        char string_args[argc][WORD_STRING_SIZE];
        char *argv[argc];

        for (int i = 0; i < argc; i++)
        {
            argv[i] = string_args[i];
            snprintf(argv[i], WORD_STRING_SIZE, "%" PRIuPTR "", args[i]);
        }

        void *init_stack;
        error = ads_write_arguments(pd, ads->vspace, ipc_buf_addr, stack_top,
                                    argc, argv, &init_stack);
        if (!error)
        {
            error = cpu_set_remote_context(cpu, entry_point, init_stack);
        }
    }
    else if (setup_mode == PdSetupType_PD_REGISTER_SETUP)
    {
        error = cpu_set_local_context(cpu,
                                      entry_point,
                                      argc > 0 ? (void *)args[0] : NULL,
                                      argc > 1 ? (void *)args[1] : NULL,
                                      argc > 2 ? (void *)args[2] : NULL,
                                      stack_top);
    }
    else if (setup_mode == PdSetupType_PD_GUEST_SETUP)
    {
        error = cpu_elevate(cpu);
        if (!error)
        {
            SERVER_GOTO_IF_COND(argc == 0, "Setting up a guest requires at least one argument for the DTB\n");
            error = cpu_set_guest_context(cpu, (uintptr_t)entry_point, (uintptr_t)args[0]);
        }
    }
    else
    {
        error = 1;
        OSDB_PRINTERR("Invalid PD setup mode specified\n");
    }

#if CONFIG_DEBUG_BUILD
    seL4_DebugNameThread(cpu->tcb.cptr, pd->name);
#endif

err_goto:
    return error;
}

static void handle_runtime_setup_req(seL4_Word sender_badge, PdSetupMessage *msg, PdReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got runtime setup request from client badge: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    SERVER_GOTO_IF_COND(!sel4gpi_rpc_check_caps_2(GPICAP_TYPE_ADS, GPICAP_TYPE_CPU), "Did not receive ADS & CPU cap\n");

    /* Find the target PD */
    pd_component_registry_entry_t *target_pd = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(target_pd == NULL, "Couldn't find target PD (%u)\n", get_object_id_from_badge(sender_badge));

    /* Find the target ADS */
    ads_component_registry_entry_t *target_ads = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), get_object_id_from_badge(seL4_GetBadge(0)));
    SERVER_GOTO_IF_COND(target_ads == NULL,
                        "Couldn't find target ADS (%u)\n",
                        get_object_id_from_badge(seL4_GetBadge(0)));

    /* Find the target CPU */
    cpu_component_registry_entry_t *target_cpu = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_cpu_component(), get_object_id_from_badge(seL4_GetBadge(1)));
    SERVER_GOTO_IF_COND(target_cpu == NULL,
                        "Couldn't find target CPU (%u)\n",
                        get_object_id_from_badge(seL4_GetBadge(1)));

    /* perform the setup */
    error = pd_component_runtime_setup(&target_pd->pd,
                                       &target_ads->ads,
                                       &target_cpu->cpu,
                                       msg->setup_mode,
                                       msg->args_count,
                                       (seL4_Word *)msg->args,
                                       (void *)msg->stack_top,
                                       (void *)msg->entry_point,
                                       (void *)msg->ipc_buf_addr,
                                       (void *)msg->osm_data_addr);
    SERVER_GOTO_IF_ERR(error, "Failed to setup PD\n");

err_goto:
    reply_msg->which_msg = PdReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_share_resource_type_req(seL4_Word sender_badge,
                                           PdShareResTypeMessage *msg, PdReturnMessage *reply_msg)
{
    int error = 0;
    OSDB_PRINTF("Got Share Resource Type Request: ");
    BADGE_PRINT(sender_badge);
    gpi_cap_t res_type = (gpi_cap_t)msg->res_type;

    SERVER_GOTO_IF_COND(!sel4gpi_rpc_check_cap(GPICAP_TYPE_PD), "Did not receive PD cap\n");

    /* Find the source PD */
    pd_component_registry_entry_t *src_pd_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND_BG(src_pd_data == NULL, sender_badge, "Failed to find source PD data ");

    /* Find the destination PD */
    seL4_Word dst_pd_badge = seL4_GetBadge(0);
    pd_component_registry_entry_t *dst_pd_data = pd_component_registry_get_entry_by_badge(dst_pd_badge);
    SERVER_GOTO_IF_COND_BG(dst_pd_data == NULL, dst_pd_badge, "Failed to find dest PD data ");

    /* Check for invalid sharing */
    SERVER_GOTO_IF_COND(src_pd_data->pd.id == dst_pd_data->pd.id,
                        "Invalid sharing of resources between the same PD (%u -> %u)\n",
                        src_pd_data->pd.id, dst_pd_data->pd.id);

    SERVER_GOTO_IF_COND(res_type != GPICAP_TYPE_MO && res_type < GPICAP_TYPE_seL4,
                        // (XXX) Arya: how to check for non-core resources?
                        "Sharing of resource type %s not permitted.\n",
                        cap_type_to_str(res_type));

    /* Share all resources of given type */
    linked_list_t *resources = pd_get_resources_of_type(&src_pd_data->pd, res_type);
    error = pd_bulk_add_resource(&dst_pd_data->pd, resources);
    SERVER_GOTO_IF_ERR(error, "Error occurred during resource sharing (some may still have been successful)\n");

    OSDB_PRINTF("Shared %s resources between PDs (%u -> %u)\n", cap_type_to_str(res_type),
                src_pd_data->pd.id, dst_pd_data->pd.id);

err_goto:
    if (resources)
    {
        linked_list_destroy(resources, false);
    }

    reply_msg->which_msg = PdReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_get_work_req(seL4_Word sender_badge, PdGetWorkMessage *msg, PdReturnMessage *reply_msg)
{
    int error = 0;
    OSDB_PRINTF("Got request for work: ");
    BADGE_PRINT(sender_badge);

    /* Find the target PD */
    pd_component_registry_entry_t *pd_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(pd_data == NULL, "Failed to find PD (%u)\n", get_object_id_from_badge(sender_badge));

    SERVER_GOTO_IF_COND(get_client_id_from_badge(sender_badge) != get_object_id_from_badge(sender_badge),
                        "Invalid request for work from a different PD (%u)\n",
                        get_client_id_from_badge(sender_badge));

    /* Return the next piece of work, if there is any */
    reply_msg->msg.work.action = PdWorkAction_NO_WORK;
    int n_object_ids = sizeof(reply_msg->msg.work.object_ids) / sizeof(reply_msg->msg.work.object_ids[0]);

    // Order gives the priority of different types of work
    linked_list_t *lists[3] = {pd_data->pending_model_state, pd_data->pending_destroy, pd_data->pending_frees};
    PdWorkAction work_types[3] = {PdWorkAction_EXTRACT, PdWorkAction_DESTROY, PdWorkAction_FREE};

    for (int i = 0; i < 3; i++)
    {
        pd_work_entry_t *work_res;

        // Check if the list has any content
        if (lists[i]->count > 0)
        {
            reply_msg->msg.work.action = work_types[i];
            int n_work = MIN(n_object_ids, lists[i]->count);
            reply_msg->msg.work.object_ids_count = n_work;
            reply_msg->msg.work.pd_ids_count = n_work;
            reply_msg->msg.work.space_ids_count = n_work;

            for (int j = 0; j < n_work; j++)
            {
                linked_list_pop_head(lists[i], (void **)&work_res);
                assert(work_res != NULL);
                reply_msg->msg.work.space_ids[j] = work_res->res_id.space_id;
                reply_msg->msg.work.object_ids[j] = work_res->res_id.object_id;
                reply_msg->msg.work.pd_ids[j] = work_res->client_pd_id;
                free(work_res);
            }

            break;
        }
    }

err_goto:
    reply_msg->which_msg = PdReturnMessage_work_tag;
    reply_msg->errorCode = error;
}

static void handle_send_subgraph_req(seL4_Word sender_badge, PdSendSubgraphMessage *msg, PdReturnMessage *reply_msg)
{
    int error = 0;

    OSDB_PRINTF("Got a subgraph from: ");
    BADGE_PRINT(sender_badge);

    bool has_data = msg->has_data;

    // (XXX) Arya: doesn't do any authentication, or check if we actually needed this piece
    // For simplicity, just decrement the counter of "remaining pieces"
    SERVER_GOTO_IF_COND(!get_gpi_server()->pending_extraction,
                        "Got subgraph when there is no pending model extraction\n");

    if (has_data)
    {
        SERVER_GOTO_IF_COND(!sel4gpi_rpc_check_cap(GPICAP_TYPE_MO), "Did not receive MO cap\n");

        /* Attach the included MO */
        seL4_Word mo_badge = seL4_GetBadge(0);
        SERVER_GOTO_IF_COND(get_cap_type_from_badge(mo_badge) != GPICAP_TYPE_MO, "Provided cap was not an MO\n");

        void *mo_vaddr;
        error = ads_component_attach_to_rt(get_object_id_from_badge(mo_badge), &mo_vaddr);
        SERVER_GOTO_IF_ERR(error, "Failed to attach MO to RT\n");

        // Update model state's pointers
        model_state_t *model_state = (model_state_t *)mo_vaddr;
        gpi_model_state_component_t *old_mem_start = model_state->mem_start;
        model_state->mem_start = (gpi_model_state_component_t *)(mo_vaddr + sizeof(model_state_t));
        model_state->mem_ptr = model_state->mem_ptr - old_mem_start + model_state->mem_start;

        // Combine with current model state
        combine_model_states(get_gpi_server()->model_state, model_state);

        /* Unattach the MO */
        error = ads_component_remove_from_rt(mo_vaddr);
        SERVER_GOTO_IF_ERR(error, "Failed to remove MO from RT\n");
    }

    // Update the pending model counter
    get_gpi_server()->model_extraction_n_missing -= msg->n_requests;

    /* Check if the extraction is finished */
    if (get_gpi_server()->model_extraction_n_missing == 0)
    {
        OSDB_PRINTF("Current model extraction is finished\n");

        // Print the model state
        print_model_state(get_gpi_server()->model_state);

        // Cleanup the model state
        destroy_model_state(get_gpi_server()->model_state);
        get_gpi_server()->pending_extraction = false;

        // Reply to the PD that requested the extraction
        PdReturnMessage dump_return_msg = {
            .which_msg = PdReturnMessage_basic_tag,
            .errorCode = PdComponentError_NONE};

        seL4_MessageInfo_t dump_return_tag;
        sel4gpi_rpc_reply(&get_pd_component()->rpc_env, (void *)&dump_return_msg, &dump_return_tag);
        seL4_Send(get_gpi_server()->model_extraction_reply, dump_return_tag);

        // Free the reply cap's slot
        vka_cspace_free(get_pd_component()->server_vka, get_gpi_server()->model_extraction_reply);
    }
    else
    {
        OSDB_PRINTF("Current model extraction is still missing %u pieces\n", get_gpi_server()->model_extraction_n_missing);
    }

err_goto:
    reply_msg->which_msg = PdReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_finish_work_req(seL4_Word sender_badge, PdFinishWorkMessage *msg, PdReturnMessage *reply_msg)
{
    int error = 0;

    OSDB_PRINTF("Got a 'finish work' message from: ");
    BADGE_PRINT(sender_badge);

    // (XXX) Arya: doesn't do any authentication, or check if we actually needed this piece
    // For simplicity, just decrement the counter of "remaining pieces"
    SERVER_GOTO_IF_COND(!get_gpi_server()->pending_termination,
                        "Got subgraph when there is no pending model extraction\n");

    // Update the pending termination counter
    get_gpi_server()->pd_termination_n_missing -= msg->n_requests;

    /* Check if the cleanup is finished */
    if (get_gpi_server()->pd_termination_n_missing == 0)
    {
        OSDB_PRINTF("Current PD cleanup is finished\n");

        get_gpi_server()->pending_termination = false;
        printf("TEMPA reply delayed to termination\n");

        // Reply to the PD that requested the termination
        PdReturnMessage return_msg = {
            .which_msg = PdReturnMessage_basic_tag,
            .errorCode = PdComponentError_NONE};

        seL4_MessageInfo_t return_tag;
        sel4gpi_rpc_reply(&get_pd_component()->rpc_env, (void *)&return_msg, &return_tag);
        seL4_Send(get_gpi_server()->pd_termination_reply, return_tag);

        // Free the reply cap's slot
        vka_cspace_free(get_pd_component()->server_vka, get_gpi_server()->pd_termination_reply);
    }
    else
    {
        OSDB_PRINTF("Current PD cleanup is still missing %u pieces\n", get_gpi_server()->model_extraction_n_missing);
    }

err_goto:
    reply_msg->which_msg = PdReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

#ifdef CONFIG_DEBUG_BUILD
static void handle_set_name_req(seL4_Word sender_badge, PdSetNameMessage *msg, PdReturnMessage *reply_msg)
{
    int error = 0;

    OSDB_PRINTF("Got a 'set name' request from: ");
    BADGE_PRINT(sender_badge);

    /* Find the target PD */
    pd_component_registry_entry_t *pd_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(pd_data == NULL, "Failed to find PD (%u)\n", get_object_id_from_badge(sender_badge));

    /* Set the image name */
    pd_set_name(&pd_data->pd, msg->pd_name);

err_goto:
    reply_msg->which_msg = PdReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}
#endif

static void pd_component_handle(void *msg_p,
                                seL4_Word sender_badge,
                                seL4_CPtr received_cap,
                                void *reply_msg_p,
                                bool *need_new_recv_cap,
                                bool *should_reply)
{
    int error = 0; // unused, to appease the error handling macros
    PdMessage *msg = (PdMessage *)msg_p;
    PdReturnMessage *reply_msg = (PdReturnMessage *)reply_msg_p;

    if (get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL)
    {
        SERVER_GOTO_IF_COND(msg->which_msg != PdMessage_alloc_tag,
                            "Received invalid request on the allocation endpoint: %u\n", msg->which_msg);
        handle_pd_allocation(sender_badge, reply_msg);
    }
    else
    {
        switch (msg->which_msg)
        {
        case PdMessage_terminate_tag:
            handle_terminate_req(sender_badge, &msg->msg.terminate, reply_msg, should_reply);
            break;
        case PdMessage_next_slot_tag:
            handle_next_slot_req(sender_badge, &msg->msg.next_slot, reply_msg);
            break;
        case PdMessage_free_slot_tag:
            handle_free_slot_req(sender_badge, &msg->msg.free_slot, reply_msg);
            break;
        case PdMessage_clear_slot_tag:
            handle_clear_slot_req(sender_badge, &msg->msg.clear_slot, reply_msg);
            break;
        case PdMessage_send_cap_tag:
            handle_send_cap_req(sender_badge, &msg->msg.send_cap, reply_msg, received_cap);
            *need_new_recv_cap = true;
            break;
        case PdMessage_dump_tag:
            handle_dump_cap_req(sender_badge, &msg->msg.dump, reply_msg, should_reply);
            break;
        case PdMessage_share_rde_tag:
            handle_share_rde_req(sender_badge, &msg->msg.share_rde, reply_msg);
            break;
        case PdMessage_remove_rde_tag:
            handle_remove_rde_req(sender_badge, &msg->msg.remove_rde, reply_msg);
            break;
        case PdMessage_give_resource_tag:
            handle_give_resource_req(sender_badge, &msg->msg.give_resource, reply_msg);
            break;
#if TRACK_MAP_RELATIONS
        case PD_FUNC_MAP_RES_REQ:
            handle_map_resource_req(sender_badge, &msg->msg.disconnect, reply_msg);
            break;
#endif
        case PdMessage_exit_tag:
            handle_exit_req(sender_badge, &msg->msg.exit);
            *should_reply = false;
            break;
        case PdMessage_bench_ipc_tag:
            handle_ipc_bench_req(&msg->msg.bench_ipc, reply_msg, received_cap);
            break;
        case PdMessage_setup_tag:
            handle_runtime_setup_req(sender_badge, &msg->msg.setup, reply_msg);
            break;
        case PdMessage_share_res_type_tag:
            handle_share_resource_type_req(sender_badge, &msg->msg.share_res_type, reply_msg);
            break;
        case PdMessage_get_work_tag:
            handle_get_work_req(sender_badge, &msg->msg.get_work, reply_msg);
            break;
        case PdMessage_send_subgraph_tag:
            handle_send_subgraph_req(sender_badge, &msg->msg.send_subgraph, reply_msg);
            break;
        case PdMessage_finish_work_tag:
            handle_finish_work_req(sender_badge, &msg->msg.finish_work, reply_msg);
            break;
#ifdef CONFIG_DEBUG_BUILD
        case PdMessage_set_name_tag:
            handle_set_name_req(sender_badge, &msg->msg.set_name, reply_msg);
            break;
#endif
        default:
            SERVER_GOTO_IF_COND(1, "Unknown request received: %u\n", msg->which_msg);
            break;
        }
    }

    OSDB_PRINT_VERBOSE("Returning from PD component with error code %u\n", reply_msg->errorCode);
    return;

err_goto:
    OSDB_PRINT_VERBOSE("Returning from PD component with error code %u\n", error);
    reply_msg->errorCode = error;
}

/** --- Functions callable by root task --- **/

int pd_component_initialize(vka_t *server_vka,
                            vspace_t *server_vspace,
                            vka_object_t server_ep_obj)
{
    int error = 0;

    // Create the default PD resource space
    resspc_component_registry_entry_t *space_entry;

    resspc_config_t resspc_config = {
        .type = GPICAP_TYPE_PD,
        .ep = get_gpi_server()->server_ep_obj.cptr,
        .pd_id = get_gpi_server()->rt_pd_id,
    };

    error = resource_component_allocate(get_resspc_component(), get_gpi_server()->rt_pd_id, BADGE_OBJ_ID_NULL, false, (void *)&resspc_config,
                                        (resource_registry_node_t **)&space_entry, NULL);
    assert(error == 0);

    // Initialize the component
    resource_component_initialize(get_pd_component(),
                                  GPICAP_TYPE_PD,
                                  space_entry->space.id,
                                  pd_component_handle,
                                  (int (*)(resource_component_object_t *, vka_t *, vspace_t *, void *))pd_new,
                                  on_pd_registry_delete,
                                  sizeof(pd_component_registry_entry_t),
                                  server_vka,
                                  server_vspace,
                                  server_ep_obj.cptr,
                                  &PdMessage_msg,
                                  &PdReturnMessage_msg);
}

void forge_pd_for_root_task(gpi_obj_id_t rt_id)
{
    pd_component_registry_entry_t *rt_entry = calloc(1, sizeof(pd_component_registry_entry_t));
    rt_entry->gen.object_id = rt_id;
    rt_entry->pd.id = rt_id;
    rt_entry->pd.pd_vka = get_gpi_server()->server_vka;
    resource_registry_insert(&get_pd_component()->registry, (resource_registry_node_t *)rt_entry);
}

int pd_add_resource_by_id(gpi_obj_id_t pd_id,
                          gpi_res_id_t res_id,
                          seL4_CPtr slot_in_RT,
                          seL4_CPtr slot_in_PD,
                          seL4_CPtr slot_in_serverPD)
{
    int error = 0;

    pd_component_registry_entry_t *client_pd_data = pd_component_registry_get_entry_by_id(pd_id);
    SERVER_GOTO_IF_COND(client_pd_data == NULL, "Couldn't find PD (%u) to add resource \n", pd_id);

    error = pd_add_resource(&client_pd_data->pd, res_id, slot_in_RT, slot_in_PD, slot_in_serverPD);

err_goto:
    return error;
}

int pd_component_remove_resource_from_rt(gpi_res_id_t res_id)
{
    int error = 0;

    // Get the root task PD
    pd_component_registry_entry_t *pd_entry = pd_component_registry_get_entry_by_id(get_gpi_server()->rt_pd_id);
    SERVER_GOTO_IF_COND(pd_entry == NULL,
                        "Couldn't find RT PD (%u) to remove resource \n",
                        get_gpi_server()->rt_pd_id);

    // Remove the resource from it
    error = pd_remove_resource(&pd_entry->pd, res_id);

err_goto:
    return error;
}

int pd_component_resource_cleanup(gpi_res_id_t res_id)
{
    int error = 0;

    // Iterate over all live PDs
    resource_registry_node_t *curr, *tmp;
    HASH_ITER(hh, get_pd_component()->registry.head, curr, tmp)
    {
        pd_component_registry_entry_t *pd_entry = (pd_component_registry_entry_t *)curr;

        if (pd_entry->pd.id == get_gpi_server()->rt_pd_id || pd_entry->pd.deleting)
        {
            // Skip a PD currently being deleted
            continue;
        }

        OSDB_PRINTF("Remove resource %s_%u_%u from PD(%u)\n", cap_type_to_str(res_id.type),
                    res_id.space_id, res_id.object_id, pd_entry->pd.id);

        error = pd_remove_resource(&pd_entry->pd, res_id);
        SERVER_GOTO_IF_ERR(error, "failed to remove resource %s_%u_%u from PD (%u)\n",
                           cap_type_to_str(res_id.type),
                           res_id.space_id, res_id.object_id, pd_entry->pd.id);
    }

err_goto:
    return error;
}

int pd_component_space_cleanup(gpi_obj_id_t pd_id, gpi_cap_t space_type,
                               gpi_space_id_t space_id, bool execute_cleanup_policy)
{
    int error = 0;

    OSDB_PRINTF("Starting to cleanup resource space %s_%u \n", cap_type_to_str(space_type), space_id);

    // Find the manager PD of this resource space
    pd_component_registry_entry_t *manager_data = pd_component_registry_get_entry_by_id(pd_id);
    SERVER_GOTO_IF_COND(manager_data == NULL, "couldn't find PD (%u) managing resource space (%u)", pd_id, space_id);
    int depth = manager_data->pd.deletion_depth;

    // Remove the space resource from the manager, if still live
    if (!manager_data->pd.deleting)
    {
        // Remove the resource space object
        gpi_res_id_t space_res_id = make_res_id(GPICAP_TYPE_RESSPC, get_resspc_component()->space_id, space_id);
        error = pd_remove_resource(&manager_data->pd, space_res_id);
        SERVER_GOTO_IF_ERR(error, "failed to remove resource space (%u) resource from PD (%u)\n",
                           space_id, pd_id);

        if (manager_data->pd.id != get_gpi_server()->rt_pd_id)
        {
            // Notify the server that its space is being deleted
            pd_work_entry_t *work_entry = calloc(1, sizeof(pd_work_entry_t));
            work_entry->res_id.type = space_type;
            work_entry->res_id.space_id = space_id;
            work_entry->res_id.object_id = BADGE_OBJ_ID_NULL;

            pd_component_queue_destroy_work(manager_data, work_entry);
        }
    }

    if (execute_cleanup_policy)
    {
        // Iterate over all live PDs to check if they should be deleted
        resource_registry_node_t *curr, *tmp;
        HASH_ITER(hh, get_pd_component()->registry.head, curr, tmp)
        {
            pd_component_registry_entry_t *pd_entry = (pd_component_registry_entry_t *)curr;
            pd_t *pd = &pd_entry->pd;

            if (pd->id == get_gpi_server()->rt_pd_id || pd->deleting)
            {
                // Skip the root task, or a PD currently being deleted
                continue;
            }

            // Check if we should delete this PD
            if (GPI_CLEANUP_PD_DEPTH == -1 || depth + 1 <= GPI_CLEANUP_PD_DEPTH)
            {
                // Within PD deletion depth
                // Check if the PD has a request edge for the deleted space, or holds any resource from the space
                if (pd_rde_get(pd, space_type, space_id) || pd_has_resources_in_space(pd, space_id))
                {
                    OSDB_PRINTF("Delete PD (%u) depending on %s_%u at depth %u\n", pd->id, cap_type_to_str(space_type),
                                space_id, depth + 1);

                    // Set the deletion depth
                    pd->deletion_depth = depth + 1;

                    // Remove the PD from registry, this will also destroy the PD
                    resource_registry_delete(&get_pd_component()->registry, curr);

                    continue;
                }
            }
        }
    }

    // Iterate over any live PDs to cleanup resources from the deleted space
    resource_registry_node_t *curr, *tmp;
    HASH_ITER(hh, get_pd_component()->registry.head, curr, tmp)
    {
        pd_component_registry_entry_t *pd_entry = (pd_component_registry_entry_t *)curr;
        pd_t *pd = &pd_entry->pd;

        if (pd->id == get_gpi_server()->rt_pd_id || pd->deleting)
        {
            // Skip the root task, or a PD currently being deleted
            continue;
        }

        OSDB_PRINTF("Cleanup resource space %s_%u in PD(%u)\n", cap_type_to_str(space_type),
                    space_id, pd->id);

        // Remove an RDE if there is one
        // Ignore errors if the RDE doesn't exist
        rde_type_t rde_type = {.type = space_type};
        pd_remove_rde(pd, rde_type, space_id);

        // Remove resources belonging to the deleted space
        error = pd_remove_resources_in_space(pd, space_id);
        SERVER_GOTO_IF_ERR(error, "failed to remove resources in %s_%u from PD (%u)\n",
                           cap_type_to_str(space_type),
                           space_id, pd->id);
    }

err_goto:
    return error;
}

void pd_component_queue_model_extraction_work(pd_component_registry_entry_t *pd_entry, pd_work_entry_t *work)
{
    OSDB_PRINTF("Requesting model subgraph from PD (%u)\n", pd_entry->pd.id);
    assert(work != NULL);

    // Add to the list
    linked_list_insert(pd_entry->pending_model_state, (void *)work);
    get_gpi_server()->model_extraction_n_missing++;

    // Notify the PD
    seL4_Signal(pd_entry->pd.badged_notification);
}

void pd_component_queue_destroy_work(pd_component_registry_entry_t *pd_entry, pd_work_entry_t *work)
{
    assert(work != NULL);

    if (get_gpi_server()->pending_termination)
    {
        get_gpi_server()->pd_termination_n_missing++;
    }

    // Add to the list
    linked_list_insert(pd_entry->pending_destroy, (void *)work);

    // Notify the PD
    seL4_Signal(pd_entry->pd.badged_notification);
}

void pd_component_queue_free_work(pd_component_registry_entry_t *pd_entry, pd_work_entry_t *work)
{
    assert(work != NULL);

    if (get_gpi_server()->pending_termination)
    {
        get_gpi_server()->pd_termination_n_missing++;
    }

    // Add to the list
    linked_list_insert(pd_entry->pending_frees, (void *)work);

    // Notify the PD
    seL4_Signal(pd_entry->pd.badged_notification);
}

seL4_CPtr pd_component_create_ipc_bench_ep(void)
{
    // Make a special badged endpoint just for IPC benchmark request
    seL4_Word badge = gpi_new_badge(GPICAP_TYPE_PD, 0, 0, 0, 0);

    seL4_CPtr slot = resource_component_make_badged_ep_custom(
        get_pd_component()->server_vka,
        get_pd_component()->server_vka,
        get_gpi_server()->server_ep_obj.cptr,
        badge);

    assert(slot != seL4_CapNull);

    return slot;
}
