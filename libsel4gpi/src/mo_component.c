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

    resource_server_initialize_registry(&component->mo_registry, NULL);
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

// (XXX): Somwehere here we should call mo_new
void mo_handle_allocation_request(seL4_Word sender_badge, seL4_MessageInfo_t *reply_tag)
{
    seL4_Word num_pages = seL4_GetMR(MOMSGREG_CONNECT_REQ_NUM_PAGES);
    OSDB_PRINTF(MO_DEBUG, MOSERVS "Got connect request for %ld pages\n", num_pages);

    /* Allocator numm_pages frame */
    mo_frame_t *frame_caps = malloc(sizeof(mo_frame_t) * num_pages);
    assert(frame_caps != NULL);

    vka_object_t frame_obj;
    for (int i = 0; i < num_pages; i++)
    {
        int error = vka_alloc_frame_maybe_device(get_mo_component()->server_vka,
                                                 seL4_PageBits,
                                                 false,
                                                 &frame_obj);
        assert(error == 0);
        frame_caps[i].cap = frame_obj.cptr;
        frame_caps[i].paddr = vka_object_paddr(get_mo_component()->server_vka, &frame_obj);
        // OSDB_PRINTF(MO_DEBUG, MOSERVS "%s %d: Allocated frame %lu\n", __FUNCTION__, __LINE__, frame_caps[i]);
    }

    /* Allocate a new registry entry for the client. */
    mo_component_registry_entry_t *client_reg_ptr = malloc(sizeof(mo_component_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        OSDB_PRINTF(MO_DEBUG, MOSERVS "main: Failed to allocate new badge for client.\n");
        return;
    }
    memset((void *)client_reg_ptr, 0, sizeof(mo_component_registry_entry_t));

    seL4_Word badge = resource_server_registry_badge_and_insert(&get_mo_component()->mo_registry, (resource_server_registry_node_t *)client_reg_ptr,
                                                                GPICAP_TYPE_MO, NSID_DEFAULT, &client_reg_ptr->mo.mo_obj_id);

    /* Createa a new MO object */
    /* Allocate frames */

    int error = mo_new(&client_reg_ptr->mo,
                       frame_caps,
                       num_pages,
                       get_mo_component()->server_vka);
    if (error)
    {
        OSDB_PRINTF(MO_DEBUG, MOSERVS "main: Failed to create new MO object\n");
        return;
    }
    free(frame_caps);

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src, dest;
    vka_cspace_make_path(get_mo_component()->server_vka,
                         get_mo_component()->server_ep_obj.cptr, &src);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_mo_component()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_mo_component()->server_vka, dest_cptr, &dest);

    // (XXX) Linh: this is not very nice as we're coupling the PD and MO components
    osmosis_pd_cap_t *res = pd_add_resource_by_id(get_client_id_from_badge(sender_badge), GPICAP_TYPE_MO, get_object_id_from_badge(badge));
    if (res)
    {
        res->slot_in_RT_Debug = dest_cptr;
        badge = set_client_id_to_badge(badge, get_client_id_from_badge(sender_badge));
    }

    error = vka_cnode_mint(&dest,
                           &src,
                           seL4_AllRights,
                           badge);
    if (error)
    {
        OSDB_PRINTF(MO_DEBUG, MOSERVS "main: Failed to mint client badge %lx.\n", badge);
        return;
    }
    OSDB_PRINTF(MO_DEBUG, MOSERVS "Minted MO with new badge: %lx\n", badge);
    /* Return this badged end point in the return message. */
    seL4_SetCap(0, dest.capPtr);
    seL4_SetMR(MOMSGREG_FUNC, MO_FUNC_CONNECT_ACK);
    seL4_SetMR(MOMSGREG_CONNECT_ACK_ID, get_object_id_from_badge(badge));
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, MOMSGREG_CONNECT_ACK_END);
    return reply(tag);
}

