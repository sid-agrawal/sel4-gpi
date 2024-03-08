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

uint64_t pd_assign_new_badge_and_objectID(pd_component_registry_entry_t *reg)
{
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
static void pd_component_registry_insert(pd_component_registry_entry_t *new_node)
{
    // TODO:Use a mutex

    pd_component_registry_entry_t *head = get_pd_component()->client_registry;

    if (head == NULL)
    {
        get_pd_component()->client_registry = new_node;
        new_node->next = NULL;
        return;
    }

    while (head->next != NULL)
    {
        head = head->next;
    }
    head->next = new_node;
    new_node->next = NULL;
}

/**
 * @brief Lookup the client registry entry for the given object id.
 *
 * @param object_id
 * @return pd_component_registry_entry_t*
 */
static pd_component_registry_entry_t *pd_component_registry_get_entry_by_id(seL4_Word object_id)
{
    /* Get the head of the list */
    pd_component_registry_entry_t *current_ctx = get_pd_component()->client_registry;

    while (current_ctx != NULL)
    {
        if ((seL4_Word)current_ctx->pd.pd_obj_id == object_id)
        {
            break;
        }
        current_ctx = current_ctx->next;
    }
    return current_ctx;
}

/**
 * @brief Lookup the client registry entry for the given badge.
 *
 * @param badge
 * @return pd_component_registry_entry_t*
 */
static pd_component_registry_entry_t *pd_component_registry_get_entry_by_badge(seL4_Word badge)
{

    uint64_t objectID = get_object_id_from_badge(badge);
    return pd_component_registry_get_entry_by_id(objectID);
}

/**
 * @brief Insert a new resource server into the resourcer server registry Linked List.
 *
 * @param new_node
 */
static void pd_component_server_registry_insert(pd_component_resource_server_entry_t *new_node)
{
    // TODO:Use a mutex

    pd_component_resource_server_entry_t *head = get_pd_component()->server_registry;

    if (head == NULL)
    {
        get_pd_component()->server_registry = new_node;
        new_node->next = NULL;
        return;
    }

    while (head->next != NULL)
    {
        head = head->next;
    }
    head->next = new_node;
    new_node->next = NULL;
}

/**
 * @brief Lookup the resource server registry entry for the given object id.
 *
 * @param server_id
 * @return pd_component_resource_server_entry_t*
 */
pd_component_resource_server_entry_t *pd_component_server_registry_get_entry_by_id(seL4_Word server_id)
{
    /* Get the head of the list */
    pd_component_resource_server_entry_t *current_ctx = get_pd_component()->server_registry;

    while (current_ctx != NULL)
    {
        if ((seL4_Word)current_ctx->pd_id == server_id)
        {
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
    client_reg_ptr - pd.io_space = init_data->io_space;
#endif /* CONFIG_IOMMU */

#ifdef CONFIG_TK1_SMMU
    client_reg_ptr->pd.io_space_caps = init_data->io_space_caps;
#endif

    client_reg_ptr->pd.cores = init_data->cores;
    /* copy the sched ctrl caps to the remote process */
    if (config_set(CONFIG_KERNEL_MCS))
    {
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

void update_forged_pd_cap_from_init_data(test_init_data_t *init_data, seL4_CPtr cap)
{
    assert(init_data != NULL);

    pd_t *pd = &get_pd_component()->client_registry->pd;
    assert(pd != NULL);
    assert(pd->pd_obj_id == 0x1);
    pd->free_slots = init_data->free_slots;
    pd->cspace_size_bits = init_data->cspace_size_bits;

    assert(pd->free_slots.start < pd->free_slots.end);
}

osmosis_pd_cap_t *pd_add_resource_by_id(uint32_t client_id, gpi_cap_t cap_type, uint32_t res_id)
{
    if (client_id != 0) // only test processes would have no client ID
    {
        pd_component_registry_entry_t *client_pd_data = pd_component_registry_get_entry_by_id(client_id);
        ZF_LOGF_IF(client_pd_data == NULL, "Couldn't find PD client data");
        osmosis_pd_cap_t *res = pd_add_resource(&client_pd_data->pd, cap_type, res_id);
        return res;
    }
    return NULL;
}

void pd_handle_allocation_request(seL4_Word sender_badge, seL4_MessageInfo_t *reply_tag)
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
                       get_pd_component()->server_simple);

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
    uint32_t client_id = get_client_id_from_badge(sender_badge);
    osmosis_pd_cap_t *res = pd_add_resource_by_id(client_id, GPICAP_TYPE_PD, get_object_id_from_badge(badge));
    if (res)
    {
        res->slot_in_RT_Debug = dest_cptr;
        badge = set_client_id_to_badge(badge, client_id);
    }

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

    // OSDB_PRINTF(PDSERVS " received_cap: ");
    //  debug_cap_identify("", received_cap);

    assert(seL4_MessageInfo_get_label(old_tag) == 0);

    int error = 0;

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

    ads_component_registry_entry_t *ads_data = ads_component_registry_get_entry_by_badge(badge);
    assert(ads_data != NULL);

    const char *image_path = pd_images[seL4_GetMR(PDMSGREG_LOAD_FUNC_IMAGE)];

    seL4_CNode cspace_root = received_cap;
    error = pd_load_image(&client_data->pd,
                          get_pd_component()->server_vka,
                          get_pd_component()->server_simple,
                          image_path,
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

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_LOAD_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, PDMSGREG_LOAD_ACK_END);
    return reply(tag);
}

static void handle_next_slot_req(seL4_Word sender_badge,
                                 seL4_MessageInfo_t old_tag,
                                 seL4_CPtr received_cap)
{

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
                             &slot);

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_NEXT_SLOT_ACK);
    seL4_SetMR(PDMSGREG_NEXT_SLOT_PD_SLOT, slot);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_NEXT_SLOT_ACK_END);
    return reply(tag);
}

