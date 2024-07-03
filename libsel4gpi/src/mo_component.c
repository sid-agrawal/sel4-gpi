/**
 * @file mo_component.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the cpu server API from mo_component.h.
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

#include <sel4gpi/mo_component.h>
#include <sel4gpi/mo_clientapi.h>

#include <sel4gpi/gpi_server.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/gpi_rpc.h>
#include <mo_component_rpc.pb.h>

// Defined for utility printing macros
#define DEBUG_ID MO_DEBUG
#define SERVER_ID MOSERVS

resource_component_context_t *get_mo_component(void)
{
    return &get_gpi_server()->mo_component;
}

// Called when an item from the MO registry is deleted
static void on_mo_registry_delete(resource_registry_node_t *node_gen, void *arg)
{
    mo_component_registry_entry_t *node = (mo_component_registry_entry_t *)node_gen;

    OSDB_PRINTF("Destroying MO (%d)\n", node->mo.id);

    // Destroy the MO
    mo_destroy(&node->mo, get_mo_component()->server_vka);
}

int mo_component_allocate_rt(int num_pages, mo_t **ret_mo)
{
    int error = 0;
    mo_component_registry_entry_t *new_entry;

    mo_new_args_t alloc_args = {.num_pages = num_pages, .paddr = 0, .page_bits = MO_PAGE_BITS};

    error = resource_component_allocate(
        get_mo_component(),
        get_gpi_server()->rt_pd_id,
        BADGE_OBJ_ID_NULL,
        false,
        (void *)&alloc_args,
        (resource_registry_node_t **)&new_entry, NULL);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new MO object for RT\n");

    OSDB_PRINTF("Root task allocated a new MO (%d) with %d pages.\n",
                new_entry->mo.id, new_entry->mo.num_pages);

    *ret_mo = &new_entry->mo;

err_goto:
    return error;
}

static void handle_mo_allocation_request(seL4_Word sender_badge,
                                         MoAllocMessage *msg, MoReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got MO allocation request from %lx\n", sender_badge);
    BADGE_PRINT(sender_badge);

    int error = 0;
    seL4_CPtr ret_cap;
    mo_component_registry_entry_t *new_entry;
    uint32_t client_id = get_client_id_from_badge(sender_badge);
    seL4_Word num_pages = msg->num_pages;
    uintptr_t paddr = msg->phys_addr;
    size_t page_bits = msg->page_bits;

    OSDB_PRINTF("Got connect request for %ld pages of size: %zu\n", num_pages, SIZE_BITS_TO_BYTES(page_bits));

    if (paddr)
    {
        OSDB_PRINTF("Sender requested specific paddr: %lx\n", paddr);
    }

    mo_new_args_t alloc_args = {.num_pages = num_pages, .paddr = paddr, .page_bits = page_bits};

    error = resource_component_allocate(get_mo_component(), client_id, BADGE_OBJ_ID_NULL, false, (void *)&alloc_args,
                                        (resource_registry_node_t **)&new_entry, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new MO object\n");

    OSDB_PRINTF("Allocated a new MO (%d) with %d pages.\n",
                new_entry->mo.id, new_entry->mo.num_pages);

    /* Return this badged end point in the return message. */
    reply_msg->msg.alloc.id = new_entry->mo.id;
    reply_msg->msg.alloc.slot = ret_cap;

err_goto:
    reply_msg->which_msg = MoReturnMessage_alloc_tag;
    reply_msg->errorCode = error;
}

