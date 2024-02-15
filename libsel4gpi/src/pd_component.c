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

uint64_t pd_assign_new_badge_and_objectID(pd_component_registry_entry_t *reg) {
    get_pd_component()->registry_n_entries++;
    // Add the latest ID to the obj and to the badlge.
    seL4_Word badge_val = gpi_new_badge(GPICAP_TYPE_PD,
                                        0x00,
                                        0x00, /* (XXX) This needs to be changed  to the PD*/
                                        get_pd_component()->registry_n_entries);

    assert(badge_val != 0);
    reg->pd.pd_obj_id = get_pd_component()->registry_n_entries;
    OSDB_PRINTF("pd_assign_new_badge_and_objectID: new badge: %lx\n", badge_val);
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

/**
 * @brief Lookup the client registry entry for the give PD ID.
 *
 * @param pd_id
 * @return pd_component_registry_entry_t*
 */
pd_component_registry_entry_t *pd_component_registry_get_entry_by_id(seL4_Word pd_id){

    /* Get the head of the list */
    pd_component_registry_entry_t *current_ctx = get_pd_component()->client_registry;

    while (current_ctx != NULL) {
        if ((seL4_Word)current_ctx->pd.pd_obj_id == pd_id) {
            break;
        }
        current_ctx = current_ctx->next;
    }
    return current_ctx;
}

int forge_pd_cap_from_init_data(
    test_init_data_t *init_data, // Change this to something else
    vka_t *vka,
    seL4_CPtr *cap_ret)
{

    assert(init_data != NULL);
    /* Allocate a new registry entry for the client. */
    pd_component_registry_entry_t *client_reg_ptr = malloc(sizeof(pd_component_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        OSDB_PRINTF(PDSERVS "main: Failed to allocate new badge for client.\n");
        return 1;
    }
    memset((void *)client_reg_ptr, 0, sizeof(pd_component_registry_entry_t));

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src, dest;
    vka_cspace_make_path(
        get_pd_component()->server_vka,
                         get_pd_component()->server_ep_obj.cptr, &src);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_pd_component()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_pd_component()->server_vka, dest_cptr, &dest);

    /* Update the info in the registry entry. */
    seL4_Word badge = pd_assign_new_badge_and_objectID(client_reg_ptr);
    pd_component_registry_insert(client_reg_ptr);


    // (XXX) A lot more will go here.
    // client_reg_ ptr->pd ...
    client_reg_ptr->pd.vka = vka;
    client_reg_ptr->pd.stack_pages = init_data->stack_pages;
    client_reg_ptr->pd.stack = init_data->stack;
    client_reg_ptr->pd.page_directory_in_pd = init_data->page_directory;
    client_reg_ptr->pd.root_cnode_in_pd = init_data->root_cnode;
    client_reg_ptr->pd.tcb_in_pd = init_data->tcb;
    client_reg_ptr->pd.domain_in_pd = init_data->domain;
    client_reg_ptr->pd.asid_pool_in_pd = init_data->asid_pool;
    client_reg_ptr->pd.asid_ctrl_in_pd = init_data->asid_ctrl;


    // Look at device frame caps and anyother relevant caps


#ifdef CONFIG_IOMMU
    client_reg_ptr-pd.io_space = init_data->io_space;
#endif /* CONFIG_IOMMU */

#ifdef CONFIG_TK1_SMMU
    client_reg_ptr->pd.io_space_caps = init_data->io_space_caps;
#endif

    client_reg_ptr->pd.cores = init_data->cores;
    /* copy the sched ctrl caps to the remote process */
    if (config_set(CONFIG_KERNEL_MCS)) {
        client_reg_ptr->pd.sched_ctrl_in_pd = init_data->sched_ctrl;
    }

    client_reg_ptr->pd.untypeds = init_data->untypeds;
    memcpy(
           client_reg_ptr->pd.untyped_size_bits_list,
        init_data->untyped_size_bits_list,
            sizeof(uint8_t) * CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS);


    // client_reg_ptr->pd.cspace_size_bits = init_data->cspace_size_bits;
    // client_reg_ptr->pd.free_slots = init_data->free_slots;
    // assert(client_reg_ptr->pd.free_slots.start < client_reg_ptr->pd.free_slots.end);

    int error = vka_cnode_mint(&dest,
                               &src,
                               seL4_AllRights,
                               badge);
    if (error)
    {
        OSDB_PRINTF(CPUSERVS "main: Failed to mint client badge %lx.\n", badge);
        return 1;
    }
    OSDB_PRINTF(CPUSERVS "main: Forged a new PD cap(EP: %lx) with badge value: %lx \n",
                dest.capPtr, badge);

    *cap_ret = dest_cptr;
    client_reg_ptr->raw_cap_in_root = dest_cptr;
    return 0;
}

void update_forged_pd_cap_from_init_data(test_init_data_t * init_data, seL4_CPtr cap)
{
    assert(init_data != NULL);

    pd_t *pd = &get_pd_component()->client_registry->pd;
    assert(pd != NULL);
    assert (pd->pd_obj_id == 0x1);
    pd->free_slots = init_data->free_slots;
    pd->cspace_size_bits = init_data->cspace_size_bits;

    assert(pd->free_slots.start < pd->free_slots.end);


}

void pd_handle_allocation_request(seL4_MessageInfo_t *reply_tag)
{
    OSDB_PRINTF(PDSERVS "main: Got connect request\n");

    /* Allocate a new registry entry for the client. */
    pd_component_registry_entry_t *client_reg_ptr =
        malloc(sizeof(pd_component_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        OSDB_PRINTF(PDSERVS "main: Failed to allocate new badge for client.\n");
        return;
    }
    memset((void *)client_reg_ptr, 0, sizeof(pd_component_registry_entry_t));
    pd_component_registry_insert(client_reg_ptr);

    // Allocate a new cspace
    // TODO

    int error = pd_new(&client_reg_ptr->pd,
                       get_pd_component()->server_vka,
                       get_pd_component()->server_vspace,
                       get_pd_component()->server_simple
                       );

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
        OSDB_PRINTF(PDSERVS "main: Failed to mint client badge %lx.\n", badge);
        return;
    }
    client_reg_ptr->raw_cap_in_root = dest_cptr;
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
    OSDB_PRINTF(PDSERVS "-----main: Got pd-load request\n");
    badge_print(sender_badge);
    assert(GPICAP_TYPE_PD == get_cap_type_from_badge(sender_badge));

    //OSDB_PRINTF(PDSERVS " received_cap: ");
    // debug_cap_identify("", received_cap);

    assert(seL4_MessageInfo_get_label(old_tag) == 0);

    int error = 0;


    assert(seL4_GetMR(PDMSGREG_LOAD_FUNC_IMAGE) == 1);
    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PDSERVS "main: Failed to find client badge %lx.\n",
               sender_badge);
        assert(0);
        return;
    }

    seL4_Word badge = seL4_GetBadge(0);

    ads_component_registry_entry_t *ads_data = ads_component_registry_get_entry_by_badge(seL4_GetBadge(0));
    assert(ads_data != NULL);

    seL4_CNode cspace_root = received_cap;
    error = pd_load_image(&client_data->pd,
                       get_pd_component()->server_vka,
                       get_pd_component()->server_simple,
                       "hello",
                       get_pd_component()->server_vspace,
                       ads_data->ads.vspace,
                       ads_data->ads.root_page_dir);
    if (error)
    {
        OSDB_PRINTF(PDSERVS "main: Failed to config from client badge:");
        badge_print(sender_badge);
        assert(0);
        return;
    }
    OSDB_PRINTF(PDSERVS "main: config done.\n");

    // Insert into has access to.
    client_data->pd.has_access_to[0].type = GPICAP_TYPE_ADS;
    client_data->pd.has_access_to[0].res_id = ads_data->ads.ads_obj_id;



    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_LOAD_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, PDMSGREG_LOAD_ACK_END);
    return reply(tag);
}

