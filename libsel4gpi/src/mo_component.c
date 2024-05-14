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

static inline seL4_MessageInfo_t recv(seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(get_mo_component()->server_ep_obj.cptr,
                    sender_badge_ptr,
                    get_mo_component()->server_thread.reply.cptr);
}

static inline void reply(seL4_MessageInfo_t tag)
{
    api_reply(get_mo_component()->server_thread.reply.cptr, tag);
}

// Called when an item from the MO registry is deleted
static void on_mo_registry_delete(resource_server_registry_node_t *node_gen)
{
    mo_component_registry_entry_t *node = (mo_component_registry_entry_t *)node_gen;

    OSDB_PRINTF("Destroying MO (%d)\n", node->mo.mo_obj_id);

    mo_destroy(&node->mo, get_mo_component()->server_vka);
}

int mo_component_initialize(simple_t *server_simple,
                            vka_t *server_vka,
                            seL4_CPtr server_cspace,
                            vspace_t *server_vspace,
                            sel4utils_thread_t server_thread,
                            vka_object_t server_ep_obj)
{
    mo_component_context_t *component = get_mo_component();

    component->server_simple = server_simple;
    component->server_vka = server_vka;
    component->server_cspace = server_cspace;
    component->server_vspace = server_vspace;
    component->server_thread = server_thread;
    component->server_ep_obj = server_ep_obj;

    resource_server_initialize_registry(&component->mo_registry, on_mo_registry_delete);
}

/**
 * @brief Lookup the client registry entry for the give badge.
 *
 * @param badge
 * @return mo_component_registry_entry_t*
 */
mo_component_registry_entry_t *mo_component_registry_get_entry_by_badge(seL4_Word badge)
{
    return (mo_component_registry_entry_t *)resource_server_registry_get_by_badge(&get_mo_component()->mo_registry, badge);
}

/**
 * @brief Lookup the client registry entry for the given objectID
 *
 * @param res_id
 * @return ads_component_registry_entry_t*
 */
mo_component_registry_entry_t *mo_component_registry_get_entry_by_id(seL4_Word object_id)
{
    return (mo_component_registry_entry_t *)resource_server_registry_get_by_id(&get_mo_component()->mo_registry, object_id);
}