static void handle_free_slot_req(seL4_Word sender_badge,
                                 seL4_MessageInfo_t old_tag,
                                 seL4_CPtr received_cap)
{

    OSDB_PRINTF(PDSERVS "Got free slot request from client badge %lx.\n",
                sender_badge);

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PDSERVS "Failed to find client badge %lx.\n",
                    sender_badge);
        return;
    }
    seL4_Word slot = seL4_GetMR(PDMSGREG_FREE_SLOT_REQ_SLOT);
    OSDB_PRINTF(PDSERVS "Freeing PD's slot %d.\n", (int)slot);

    int error = pd_free_slot(&client_data->pd,
                             slot);

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_FREE_SLOT_ACK);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_FREE_SLOT_ACK_END);
    return reply(tag);
}

static void handle_alloc_ep_req(seL4_Word sender_badge,
                                seL4_MessageInfo_t old_tag,
                                seL4_CPtr received_cap)
{

    OSDB_PRINTF(PDSERVS "Got alloc ep request from client badge %lx.\n",
                sender_badge);

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PDSERVS "Failed to find client badge %lx.\n",
                    sender_badge);
        return;
    }
    seL4_CPtr slot;
    int error = pd_alloc_ep(&client_data->pd,
                            get_pd_component()->server_vka,
                            &slot);

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_ALLOC_EP_ACK);
    seL4_SetMR(PDMSGREG_ALLOC_EP_PD_SLOT, slot);
    OSDB_PRINTF(PDSERVS "Allocated ep in slot %d\n", (int)slot);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_ALLOC_EP_ACK_END);
    return reply(tag);
}

static void handle_badge_ep_req(seL4_Word sender_badge,
                                seL4_MessageInfo_t old_tag,
                                seL4_CPtr received_cap)
{

    OSDB_PRINTF(PDSERVS "Got badge ep request from client badge %lx.\n",
                sender_badge);

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PDSERVS "Failed to find client badge %lx.\n",
                    sender_badge);
        return;
    }

    seL4_Word badge = seL4_GetMR(PDMSGREG_BADGE_EP_REQ_BADGE);
    seL4_CPtr src_ep_slot = seL4_GetMR(PDMSGREG_BADGE_EP_REQ_SRC);
    seL4_Word slot;

    int error = pd_badge_ep(&client_data->pd,
                            src_ep_slot,
                            badge,
                            &slot);

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_BADGE_EP_ACK);
    seL4_SetMR(PDMSGREG_BADGE_EP_PD_SLOT, slot);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_BADGE_EP_ACK_END);
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

    if (error)
    {
        OSDB_PRINTF(PDSERVS "main: Failed to start PD.\n");
        return;
    }

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_START_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, PDMSGREG_START_ACK_END);
    return reply(tag);
}

