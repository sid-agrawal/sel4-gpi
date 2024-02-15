/**
 * @file ads_component.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the ads server API from ads_component.h.
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

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/ads_component.h>
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>


uint64_t ads_assign_new_badge_and_objectID(ads_component_registry_entry_t *reg) {
    get_ads_component()->registry_n_entries++;
    // Add the latest ID to the obj and to the badlge.
    seL4_Word badge_val = gpi_new_badge(GPICAP_TYPE_ADS,
                                        0x00,
                                        0x00,
                                        get_ads_component()->registry_n_entries);

    assert(badge_val != 0);
    reg->ads.ads_obj_id = get_ads_component()->registry_n_entries;
    OSDB_PRINTF(ADSSERVS"ads_assign_new_badge_and_objectID: new badge: %lx\n", badge_val);
    return badge_val;
}

ads_component_context_t *get_ads_component(void)
{
    return &get_gpi_server()->ads_component;
}

static inline seL4_MessageInfo_t recv(seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(get_ads_component()->server_ep_obj.cptr,
                    sender_badge_ptr,
                    get_ads_component()->server_thread.reply.cptr);
}

static inline void reply(seL4_MessageInfo_t tag)
{
    api_reply(get_ads_component()->server_thread.reply.cptr, tag);
}


/**
 * @brief Insert a new client into the client registry Linked List.
 *
 * @param new_node
 */
static void ads_component_registry_insert(ads_component_registry_entry_t *new_node) {
        // TODO:Use a mutex


    ads_component_registry_entry_t *head = get_ads_component()->client_registry;

    if (head == NULL) {
        get_ads_component()->client_registry = new_node;
        new_node->next = NULL;
        return;
    }

    while (head->next != NULL) {
        head = head->next;
    }
    head->next = new_node;
    new_node->next = NULL;
}

/**
 * @brief Lookup the client registry entry for the given objectID in the badge.
 *
 * @param badge
 * @return ads_component_registry_entry_t*
 */
ads_component_registry_entry_t *ads_component_registry_get_entry_by_badge(seL4_Word badge){

    uint64_t objectID = get_object_id_from_badge(badge);
    ads_component_registry_entry_t *current_ctx = get_ads_component()->client_registry;

    while (current_ctx != NULL) {
        if (current_ctx->ads.ads_obj_id == objectID) {
            break;
        }
        current_ctx = current_ctx->next;
    }
    return current_ctx;
}

/**
 * @brief Lookup the client registry entry for the given objectID
 *
 * @param res_id
 * @return ads_component_registry_entry_t*
 */
ads_component_registry_entry_t *ads_component_registry_get_entry_by_id(seL4_Word objectID){

    ads_component_registry_entry_t *current_ctx = get_ads_component()->client_registry;

    while (current_ctx != NULL) {
        if (current_ctx->ads.ads_obj_id == objectID) {
            break;
        }
        current_ctx = current_ctx->next;
    }
    return current_ctx;
}
void ads_handle_allocation_request(seL4_MessageInfo_t *reply_tag)
{
    OSDB_PRINTF(ADSSERVS "main: Got ADS connect request\n");

    /* Allocate a new registry entry for the client. */
    ads_component_registry_entry_t *client_reg_ptr = malloc(sizeof(ads_component_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        OSDB_PRINTF(ADSSERVS "main: Failed to allocate new badge for client.\n");
        return;
   }
    memset((void *)client_reg_ptr, 0, sizeof(ads_component_registry_entry_t));
    ads_component_registry_insert(client_reg_ptr);

    // We never created a new vpsace...
    int error = ads_new(get_ads_component()->server_vspace,
                          get_ads_component()->server_vka,
                          &client_reg_ptr->ads);
    if (error)
    {
        OSDB_PRINTF(ADSSERVS "main: Failed to create new ads object\n");
        return;
    }

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge
     */
    cspacepath_t src, dest;
    vka_cspace_make_path(get_ads_component()->server_vka,
                         get_ads_component()->server_ep_obj.cptr, &src);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_ads_component()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_ads_component()->server_vka, dest_cptr, &dest);

    seL4_Word badge = ads_assign_new_badge_and_objectID(client_reg_ptr);
    error = vka_cnode_mint(&dest,
                               &src,
                               seL4_AllRights,
                               badge);
    if (error)
    {
        OSDB_PRINTF(ADSSERVS "main: Failed to mint client badge %lx.\n", badge);
        return;
    }
    /* Return this badged end point in the return message. */
    seL4_SetCap(0, dest.capPtr);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, 1);
    OSDB_PRINTF(ADSSERVS "main: Successfully allocated a new ads %lx.\n", badge);
    return reply(tag);
}