static void handle_next_slot_req(seL4_Word sender_badge,
                                seL4_MessageInfo_t old_tag,
                                seL4_CPtr received_cap) {

    OSDB_PRINTF(PDSERVS "Got next slot request from client badge %lx.\n",
           sender_badge);

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PDSERVS "Failed to find client badge %lx.\n",
               sender_badge);
        return;
    }
    seL4_Word slot;
    int error = pd_next_slot(&client_data->pd,
                             get_pd_component()->server_vka,
                             &slot);

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_NEXT_SLOT_ACK);
    seL4_SetMR(PDMSGREG_NEXT_SLOT_PD_SLOT, slot);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_NEXT_SLOT_ACK_END);
    return reply(tag);
}

static void handle_send_cap_req(seL4_Word sender_badge,
                                seL4_MessageInfo_t old_tag,
                                seL4_CPtr received_cap)
{
    OSDB_PRINTF(PDSERVS "main: Got send-cap request from client badge %lx.\n",
           sender_badge);


    /*
    Unwerapped works only if the badgted extra cap is the badged verion of the EPcap via which the
    client is sending the cap.

    */
    OSDB_PRINTF(PDSERVS " received_cap: %lu (badge: %lx)\n",
    received_cap, seL4_GetBadge(0));
    OSDB_PRINTF(PDSERVS " Unwrapped: %s\n",
                seL4_MessageInfo_get_capsUnwrapped(old_tag) ? "true" : "false");
    // debug_cap_identify("", received_cap);

    // assert(seL4_MessageInfo_get_extraCaps(old_tag) == 1);
    assert(seL4_MessageInfo_get_label(old_tag) == 0);

    // assert(seL4_MessageInfo_get_capsUnwrapped(old_tag) == 0);

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PDSERVS "main: Failed to find client badge %lx.\n",
               sender_badge);
        return;
    }

    seL4_Word received_caps_badge = 0;
    // if (seL4_MessageInfo_get_capsUnwrapped(old_tag) == 1) {
        received_caps_badge = seL4_GetBadge(0);
    // }

    seL4_Word slot;
    int error = pd_send_cap(&client_data->pd,
                            received_cap,
                            received_caps_badge,
                            &slot);

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SENDCAP_ACK);
    seL4_SetMR(PDMSGREG_SEND_CAP_PD_SLOT, slot);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_SEND_CAP_ACK_END);
    return reply(tag);
}