int forge_mo_caps_from_vspace(vspace_t *child_vspace,
                              ads_t *target_ads,
                              vka_t *vka,
                              uint32_t client_pd_id,
                              uint32_t *num_ret_caps,
                              seL4_CPtr *cap_ret,
                              uint64_t *id_ret)
{
    /* Walk every reservation */
    OSDB_PRINTF(MO_DEBUG, "%s: %d\n", __FUNCTION__, __LINE__);

    sel4utils_alloc_data_t *child_data = get_alloc_data(child_vspace);
    OSDB_PRINTF(MO_DEBUG, "%s: %d\n", __FUNCTION__, __LINE__);
    sel4utils_res_t *res = child_data->reservation_head;
    OSDB_PRINTF(MO_DEBUG, "%s: %d\n", __FUNCTION__, __LINE__);

    OSDB_PRINTF(MO_DEBUG, "forge_mo_caps_from_vspace: %d\n", __LINE__);
    while (res != NULL)
    {
        *num_ret_caps = *num_ret_caps + 1;
        OSDB_PRINTF(MO_DEBUG, "forge_mo_caps_from_vspace: %d\n", __LINE__);
        res = res->next;
    }

    /* For each reservation, forge an MO */
    int j = 0;
    res = child_data->reservation_head;
    OSDB_PRINTF(MO_DEBUG, "forge_mo_caps_from_vspace: %d\n", __LINE__);
    while (res != NULL)
    {

        OSDB_PRINTF(MO_DEBUG, "forge_mo_caps_from_vspace: %d\n", __LINE__);
        /* Get the caps in a reservation */
        uint32_t num_frames = (res->end - res->start) / PAGE_SIZE_4K;
        seL4_CPtr *frame_caps = malloc(sizeof(seL4_CPtr) * num_frames);
        assert(frame_caps != NULL);

        int i = 0;
        for (void *start = (void *)res->start;
             start < (void *)res->end;
             start += PAGE_SIZE_4K)
        {
            frame_caps[i] = vspace_get_cap(child_vspace, start);
            i++;
        }

        /* This way, we can call forge_mo_caps_from vspace again and again */
        if (res->mo_ref == NULL)
        {
            int error = forge_mo_cap_from_frames(frame_caps,
                                                 num_frames,
                                                 vka,
                                                 client_pd_id,
                                                 &cap_ret[j],
                                                 (mo_t **)&res->mo_ref);
            assert(error == 0);

            if (id_ret)
            {
                id_ret[j] = ((mo_t *)res->mo_ref)->mo_obj_id;
            }

            // Add the attach node for this region
            if (target_ads != NULL)
            {
                attach_node_t *attach_node = malloc(sizeof(attach_node_t));
                attach_node->mo_id = ((mo_t *)res->mo_ref)->mo_obj_id;
                ;
                attach_node->vaddr = (void *)(void *)res->start;
                attach_node->type = res->type;
                attach_node->n_pages = num_frames;
                attach_node->next = target_ads->attach_nodes;
                target_ads->attach_nodes = attach_node;
            }
        }

        res = res->next;
        j++;
    }

    /* Add the MO's cap to the cap_ret*/

    assert(*num_ret_caps < 10);
    return 0;
}

int forge_mo_cap_from_frames(seL4_CPtr *frame_caps,
                             uint32_t num_pages,
                             vka_t *vka,
                             uint32_t client_pd_id,
                             seL4_CPtr *cap_ret,
                             mo_t **mo_ret)
{

    assert(frame_caps != NULL);
    /* Allocate a new registry entry for the client. */
    mo_component_registry_entry_t *client_reg_ptr = malloc(sizeof(mo_component_registry_entry_t));
    if (client_reg_ptr == NULL)
    {
        OSDB_PRINTF(MO_DEBUG, MOSERVS "main: Failed to allocate new badge for client.\n");
        return 1;
    }
    memset((void *)client_reg_ptr, 0, sizeof(mo_component_registry_entry_t));

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src, dest;
    vka_cspace_make_path(get_mo_component()->server_vka,
                         get_mo_component()->server_ep_obj.cptr, &src);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_mo_component()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_mo_component()->server_vka, dest_cptr, &dest);

    /* Update the info in the registry entry. */
    seL4_Word badge = resource_server_registry_badge_and_insert(&get_mo_component()->mo_registry, (resource_server_registry_node_t *)client_reg_ptr,
                                                                GPICAP_TYPE_MO, NSID_DEFAULT, &client_reg_ptr->mo.mo_obj_id);

    client_reg_ptr->mo.frame_caps_in_root_task = malloc(sizeof(mo_frame_t) * num_pages);
    assert(client_reg_ptr->mo.frame_caps_in_root_task != NULL);

    // (XXX) A lot more will go here.
    for (int i = 0; i < num_pages; i++)
    {
        client_reg_ptr->mo.frame_caps_in_root_task[i].cap = frame_caps[i];

        // (XXX) Arya: Should we have a non-debug way to get paddr?
        client_reg_ptr->mo.frame_caps_in_root_task[i].paddr = seL4_DebugCapPaddr(frame_caps[i]);
    }
    client_reg_ptr->mo.num_pages = num_pages;

    int error = vka_cnode_mint(&dest,
                               &src,
                               seL4_AllRights,
                               badge);
    if (error)
    {
        OSDB_PRINTF(MO_DEBUG, MOSERVS "main: Failed to mint client badge %lx.\n", badge);
        return 1;
    }
    OSDB_PRINTF(MO_DEBUG, MOSERVS "main: Forged a new MO cap(EP: %lx) with badge value: %lx and %u pages.\n",
                dest.capPtr, badge, client_reg_ptr->mo.num_pages);

    *cap_ret = dest_cptr;
    *mo_ret = &client_reg_ptr->mo;
    return 0;
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
    enum mo_component_funcs func;
    seL4_Error error = 0;
    /* Post */
    func = seL4_GetMR(MOMSGREG_FUNC);
    switch (func)
    {
    default:
        gpi_panic(MOSERVS "Unknown func type.", (seL4_Word)func);
        break;
    }
}