static void handle_mo_disconnect_request(seL4_Word sender_badge,
                                         MoDisconnectMessage *msg, MoReturnMessage *reply_msg)
{
    int error = 0;

    OSDB_PRINTF("Got MO disconnect request from %lx\n", sender_badge);
    BADGE_PRINT(sender_badge);

    uint64_t mo_id = get_object_id_from_badge(sender_badge);

    /* Find the PD */
    pd_component_registry_entry_t *pd_data =
        pd_component_registry_get_entry_by_id(get_client_id_from_badge(sender_badge));
    SERVER_GOTO_IF_COND(pd_data == NULL, "Couldn't find PD (%ld)\n", get_client_id_from_badge(sender_badge));

    /* Remove the MO from the client PD */
    error = pd_remove_resource(&pd_data->pd, make_res_id(GPICAP_TYPE_MO, get_mo_component()->space_id, mo_id));
    SERVER_GOTO_IF_ERR(error, "Failed to remove MO from PD\n");

    // This will reduce the refcount of the MO, and then it will be deleted if necessary

err_goto:
    reply_msg->which_msg = MoReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void mo_component_handle(void *msg_p,
                                seL4_Word sender_badge,
                                seL4_CPtr received_cap,
                                void *reply_msg_p,
                                bool *need_new_recv_cap,
                                bool *should_reply)
{
    int error = 0; // unused, to appease the error handling macros
    MoMessage *msg = (MoMessage *)msg_p;
    MoReturnMessage *reply_msg = (MoReturnMessage *)reply_msg_p;

    if (get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL)
    {
        SERVER_GOTO_IF_COND(msg->which_msg != MoMessage_alloc_tag,
                            "Received invalid request on the allocation endpoint\n");
        handle_mo_allocation_request(sender_badge, &msg->msg.alloc, reply_msg);
    }
    else
    {
        switch (msg->which_msg)
        {
        case MoMessage_disconnect_tag:
            handle_mo_disconnect_request(sender_badge, &msg->msg.disconnect, reply_msg);
            break;
        default:
            SERVER_GOTO_IF_COND(1, "Unknown request received: %d\n", msg->which_msg);
            break;
        }
    }

    OSDB_PRINTF("Returning from MO component with error code %d\n", reply_msg->errorCode);
    return;

err_goto:
    OSDB_PRINTF("Returning from MO component with error code %d\n", error);
    reply_msg->errorCode = error;
}

int mo_component_initialize(vka_t *server_vka,
                            vspace_t *server_vspace,
                            vka_object_t server_ep_obj)
{
    int error = 0;

    // Create the default MO resource space
    resspc_component_registry_entry_t *space_entry;

    resspc_config_t resspc_config = {
        .type = GPICAP_TYPE_MO,
        .ep = get_gpi_server()->server_ep_obj.cptr,
        .pd_id = get_gpi_server()->rt_pd_id,
    };

    error = resource_component_allocate(get_resspc_component(), get_gpi_server()->rt_pd_id, BADGE_OBJ_ID_NULL, false,
                                        (void *)&resspc_config, (resource_registry_node_t **)&space_entry, NULL);
    assert(error == 0);

    // Initialize the component
    resource_component_initialize(get_mo_component(),
                                  GPICAP_TYPE_MO,
                                  space_entry->space.id,
                                  mo_component_handle,
                                  (int (*)(resource_component_object_t *, vka_t *, vspace_t *, void *))mo_new,
                                  on_mo_registry_delete,
                                  sizeof(mo_component_registry_entry_t),
                                  server_vka,
                                  server_vspace,
                                  server_ep_obj.cptr,
                                  &MoMessage_msg,
                                  &MoReturnMessage_msg);
}

/** --- Functions callable by root task --- **/

int forge_mo_cap_from_frames(seL4_CPtr *frame_caps,
                             uint32_t num_pages,
                             uint32_t client_pd_id,
                             seL4_CPtr *cap_ret,
                             mo_t **mo_ret)
{
    OSDB_PRINTF("Forging MO cap from frames\n");

    assert(frame_caps != NULL);

    int error = 0;
    mo_component_registry_entry_t *new_entry;

    /* Allocate the MO object */
    error = resource_component_allocate(get_mo_component(), client_pd_id, BADGE_OBJ_ID_NULL, true, NULL,
                                        (resource_registry_node_t **)&new_entry, cap_ret);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new MO object for forge\n");
    mo_t *mo = &new_entry->mo;

    /* Update the MO object from frames */
    // Without the VKA objects, we won't be able to free these frames
    mo->frame_caps_in_root_task = malloc(num_pages * sizeof(seL4_CPtr));
    mo->frame_paddrs = malloc(num_pages * sizeof(uintptr_t));
    assert(mo->frame_caps_in_root_task != NULL);
    assert(mo->frame_paddrs != NULL);

    mo->page_bits = MO_PAGE_BITS;
    mo->num_pages = num_pages;
    for (int i = 0; i < num_pages; i++)
    {
        // We cannot assert this, may be forging an MO from a reservation
        // that is not fully backed
        // assert(caps[i] != seL4_CapNull);
        mo->frame_caps_in_root_task[i] = frame_caps[i];

        mo->frame_paddrs[i] = seL4_GetCapPaddr(frame_caps[i]);
    }

    SERVER_GOTO_IF_ERR(error, "Failed to initialize new MO object for forge\n");

    /* The root task holds the MO by default */
    error = pd_add_resource_by_id(get_gpi_server()->rt_pd_id,
                                  make_res_id(GPICAP_TYPE_MO, get_mo_component()->space_id, mo->id),
                                  seL4_CapNull, seL4_CapNull, seL4_CapNull);

    SERVER_GOTO_IF_ERR(error, "Failed to add new MO to root task\n");

    OSDB_PRINTF("Forged a new MO (%d) with %d pages.\n",
                mo->id, new_entry->mo.num_pages);

    *mo_ret = &new_entry->mo;

err_goto:
    return error;
}