static void handle_dump_cap_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    OSDB_PRINTF(PDSERVS "main: Got dump-cap request from client badge %lx.\n",
           sender_badge);


    assert(seL4_MessageInfo_get_extraCaps(old_tag) == 0);
    assert(seL4_MessageInfo_get_label(old_tag) == 0);

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PDSERVS "main: Failed to find client badge %lx.\n",
               sender_badge);
        return;
    }

    // Extract buffer and VA
    // Find out which AS it belongs too.


    int error = pd_dump(&client_data->pd);

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_DUMP_ACK);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_DUMP_ACK_END);
    return reply(tag);
}

static void handle_start_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    OSDB_PRINTF(PDSERVS "main: Got start request from client badge %lx.\n",
           sender_badge);

    int error;
    seL4_Word arg0;
    arg0 = seL4_GetMR(PDMSGREG_START_ARG0);
    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PDSERVS "main: Failed to find client badge %lx.\n",
               sender_badge);
        return;
    }
    OSDB_PRINTF(PDSERVS "main: found client_data %p.\n", client_data);
    for (int i = 0; i < 5; i++)
    {
        OSDB_PRINTF(PDSERVS "MR[%d] = %lx\n", i, seL4_GetMR(i));
    }

    error = pd_start(&client_data->pd,
        get_pd_component()->server_vka,
                     client_data->raw_cap_in_root,
                     get_pd_component()->server_vspace,
                         arg0);
    if (error) {
        OSDB_PRINTF(PDSERVS "main: Failed to start PD.\n");
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
    uint64_t blah = seL4_GetMR(1);
    switch (func)
    {
    case PD_FUNC_LOAD_REQ:
        handle_load_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_NEXT_SLOT_REQ:
        handle_next_slot_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_SENDCAP_REQ:
        handle_send_cap_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_DUMP_REQ:
        handle_dump_cap_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_START_REQ:
        handle_start_req(sender_badge, tag, received_cap->capPtr);
        break;
    default:
        gpi_panic(PDSERVS "Unknown func type.", (seL4_Word)func);
        break;
    }
}