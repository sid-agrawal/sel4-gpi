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

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/badge_usage.h>

uint64_t pd_assign_new_badge_and_objectID(pd_component_registry_entry_t *reg) {
    get_pd_component()->registry_n_entries++;
    // Add the latest ID to the obj and to the badlge.
    seL4_Word badge_val = gpi_new_badge(GPICAP_TYPE_PD,
                                        0x00,
                                        0x00,
                                        get_pd_component()->registry_n_entries);

    assert(badge_val != 0);
    reg->pd.pd_obj_id = get_pd_component()->registry_n_entries;
    printf("pd_assign_new_badge_and_objectID: new badge: %lx\n", badge_val);
    return badge_val;
}

pd_component_context_t *get_pd_component(void)
{
    return &get_gpi_server()->pd_component;
}

static inline seL4_MessageInfo_t recv(seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(get_pd_component()->server_ep_obj.cptr,
                    sender_badge_ptr,
                    get_pd_component()->server_thread.reply.cptr);
}

static inline void reply(seL4_MessageInfo_t tag)
{
    api_reply(get_pd_component()->server_thread.reply.cptr, tag);
}


/**
 * @brief Insert a new client into the client registry Linked List.
 * 
 * @param new_node 
 */
static void pd_component_registry_insert(pd_component_registry_entry_t *new_node) {
        // TODO:Use a mutex


    pd_component_registry_entry_t *head = get_pd_component()->client_registry;

    if (head == NULL) {
        get_pd_component()->client_registry = new_node;
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
 * @brief Lookup the client registry entry for the give badge.
 * 
 * @param badge 
 * @return pd_component_registry_entry_t* 
 */
static pd_component_registry_entry_t *pd_component_registry_get_entry_by_badge(seL4_Word badge){

    uint64_t objectID = get_object_id_from_badge(badge);
    /* Get the head of the list */
    pd_component_registry_entry_t *current_ctx = get_pd_component()->client_registry;

    while (current_ctx != NULL) {
        if ((seL4_Word)current_ctx->pd.pd_obj_id == objectID) {
            break;
        }
        current_ctx = current_ctx->next;
    }
    return current_ctx;
}

void pd_handle_allocation_request(seL4_MessageInfo_t *reply_tag)
{
    printf(PDSERVS "main: Got connect request\n");

    /* Allocate a new registry entry for the client. */
    pd_component_registry_entry_t *client_reg_ptr =
        malloc(sizeof(pd_component_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        printf(PDSERVS "main: Failed to allocate new badge for client.\n");
        return;
    }
    memset((void *)client_reg_ptr, 0, sizeof(pd_component_registry_entry_t));
    pd_component_registry_insert(client_reg_ptr);

    // Allocate a new cspace
    // TODO

    int error = pd_new(&client_reg_ptr->pd,
                       get_pd_component()->server_vka);

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src, dest;
    vka_cspace_make_path(get_pd_component()->server_vka,
                         get_pd_component()->server_ep_obj.cptr, &src);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_pd_component()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_pd_component()->server_vka, dest_cptr, &dest);

    // Add the latest ID to the obj and to the badlge.
    seL4_Word badge = pd_assign_new_badge_and_objectID(client_reg_ptr);
    error = vka_cnode_mint(&dest,
                               &src,
                               seL4_AllRights,
                               badge);
    if (error)
    {
        printf(PDSERVS "main: Failed to mint client badge %lx.\n", badge);
        return;
    }
    /* Return this badged end point in the return message. */
    seL4_SetCap(0, dest.capPtr);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, 1);
    return reply(tag);
}


static void handle_load_req(seL4_Word sender_badge,
                              seL4_MessageInfo_t old_tag,
                              seL4_CPtr received_cap)
{
    // Find the client - like start
    printf(PDSERVS "-----main: Got pd-load request\n");
    badge_print(sender_badge);

    printf(PDSERVS " received_cap: ");
    debug_cap_identify("", received_cap);

    assert(seL4_MessageInfo_get_label(old_tag) == 0);

    int error = 0;

    printf(PDSERVS "capsUnwrapped: %d\n", seL4_MessageInfo_get_capsUnwrapped(old_tag));
    printf(PDSERVS "extraCap: %d\n", seL4_MessageInfo_ptr_get_extraCaps(&old_tag));
    for (int i = 0; i < 5; i++)
    {
        printf(PDSERVS "MR[%d] = %lx\n", i, seL4_GetBadge(i));
    }

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        printf(PDSERVS "main: Failed to find client badge %x.\n",
               sender_badge);
        assert(0);
        return;
    }


    seL4_CNode cspace_root = received_cap;
    error = pd_load_image(&client_data->pd,
                              get_pd_component()->server_vka,
                              NULL, // old vspace arg, delete
                              cspace_root);
    if (error)
    {
        printf(PDSERVS "main: Failed to config from client badge:");
        badge_print(sender_badge);
        assert(0);
        return;
    }
    printf(PDSERVS "main: config done.\n");

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_LOAD_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, PDMSGREG_LOAD_ACK_END);
    return reply(tag);
}

static void handle_start_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    printf(PDSERVS "main: Got start request from client badge %x.\n",
           sender_badge);

    int error;
    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        printf(PDSERVS "main: Failed to find client badge %x.\n",
               sender_badge);
        return;
    }
    printf(PDSERVS "main: found client_data %x.\n", client_data);
    for (int i = 0; i < 5; i++)
    {
        printf(PDSERVS "MR[%d] = %lx\n", i, seL4_GetMR(i));
    }

    error = pd_start(&client_data->pd,
    get_pd_component()->server_vka);
    if (error) {
        printf(PDSERVS "main: Failed to start PD.\n");
        return;
    }


    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_START_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, PDMSGREG_START_ACK_END);
    return reply(tag);
}
/**
 * @brief The starting point for the pd server's thread.
 *
 */
void pd_component_handle(seL4_MessageInfo_t tag,
                          seL4_Word sender_badge, 
                          cspacepath_t *received_cap,
                          seL4_MessageInfo_t *reply_tag) /* reply_tag not used right now*/
{
    enum pd_component_funcs func;
    seL4_Error error = 0;
    /* Post */
    func = seL4_GetMR(PDMSGREG_FUNC);
    switch (func)
    {
    case PD_FUNC_LOAD_REQ:
        handle_load_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_START_REQ:
        handle_start_req(sender_badge, tag, received_cap->capPtr);
        break;
    default:
        gpi_panic(ADSSERVS "Unknown cap type.");
        break;
    }
}