static void handle_attach_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr mo_cap)
{
    OSDB_PRINTF(ADSSERVS "main: Got attach request from client badge %lx.\n",
           sender_badge);

    // No longer needed as we are now sending slots.
    // assert(seL4_MessageInfo_get_extraCaps(old_tag) == 1);
    int error;
    /* Find the client */
    ads_component_registry_entry_t *client_data = ads_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(ADSSERVS "main: Failed to find client badge %lx.\n",
               sender_badge);
        return;
    }
    OSDB_PRINTF(ADSSERVS "main: found client_data badge details:");
    badge_print(sender_badge);

    void *vaddr = (void *) seL4_GetMR(ADSMSGREG_ATTACH_REQ_VA);
    OSDB_PRINTF(ADSSERVS"main: vaddr %p \n", vaddr);


    /*
        The MO will be one of the caps Unwrapped.
        Get its badge using seL4_GetBadge(0) see handle_config_req
        where ads cap is passed.
        Get frame cap from the MO cap.
    */


   seL4_Word mo_badge = seL4_GetBadge(0);
   OSDB_PRINTF(ADSSERVS "Extra Caps: %s Badge: %lx\n",
   seL4_MessageInfo_get_extraCaps(old_tag) ? "true" : "false", mo_badge);
   mo_component_registry_entry_t *mo_reg = mo_component_registry_get_entry_by_badge(mo_badge);
   assert (mo_reg != NULL);


   uint32_t num_pages = mo_reg->mo.num_pages;
   seL4_CPtr *frame_caps = mo_reg->mo.frame_caps_in_root_task;
   void *ret_vaddr = NULL;

    error = ads_attach(&client_data->ads,
                       get_ads_component()->server_vka,
                       vaddr,
                       num_pages,
                       frame_caps,
                       &ret_vaddr,
                       client_data->ads.vspace);
    if (error) {
        OSDB_PRINTF(ADSSERVS "main: Failed to attach at vaddr:%p num_pages: %u to client badge %lx.\n",
                vaddr, num_pages, sender_badge);
        return;
    }


    // sel4utils_walk_vspace(client_data->ads.vspace, NULL);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_ATTACH_ACK);
    seL4_SetMR(ADSMSGREG_ATTACH_ACK_VA, (seL4_Word)ret_vaddr);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_ATTACH_ACK_END);
    return reply(tag);
}

static void handle_testing_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF(ADSSERVS "main: Got testing request from client badge %lx."
                         " extraCaps: %lu capsUnWrapped %lu\n",
                sender_badge, seL4_MessageInfo_get_extraCaps(old_tag),
                seL4_MessageInfo_get_capsUnwrapped(old_tag));

    for (int i = 0; i < 5; i++) {
        OSDB_PRINTF(ADSSERVS "MR[%d] = %lx\n", i, seL4_GetBadge(i));
    }

    // sel4utils_walk_vspace(client_data->ads.vspace, NULL);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_TESTING_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_TESTING_ACK_END);
    return reply(tag);
}

static void handle_get_rr_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF(ADSSERVS "main: Got get rr request from client badge %lx."
                         " extraCaps: %lu capsUnWrapped %lu\n",
                sender_badge, seL4_MessageInfo_get_extraCaps(old_tag),
                seL4_MessageInfo_get_capsUnwrapped(old_tag));

    // for (int i = 0; i < 5; i++) {
    //     OSDB_PRINTF(ADSSERVS "MR[%d] = %lx\n", i, seL4_GetBadge(i));
    // }
    ads_component_registry_entry_t *client_data = ads_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(ADSSERVS "main: Failed to find client badge %lx.\n",
               sender_badge);
        return;
    }
    OSDB_PRINTF(ADSSERVS "main: found client_data with objID %u.\n", client_data->ads.ads_obj_id);


    void *buffer_addr = (void *) seL4_GetMR(ADSMSGREG_GET_RR_REQ_BUF_VA);
    size_t buffer_size = seL4_GetMR(ADSMSGREG_GET_RR_REQ_BUF_SZ);
    ZF_LOGF("Do not use implemented");
    // ads_dump_rr(&client_data->ads, buffer_addr, buffer_size, false);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_GET_RR_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_TESTING_ACK_END);
    return reply(tag);
}