static void handle_add_rde_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    OSDB_PRINTF(PDSERVS "main: Got add rde request from client badge %lx.\n",
                sender_badge);

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PDSERVS "main: Failed to find client badge %lx.\n",
                    sender_badge);
        return;
    }

    int error = 0;
    bool entry_needs_badge = seL4_GetMR(PDMSGREG_ADD_RDE_REQ_NEEDS_BADGE);

    if (client_data->pd.pd_started)
    {
        OSDB_PRINTF(PDSERVS "main: cannot add new RDEs after PD has been started\n");
        error = 1;
    }
    else
    {
        seL4_Word server_pd_badge = seL4_GetBadge(1);
        OSDB_PRINTF(PDSERVS "main: RDE server's badge %lx\n", server_pd_badge);
        pd_component_registry_entry_t *server_pd_data = pd_component_registry_get_entry_by_badge(server_pd_badge);
        if (server_pd_data == NULL)
        {
            OSDB_PRINTF(PDSERVS "error: cannot find server RDE's pd data\n");
            error = 1;
        }
        else
        {
            gpi_cap_t server_type = (gpi_cap_t)seL4_GetMR(PDMSGREG_ADD_RDE_REQ_TYPE);
            rde_type_t rde_type = {.type = server_type};
            error = pd_add_rde(&client_data->pd, rde_type, server_pd_data->pd.pd_obj_id,
                               received_cap, entry_needs_badge);
        }
    }

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_ADD_RDE_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_ADD_RDE_ACK_END);
    return reply(tag);
}

static void handle_register_server_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    int error = 0;

    OSDB_PRINTF(PDSERVS "Got register server request from client badge %lx.\n",
                sender_badge);

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PDSERVS "handle_register_server_req: Failed to find client badge %lx.\n",
                    sender_badge);
        error = -1;
    }
    else if (seL4_MessageInfo_get_extraCaps(old_tag) < 1)
    {
        OSDB_PRINTF(PDSERVS "handle_register_server_req missing server cap\n");
        error = -1;
    }
    else
    {
        // (XXX) Arya: This does not check if the server registers itself twice
        pd_component_resource_server_entry_t *rs_entry = malloc(sizeof(pd_component_resource_server_entry_t));
        rs_entry->pd_id = client_data->pd.pd_obj_id;
        // (XXX) Arya: this is ok for now because the received cap is never freed
        rs_entry->server_ep = received_cap;
        rs_entry->pd = &client_data->pd;

        pd_component_server_registry_insert(rs_entry);
        OSDB_PRINTF(PDSERVS "Registered server, cap is at %ld.\n", rs_entry->server_ep);

        // (XXX) Arya: Is there any danger in a PD being able to find its own ID this way?
        seL4_SetMR(PDMSGREG_REGISTER_SERV_ACK_ID, client_data->pd.pd_obj_id);
    }

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_REGISTER_SERV_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_REGISTER_SERV_ACK_END);
    return reply(tag);
}

static void handle_give_resource_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    int error = 0;

    OSDB_PRINTF(PDSERVS "Got give resource request from client badge %lx.\n",
                sender_badge);

    seL4_Word recipient_id = seL4_GetMR(PDMSGREG_GIVE_RES_REQ_CLIENT_ID);
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    pd_component_registry_entry_t *recipient_data = pd_component_registry_get_entry_by_id(recipient_id);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PDSERVS "handle_give_resource_req: Failed to find client badge 0x%lx.\n",
                    sender_badge);
        error = -1;
    }
    else if (recipient_data == NULL)
    {
        OSDB_PRINTF(PDSERVS "handle_give_resource_req: Failed to find recipient id 0x%lx.\n",
                    recipient_id);
        error = -1;
    }
    else
    {
        gpi_cap_t resource_type = (gpi_cap_t)seL4_GetMR(PDMSGREG_GIVE_RES_REQ_TYPE);
        seL4_Word resource_id = seL4_GetMR(PDMSGREG_GIVE_RES_REQ_RES_ID);

        OSDB_PRINTF(PDSERVS "server 0x%lx gives resource ID 0x%lx to client 0x%lx\n",
                    client_data->pd.pd_obj_id, resource_id, recipient_id);

        osmosis_pd_cap_t *out;
        HASH_FIND_INT(recipient_data->pd.has_access_to, &resource_id, out);
        if (out == NULL)
        {
            // Resource is not already in the hash
            pd_add_resource(&recipient_data->pd, resource_type, resource_id);
        }
    }

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_GIVE_RES_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_GIVE_RES_ACK_END);
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
    case PD_FUNC_FREE_SLOT_REQ:
        handle_free_slot_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_ALLOC_EP_REQ:
        handle_alloc_ep_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_BADGE_EP_REQ:
        handle_badge_ep_req(sender_badge, tag, received_cap->capPtr);
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
    case PD_FUNC_ADD_RDE_REQ:
        handle_add_rde_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_REGISTER_SERV_REQ:
        handle_register_server_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_GIVE_RES_REQ:
        handle_give_resource_req(sender_badge, tag, received_cap->capPtr);
        break;
    default:
        gpi_panic(PDSERVS "Unknown func type.", (seL4_Word)func);
        break;
    }
}