int mo_component_allocate_mo(uint64_t client_id, bool forge, int num_pages, mo_component_registry_entry_t **ret_entry, seL4_CPtr *ret_cap)
{
    int error = 0;

    /* Create the registry entry */
    mo_component_registry_entry_t *client_reg_ptr = malloc(sizeof(mo_component_registry_entry_t));
    SERVER_GOTO_IF_COND(client_reg_ptr == NULL, "malloc ran out of memory to allocate MO registry entry\n");
    memset((void *)client_reg_ptr, 0, sizeof(mo_component_registry_entry_t));

    client_reg_ptr->mo.mo_obj_id = resource_server_registry_insert_new_id(&get_mo_component()->mo_registry, (resource_server_registry_node_t *)client_reg_ptr);
    *ret_entry = client_reg_ptr;

    /* Create the MO object */
    if (!forge)
    {
        /* Allocate frames */
        seL4_CPtr *frame_caps = malloc(sizeof(seL4_CPtr) * num_pages);
        vka_object_t *vka_objects = malloc(sizeof(vka_object_t) * num_pages);
        SERVER_GOTO_IF_COND(frame_caps == NULL || vka_objects == NULL, "malloc ran out of memory to allocate MO frames\n");

        for (int i = 0; i < num_pages; i++)
        {
            error = vka_alloc_frame_maybe_device(get_mo_component()->server_vka,
                                                 seL4_PageBits,
                                                 false,
                                                 &vka_objects[i]);
            assert(error == 0);
            frame_caps[i] = vka_objects[i].cptr;
        }

        /* Create a new MO object */
        error = mo_new(&client_reg_ptr->mo,
                       frame_caps,
                       vka_objects,
                       num_pages,
                       get_mo_component()->server_vka);

        free(frame_caps);
        free(vka_objects);

        // (XXX) Arya: Do some cleanup here
        SERVER_GOTO_IF_ERR(error, "Failed to initialize new MO object\n");
    }

    /* Create the badged endpoint */
    *ret_cap = resource_server_make_badged_ep(get_mo_component()->server_vka, NULL, get_mo_component()->server_ep_obj.cptr,
                                              client_reg_ptr->mo.mo_obj_id, GPICAP_TYPE_MO, NSID_DEFAULT, client_id);

    SERVER_GOTO_IF_COND(ret_cap == seL4_CapNull, "Failed to make badged ep for new MO\n");

    /* Add the resource to the client */
    error = pd_add_resource_by_id(client_id, GPICAP_TYPE_MO, client_reg_ptr->mo.mo_obj_id, NSID_DEFAULT, *ret_cap, seL4_CapNull, *ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to initialize add MO resource to PD\n");

err_goto:
    return error;
}

void mo_handle_allocation_request(seL4_Word sender_badge, seL4_MessageInfo_t *reply_tag)
{
    OSDB_PRINTF("Got MO allocation request from %lx\n", sender_badge);
    badge_print(sender_badge);

    int error = 0;
    seL4_CPtr ret_cap;
    mo_component_registry_entry_t *new_entry;
    uint32_t client_id = get_client_id_from_badge(sender_badge);
    seL4_Word num_pages = seL4_GetMR(MOMSGREG_CONNECT_REQ_NUM_PAGES);

    OSDB_PRINTF("Got connect request for %ld pages\n", num_pages);

    error = mo_component_allocate_mo(client_id, false, num_pages, &new_entry, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new MO object\n");

    OSDB_PRINTF("Successfully allocated a new MO.\n");

    /* Return this badged end point in the return message. */
    seL4_SetCap(0, ret_cap);
    seL4_SetMR(MOMSGREG_CONNECT_ACK_ID, new_entry->mo.mo_obj_id);

err_goto:
    seL4_SetMR(MOMSGREG_FUNC, MO_FUNC_CONNECT_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, MOMSGREG_CONNECT_ACK_END);
    return reply(tag);
}

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
    error = mo_component_allocate_mo(client_pd_id, true, num_pages, &new_entry, cap_ret);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new MO object for forge\n");

    /* Update the MO object from frames */
    // Without the VKA objects, we won't be able to free these frames
    error = mo_new(&new_entry->mo, frame_caps, NULL, num_pages, vka);
    SERVER_GOTO_IF_ERR(error, "Failed to initialize new MO object for forge\n");

    OSDB_PRINTF("Forged a new MO cap(EP: %d) with %d pages.\n",
                (int)*cap_ret, new_entry->mo.num_pages);

    *mo_ret = &new_entry->mo;

err_goto:
    return error;
}

/**
 * @brief The starting point for the cpu server's thread.
 *
 */
void mo_component_handle(seL4_MessageInfo_t tag,
                         seL4_Word sender_badge,
                         cspacepath_t *received_cap,
                         seL4_MessageInfo_t *reply_tag) /* reply_tag not used right now*/
{
    enum mo_component_funcs func = seL4_GetMR(MOMSGREG_FUNC);

    switch (func)
    {
    default:
        gpi_panic(MOSERVS "Unknown func type.", (seL4_Word)func);
        break;
    }
}

/** --- Functions callable by root task --- **/

int mo_component_dec(uint64_t mo_id)
{
    int error = 0;

    mo_component_registry_entry_t *client_data = mo_component_registry_get_entry_by_id(mo_id);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find MO (%ld)\n", mo_id);

    OSDB_PRINTF("Decrementing MO (%ld), refcount %d\n", mo_id, client_data->gen.count);

    resource_server_registry_dec(&get_mo_component()->mo_registry, (resource_server_registry_node_t *) client_data);

err_goto:
    return error;
}