static void handle_shallow_copy_req(seL4_Word sender_badge)
{
    // Find the client - like attach
    OSDB_PRINTF(ADSSERVS "main: Got Shallow COpy  request from client badge %lx.\n",
           sender_badge);

    /* Find the client */
    ads_component_registry_entry_t *client_data = ads_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(ADSSERVS "main: Failed to find client badge %lx.\n",
               sender_badge);
        return;
    }
    OSDB_PRINTF(ADSSERVS "main: found client_data with objID %u.\n", client_data->ads.ads_obj_id);



    // Make a new endpoint for the client to send messages to. like connect.
    OSDB_PRINTF(ADSSERVS "main: Making a new cap for the shallow copy.\n");
    /* Allocate a new registry entry for the client. */
    ads_component_registry_entry_t *client_reg_ptr = malloc(sizeof(ads_component_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        OSDB_PRINTF(ADSSERVS "main: Failed to allocate new badge for client.\n");
        return;
   }
    memset((void *)client_reg_ptr, 0, sizeof(ads_component_registry_entry_t));


    // Do the actual shallow copy
    void *omit_vaddr = (void *) seL4_GetMR(ADSMSGREG_SHALLOW_COPY_REQ_OMIT_VA);
    ads_t *src_ads = &client_data->ads;
    ads_t *dst_ads = &client_reg_ptr->ads;
    int error = ads_shallow_copy(get_ads_component()->server_vspace,
                          src_ads,
                          get_ads_component()->server_vka,
                          omit_vaddr,
                          false,//true,
                          dst_ads);
    if (error) {
        OSDB_PRINTF(ADSSERVS "main: Failed to clone from client badge %lx.\n",
               sender_badge);
        return;
    }
    assert(client_reg_ptr->ads.vspace != NULL);
    ads_component_registry_insert(client_reg_ptr);
    OSDB_PRINTF(ADSSERVS "Shallow Copy done.\n");

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src_path, dest_path;
    vka_cspace_make_path(get_ads_component()->server_vka,
                         get_ads_component()->server_ep_obj.cptr, &src_path);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_ads_component()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_ads_component()->server_vka, dest_cptr, &dest_path);

    seL4_Word badge = ads_assign_new_badge_and_objectID(client_reg_ptr);
    error = vka_cnode_mint(&dest_path,
                               &src_path,
                               seL4_AllRights,
                               badge);
    if (error)
    {
        OSDB_PRINTF(ADSSERVS "Failed to mint client badge %lx.\n", badge);
        return;
    }
    OSDB_PRINTF(ADSSERVS "Clone assigned:");
    badge_print(badge);
    /* Return this badged end point in the return message. */
    seL4_SetCap(0, dest_path.capPtr);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_SHALLOW_COPY_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, ADSMSGREG_SHALLOW_COPY_ACK_END);
    return reply(tag);

}

/**
 * @brief The starting point for the ads server's thread.
 *
 */
void ads_component_handle(seL4_MessageInfo_t tag,
                          seL4_Word sender_badge,
                          cspacepath_t *received_cap,
                          seL4_MessageInfo_t *reply_tag)
{
    enum ads_component_funcs func;

    func = seL4_GetMR(ADSMSGREG_FUNC);
    /* Post */
    switch (func)
    {
    case ADS_FUNC_SHALLOW_COPY_REQ:
        handle_shallow_copy_req(sender_badge);
        break;

    case ADS_FUNC_ATTACH_REQ:
        handle_attach_req(sender_badge, tag, received_cap->capPtr);
        break;
    case ADS_FUNC_TESTING_REQ:
        handle_testing_req(sender_badge, tag);
        break;
    case ADS_FUNC_GET_RR_REQ:
        handle_get_rr_req(sender_badge, tag);
        break;
    default:
        gpi_panic(ADSSERVS"Unknown func type.", (seL4_Word) func);
        break;
    }
}

int forge_ads_cap_from_vspace(vspace_t *vspace, vka_t *vka, seL4_CPtr *cap_ret){

    assert(vspace != NULL);
    /* Allocate a new registry entry for the client. */
    ads_component_registry_entry_t *client_reg_ptr = malloc(sizeof(ads_component_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        OSDB_PRINTF(ADSSERVS "main: Failed to allocate new badge for client.\n");
        return 1;
    }
    memset((void *)client_reg_ptr, 0, sizeof(ads_component_registry_entry_t));

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src, dest;
    vka_cspace_make_path(get_ads_component()->server_vka,
                         get_ads_component()->server_ep_obj.cptr, &src);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_ads_component()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_ads_component()->server_vka, dest_cptr, &dest);

    /* Update the info in the registry entry. */
    seL4_Word badge = ads_assign_new_badge_and_objectID(client_reg_ptr);
    ads_component_registry_insert(client_reg_ptr);
    client_reg_ptr->ads.vspace = vspace;
    int error = vka_cnode_mint(&dest,
                               &src,
                               seL4_AllRights,
                               badge);
    if (error)
    {
        OSDB_PRINTF(ADSSERVS "main: Failed to mint client badge %lx.\n", badge);
        return 1;
    }
    OSDB_PRINTF(ADSSERVS "main: Forged a new ADS cap(EP: %lx) with badge value: %lx\n",
                dest.capPtr, badge);

    /* Iterate and print reservation_list*/
    sel4utils_res_t *res = get_alloc_data(vspace)->reservation_head;
    while (res != NULL) {
        OSDB_PRINTF(ADSSERVS "\tmain: Reservation: 0x%lx --> 0x%lx\n", res->start, res->end);
        /* print cap for each page in the reservation*/
        for (void *va = (void *)res->start; va <(void *) res->end; va += PAGE_SIZE_4K) {
            seL4_CPtr cap;
            cap = vspace_get_cap(vspace, va);
            // OSDB_PRINTF(ADSSERVS "\tmain: Cap for va: %p is %d TYPE: %d\n", va, cap, seL4_DebugCapIdentify(cap));
        }

        res = res->next;
    }

    *cap_ret = dest_cptr;
    return 0;
}