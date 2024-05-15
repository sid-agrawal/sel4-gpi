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

// Defined for utility printing macros
#define DEBUG_ID MO_DEBUG
#define SERVER_ID MOSERVS

mo_component_context_t *get_mo_component(void)
{
    return &get_gpi_server()->mo_component;
}

// Called when an item from the MO registry is deleted
static void on_mo_registry_delete(resource_server_registry_node_t *node_gen)
{
    mo_component_registry_entry_t *node = (mo_component_registry_entry_t *)node_gen;

    OSDB_PRINTF("Destroying MO (%d)\n", node->mo.id);

    mo_destroy(&node->mo, get_mo_component()->server_vka);
}

/**
 * @brief Lookup the client registry entry for the give badge.
 *
 * @param badge
 * @return mo_component_registry_entry_t*
 */
mo_component_registry_entry_t *mo_component_registry_get_entry_by_badge(seL4_Word badge)
{
    return (mo_component_registry_entry_t *)resource_server_registry_get_by_badge(&get_mo_component()->registry, badge);
}

/**
 * @brief Lookup the client registry entry for the given objectID
 *
 * @param res_id
 * @return ads_component_registry_entry_t*
 */
mo_component_registry_entry_t *mo_component_registry_get_entry_by_id(seL4_Word object_id)
{
    return (mo_component_registry_entry_t *)resource_server_registry_get_by_id(&get_mo_component()->registry, object_id);
}

static seL4_MessageInfo_t handle_mo_allocation_request(seL4_Word sender_badge)
{
    OSDB_PRINTF("Got MO allocation request from %lx\n", sender_badge);
    badge_print(sender_badge);

    int error = 0;
    seL4_CPtr ret_cap;
    mo_component_registry_entry_t *new_entry;
    uint32_t client_id = get_client_id_from_badge(sender_badge);
    seL4_Word num_pages = seL4_GetMR(MOMSGREG_CONNECT_REQ_NUM_PAGES);

    OSDB_PRINTF("Got connect request for %ld pages\n", num_pages);

    error = resource_component_allocate(get_mo_component(), client_id, false, (void *) num_pages,
                                        (resource_server_registry_node_t **)&new_entry, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new MO object\n");

    OSDB_PRINTF("Successfully allocated a new MO.\n");

    /* Return this badged end point in the return message. */
    seL4_SetCap(0, ret_cap);
    seL4_SetMR(MOMSGREG_CONNECT_ACK_ID, new_entry->mo.id);

err_goto:
    seL4_SetMR(MOMSGREG_FUNC, MO_FUNC_CONNECT_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, MOMSGREG_CONNECT_ACK_END);
    return tag;
}

static seL4_MessageInfo_t mo_component_handle(seL4_MessageInfo_t tag,
                                              seL4_Word sender_badge,
                                              seL4_CPtr received_cap,
                                              bool *need_new_recv_cap)
{
    enum mo_component_funcs func = seL4_GetMR(MOMSGREG_FUNC);
    seL4_MessageInfo_t reply_tag;

    if (get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL)
    {
        reply_tag = handle_mo_allocation_request(sender_badge);
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

int mo_component_initialize(simple_t *server_simple,
                            vka_t *server_vka,
                            seL4_CPtr server_cspace,
                            vspace_t *server_vspace,
                            sel4utils_thread_t server_thread,
                            vka_object_t server_ep_obj)
{
    resource_component_initialize(get_mo_component(),
                                  GPICAP_TYPE_MO,
                                  mo_component_handle,
                                  (int (*)(resource_component_object_t *, vka_t *, vspace_t *, void *))mo_new,
                                  on_mo_registry_delete,
                                  sizeof(mo_component_registry_entry_t),
                                  server_simple,
                                  server_vka,
                                  server_cspace,
                                  server_vspace,
                                  server_thread,
                                  server_ep_obj.cptr);
}

/** --- Functions callable by root task --- **/

int forge_mo_cap_from_frames(seL4_CPtr *frame_caps,
                             uint32_t num_pages,
                             vka_t *vka,
                             uint32_t client_pd_id,
                             seL4_CPtr *cap_ret,
                             mo_t **mo_ret)
{
    OSDB_PRINTF("Forging MO cap from frames\n");

    assert(frame_caps != NULL);

    int error = 0;
    mo_component_registry_entry_t *new_entry;

    /* Allocate the MO object */
    error = resource_component_allocate(get_mo_component(), client_pd_id, true, NULL,
                                        (resource_server_registry_node_t **)&new_entry, cap_ret);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new MO object for forge\n");
    mo_t *mo = &new_entry->mo;

    /* Update the MO object from frames */
    // Without the VKA objects, we won't be able to free these frames
    mo->frame_caps_in_root_task = malloc(num_pages * sizeof(seL4_CPtr));
    mo->frame_paddrs = malloc(num_pages * sizeof(uintptr_t));
    assert(mo->frame_caps_in_root_task != NULL);
    assert(mo->frame_paddrs != NULL);

    mo->num_pages = num_pages;
    for (int i = 0; i < num_pages; i++)
    {
        // We cannot assert this, may be forging an MO from a reservation
        // that is not fully backed
        // assert(caps[i] != seL4_CapNull);

        mo->frame_caps_in_root_task[i] = frame_caps[i];

        // (XXX) Arya: Should we have a non-debug function for this?
        mo->frame_paddrs[i] = seL4_DebugCapPaddr(frame_caps[i]);
    }

    SERVER_GOTO_IF_ERR(error, "Failed to initialize new MO object for forge\n");

    OSDB_PRINTF("Forged a new MO cap(EP: %d) with %d pages.\n",
                (int)*cap_ret, new_entry->mo.num_pages);

    *mo_ret = &new_entry->mo;

err_goto:
    return error;
}

int mo_component_dec(uint64_t mo_id)
{
    int error = 0;

    mo_component_registry_entry_t *client_data = mo_component_registry_get_entry_by_id(mo_id);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find MO (%ld)\n", mo_id);

    OSDB_PRINTF("Decrementing MO (%ld), refcount %d\n", mo_id, client_data->gen.count);

    resource_server_registry_dec(&get_mo_component()->registry, (resource_server_registry_node_t *)client_data);

err_goto:
    return error;
}