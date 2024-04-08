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
                                        NSID_DEFAULT,
                                        get_pd_component()->registry_n_entries);

    assert(badge_val != 0);
    reg->pd.pd_obj_id = get_pd_component()->registry_n_entries;
    OSDB_PRINTF(PD_DEBUG, "pd_assign_new_badge_and_objectID: new badge: %lx\n", badge_val);
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
pd_component_registry_entry_t *pd_component_registry_get_entry_by_id(seL4_Word object_id)
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
pd_component_registry_entry_t *pd_component_registry_get_entry_by_badge(seL4_Word badge)
{

    uint64_t objectID = get_object_id_from_badge(badge);
    return pd_component_registry_get_entry_by_id(objectID);
}

/**
 * @brief Insert a new resource manager into the resource manager registry Linked List.
 * Returns a new ID assigned to the resource manager
 *
 * @param new_node
 */
int pd_component_resource_manager_insert(pd_component_resource_manager_entry_t *new_node)
{
    // TODO:Use a mutex

    pd_component_resource_manager_entry_t *head = get_pd_component()->server_registry;

    if (head == NULL)
    {
        get_pd_component()->server_registry = new_node;
        new_node->next = NULL;
    }
    else
    {

        while (head->next != NULL)
        {
            head = head->next;
        }
        head->next = new_node;
        new_node->next = NULL;
    }

    new_node->manager_id = get_pd_component()->resource_manager_n_entries;
    get_pd_component()->resource_manager_n_entries++;
    return new_node->manager_id;
}

/**
 * @brief Lookup the resource server registry entry for the given object id.
 *
 * @param server_id
 * @return pd_component_resource_manager_entry_t*
 */
pd_component_resource_manager_entry_t *pd_component_resource_manager_get_entry_by_id(seL4_Word manager_id)
{
    /* Get the head of the list */
    pd_component_resource_manager_entry_t *current_ctx = get_pd_component()->server_registry;

    while (current_ctx != NULL)
    {
        if ((seL4_Word)current_ctx->manager_id == manager_id)
        {
            break;
        }
        current_ctx = current_ctx->next;
    }
    return current_ctx;
}

int forge_pd_cap_from_init_data(
    test_init_data_t *init_data, // Change this to something else
    vka_t *vka)
{
    assert(init_data != NULL);

    /* Allocate a new registry entry for the client. */
    pd_component_registry_entry_t *client_reg_ptr = malloc(sizeof(pd_component_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Failed to allocate new badge for client.\n");
        return 1;
    }
    memset((void *)client_reg_ptr, 0, sizeof(pd_component_registry_entry_t));

    pd_t *pd = &client_reg_ptr->pd;
    pd_new(pd,
           get_pd_component()->server_vka,
           get_pd_component()->server_vspace);

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    // (XXX) Arya: We might be able to replace this with the RDE in init data
    cspacepath_t src,
        dest;
    vka_cspace_make_path(
        get_pd_component()->server_vka,
        get_pd_component()->server_ep_obj.cptr, &src);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_pd_component()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_pd_component()->server_vka, dest_cptr, &dest);

    /* Update the info in the registry entry. */
    seL4_Word badge = pd_assign_new_badge_and_objectID(client_reg_ptr);
    set_client_id_to_badge(badge, pd->pd_obj_id);

    pd_component_registry_insert(client_reg_ptr);

    int error = vka_cnode_mint(&dest,
                               &src,
                               seL4_AllRights,
                               badge);
    if (error)
    {
        OSDB_PRINTF(PD_DEBUG, CPUSERVS "main: Failed to mint client badge %lx.\n", badge);
        return 1;
    }

    OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Forged a new PD cap(EP: %lx) with badge value: %lx \n",
                dest.capPtr, badge);

    client_reg_ptr->raw_cap_in_root = dest_cptr;
    return 0;
}

void update_forged_pd_cap_from_init_data(test_init_data_t *init_data, sel4utils_process_t *test_process)
{
    int error;
    assert(init_data != NULL);

    // Assumes this is called to set up the test process
    // (XXX) Arya: Would this fail if used for a second test?
    pd_component_registry_entry_t *reg_ptr = get_pd_component()->client_registry;
    pd_t *pd = &reg_ptr->pd;
    assert(pd != NULL);
    assert(pd->pd_obj_id == 0x1);
    pd->image_name = "TEST_PD";

    // Split the test process' cspace and initialize a vka with half
    seL4_CPtr mid_slot = DIV_ROUND_UP(init_data->free_slots.start + init_data->free_slots.end, 2);
    error = pd_bootstrap_allocator(pd, test_process->cspace.cptr,
                                   mid_slot, init_data->free_slots.end,
                                   init_data->cspace_size_bits,
                                   // seL4_WordBits - init_data->cspace_size_bits);
                                   0);
    ZF_LOGF_IFERR(error, "Failed to initialize PD VKA");
    init_data->free_slots.end = mid_slot - 1;

    // Forge ADS cap
    seL4_CPtr child_as_cap_in_parent;
    uint32_t ads_id;
    error = forge_ads_cap_from_vspace(&test_process->vspace, get_pd_component()->server_vka, pd->pd_obj_id, &child_as_cap_in_parent, &ads_id);
    ZF_LOGF_IFERR(error, "Failed to forge child's as cap");

    // Forge CPU cap
    seL4_CPtr child_cpu_cap_in_parent;
    uint32_t cpu_id;
    error = forge_cpu_cap_from_tcb(test_process, get_pd_component()->server_vka, pd->pd_obj_id, &child_cpu_cap_in_parent, &cpu_id);
    ZF_LOGF_IFERR(error, "Failed to forge child's CPU cap");

    // Setup the test process' init data
    error = copy_cap_to_pd(pd, child_as_cap_in_parent, &pd->init_data->ads_cap);
    assert(error == 0);
    pd_add_resource(pd, GPICAP_TYPE_ADS, ads_id, child_as_cap_in_parent, pd->init_data->ads_cap, child_as_cap_in_parent);

    // not using pd_send_cap bc this is already badged
    error = copy_cap_to_pd(pd, reg_ptr->raw_cap_in_root, &pd->init_data->pd_cap);
    assert(error == 0);
    pd_add_resource(pd, GPICAP_TYPE_PD, pd->pd_obj_id, reg_ptr->raw_cap_in_root, pd->init_data->pd_cap, reg_ptr->raw_cap_in_root);

    error = copy_cap_to_pd(pd, child_cpu_cap_in_parent, &pd->init_data->cpu_cap);
    assert(error == 0);
    pd_add_resource(pd, GPICAP_TYPE_CPU, cpu_id, child_cpu_cap_in_parent, pd->init_data->cpu_cap, child_cpu_cap_in_parent);

    rde_type_t ads_type = {.type = GPICAP_TYPE_ADS};
    pd_add_rde(pd, ads_type, get_gpi_server()->ads_manager_id, NSID_DEFAULT, get_gpi_server()->server_ep_obj.cptr);
    pd_add_rde(pd, ads_type, get_gpi_server()->ads_manager_id, ads_id, get_gpi_server()->server_ep_obj.cptr);
    pd->init_data->binded_ads_ns_id = ads_id;

    rde_type_t cpu_type = {.type = GPICAP_TYPE_CPU};
    pd_add_rde(pd, cpu_type, get_gpi_server()->cpu_manager_id, NSID_DEFAULT, get_gpi_server()->server_ep_obj.cptr);

    rde_type_t mo_type = {.type = GPICAP_TYPE_MO};
    pd_add_rde(pd, mo_type, get_gpi_server()->mo_manager_id, NSID_DEFAULT, get_gpi_server()->server_ep_obj.cptr);

    rde_type_t pd_type = {.type = GPICAP_TYPE_PD};
    pd_add_rde(pd, pd_type, get_gpi_server()->pd_manager_id, NSID_DEFAULT, get_gpi_server()->server_ep_obj.cptr);
}

void *get_osmosis_pd_init_data(vspace_t *test_vspace)
{
    // Assumes this is called to set up the test process
    // (XXX) Arya: Would this fail if used for a second test?
    pd_t *pd = &get_pd_component()->client_registry->pd;

    // Copy the init data frame cap
    cspacepath_t src, dest;
    vka_cspace_make_path(get_pd_component()->server_vka, pd->init_data_frame, &src);
    vka_cspace_alloc_path(get_pd_component()->server_vka, &dest);
    vka_cnode_copy(&dest, &src, seL4_AllRights);

    // Map the init data frame in test process
    // (XXX) Arya: It's possible that the test process vspace will not be
    // aware of this allocation. Ideally we replace this whole workaround evenutally
    void *init_data_vaddr = 0x50000000;

    reservation_t res = sel4utils_reserve_range_at(test_vspace,
                                                   init_data_vaddr,
                                                   1 * PAGE_SIZE_4K,
                                                   seL4_AllRights, 1);

    if (res.res == NULL)
    {
        ZF_LOGF("get_osmosis_pd_init_data failed to reserve range\n");
    }

    size_t size_bits = seL4_PageBits;
    int error = sel4utils_map_pages_at_vaddr(test_vspace,
                                             &dest.capPtr,
                                             NULL,
                                             init_data_vaddr,
                                             1,
                                             seL4_PageBits,
                                             res);

    if (error != seL4_NoError)
    {
        ZF_LOGF("get_osmosis_pd_init_data failed to map init data to test vspace\n");
    }

    pd->init_data_in_PD = init_data_vaddr;
    OSDB_PRINTF(PD_DEBUG, PDSERVS "Test process init data is at %p\n", pd->init_data_in_PD);

    return pd->init_data_in_PD;
}

osmosis_pd_cap_t *pd_add_resource_by_id(uint32_t client_id, gpi_cap_t cap_type, uint32_t res_id)
{
    if (client_id != 0) // only test processes would have no client ID
    {
        pd_component_registry_entry_t *client_pd_data = pd_component_registry_get_entry_by_id(client_id);
        ZF_LOGF_IF(client_pd_data == NULL, "Couldn't find PD client data");
        osmosis_pd_cap_t *res = pd_add_resource(&client_pd_data->pd, cap_type, res_id, seL4_CapNull, seL4_CapNull, seL4_CapNull);
        return res;
    }
    return NULL;
}

void pd_handle_allocation_request(seL4_Word sender_badge, seL4_MessageInfo_t *reply_tag)
{
    OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Got connect request from badge %lx\n", sender_badge);

    /* Allocate a new registry entry for the client. */
    pd_component_registry_entry_t *client_reg_ptr =
        malloc(sizeof(pd_component_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Failed to allocate new badge for client.\n");
        return;
    }
    memset((void *)client_reg_ptr, 0, sizeof(pd_component_registry_entry_t));
    pd_component_registry_insert(client_reg_ptr);

    // Allocate a new cspace
    // TODO

    int error = pd_new(&client_reg_ptr->pd,
                       get_pd_component()->server_vka,
                       get_pd_component()->server_vspace);

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
    // (XXX) Arya: replace with pd_send_cap?
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
        OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Failed to mint client badge %lx.\n", badge);
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
    OSDB_PRINTF(PD_DEBUG, PDSERVS "-----main: Got pd-load request\n");
    badge_print(sender_badge);
    assert(GPICAP_TYPE_PD == get_cap_type_from_badge(sender_badge));

    // OSDB_PRINTF(PD_DEBUG, PDSERVS " received_cap: ");
    //  debug_cap_identify("", received_cap);

    assert(seL4_MessageInfo_get_label(old_tag) == 0);

    int error = 0;

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Failed to find client badge %lx.\n",
                    sender_badge);
        assert(0);
        return;
    }

    seL4_Word badge = seL4_GetBadge(0);

    ads_component_registry_entry_t *ads_data = ads_component_registry_get_entry_by_badge(badge);
    assert(ads_data != NULL);

    badge = seL4_GetBadge(1);
    cpu_component_registry_entry_t *cpu_data = cpu_component_registry_get_entry_by_badge(badge);
    assert(cpu_data != NULL);

    int image_id = seL4_GetMR(PDMSGREG_LOAD_FUNC_IMAGE);
    const char *image_path = pd_images[image_id];
    uint64_t heap_size = pd_image_heap_size[image_id];

    seL4_CNode cspace_root = received_cap;
    error = pd_load_image(&client_data->pd,
                          get_pd_component()->server_vka,
                          get_pd_component()->server_simple,
                          image_path,
                          get_pd_component()->server_vspace,
                          &ads_data->ads,
                          &cpu_data->cpu,
                          heap_size);
    if (error)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Failed to config from client badge:");
        badge_print(sender_badge);
        assert(0);
        return;
    }
    OSDB_PRINTF(PD_DEBUG, PDSERVS "main: config done.\n");

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_LOAD_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, PDMSGREG_LOAD_ACK_END);
    return reply(tag);
}

static void handle_next_slot_req(seL4_Word sender_badge,
                                 seL4_MessageInfo_t old_tag,
                                 seL4_CPtr received_cap)
{

    OSDB_PRINTF(PD_DEBUG, PDSERVS "Got next slot request from client badge %lx.\n",
                sender_badge);

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "Failed to find client badge %lx.\n",
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

    OSDB_PRINTF(PD_DEBUG, PDSERVS "Got free slot request from client badge %lx, id %ld.\n",
                sender_badge, get_client_id_from_badge(sender_badge));

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "Failed to find client badge %lx.\n",
                    sender_badge);
        return;
    }
    seL4_Word slot = seL4_GetMR(PDMSGREG_FREE_SLOT_REQ_SLOT);
    OSDB_PRINTF(PD_DEBUG, PDSERVS "Freeing PD's slot %d.\n", (int)slot);

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

    OSDB_PRINTF(PD_DEBUG, PDSERVS "Got alloc ep request from client badge %lx.\n",
                sender_badge);

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "Failed to find client badge %lx.\n",
                    sender_badge);
        return;
    }
    seL4_CPtr slot;
    int error = pd_alloc_ep(&client_data->pd,
                            get_pd_component()->server_vka,
                            &slot);

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_ALLOC_EP_ACK);
    seL4_SetMR(PDMSGREG_ALLOC_EP_PD_SLOT, slot);
    OSDB_PRINTF(PD_DEBUG, PDSERVS "Allocated ep in slot %d\n", (int)slot);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_ALLOC_EP_ACK_END);
    return reply(tag);
}

static void handle_badge_ep_req(seL4_Word sender_badge,
                                seL4_MessageInfo_t old_tag,
                                seL4_CPtr received_cap)
{

    OSDB_PRINTF(PD_DEBUG, PDSERVS "Got badge ep request from client badge %lx.\n",
                sender_badge);

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "Failed to find client badge %lx.\n",
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
    OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Got send-cap request from client badge %lx.\n",
                sender_badge);

    /*
    Unwerapped works only if the badgted extra cap is the badged verion of the EPcap via which the
    client is sending the cap.

    */
    OSDB_PRINTF(PD_DEBUG, PDSERVS " received_cap: %lu (badge: %lx)\n",
                received_cap, seL4_GetBadge(0));
    OSDB_PRINTF(PD_DEBUG, PDSERVS " Unwrapped: %s\n",
                seL4_MessageInfo_get_capsUnwrapped(old_tag) ? "true" : "false");
    // debug_cap_identify("", received_cap);

    // assert(seL4_MessageInfo_get_extraCaps(old_tag) == 1);
    assert(seL4_MessageInfo_get_label(old_tag) == 0);

    // assert(seL4_MessageInfo_get_capsUnwrapped(old_tag) == 0);

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Failed to find client badge %lx.\n",
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
    OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Got dump-cap request from client badge %lx.\n",
                sender_badge);

    assert(seL4_MessageInfo_get_extraCaps(old_tag) == 0);
    assert(seL4_MessageInfo_get_label(old_tag) == 0);

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Failed to find client badge %lx.\n",
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
    OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Got start request from client badge %lx.\n",
                sender_badge);

    int error;

    /* parse the arguments */
    int argc = seL4_GetMR(PDMSGREG_START_ARGC);
    seL4_Word args[argc];

    for (int i = 0; i < argc; i++)
    {
        switch (i)
        {
        case 0:
            args[i] = seL4_GetMR(PDMSGREG_START_ARG0);
            break;
        case 1:
            args[i] = seL4_GetMR(PDMSGREG_START_ARG1);
            break;
        case 2:
            args[i] = seL4_GetMR(PDMSGREG_START_ARG2);
            break;
        case 3:
            args[i] = seL4_GetMR(PDMSGREG_START_ARG3);
            break;
        }
    }

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Failed to find client badge %lx.\n",
                    sender_badge);
        return;
    }
    OSDB_PRINTF(PD_DEBUG, PDSERVS "main: found client_data %p.\n", client_data);
    for (int i = 0; i < 5; i++)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "MR[%d] = %lx\n", i, seL4_GetMR(i));
    }

    error = pd_start(&client_data->pd,
                     get_pd_component()->server_vka,
                     client_data->raw_cap_in_root,
                     get_pd_component()->server_vspace,
                     argc,
                     args);

    if (error)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "main: Failed to start PD.\n");
        return;
    }

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_START_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, PDMSGREG_START_ACK_END);
    return reply(tag);
}

static void handle_add_rde_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    int error;

    OSDB_PRINTF(PD_DEBUG, PDSERVS "add_rde_req: Got request from client badge %lx.\n",
                sender_badge);

    seL4_Word server_badge = seL4_GetBadge(0);
    seL4_Word manager_id = seL4_GetMR(PDMSGREG_ADD_RDE_REQ_MANAGER_ID);
    seL4_Word ns_id = seL4_GetMR(PDMSGREG_ADD_RDE_REQ_NSID);
    pd_component_registry_entry_t *target_data = pd_component_registry_get_entry_by_badge(sender_badge);
    pd_component_registry_entry_t *server_data = pd_component_registry_get_entry_by_badge(server_badge);
    pd_component_resource_manager_entry_t *resource_manager_data = pd_component_resource_manager_get_entry_by_id(manager_id);

    if (target_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "add_rde_req: Failed to find target badge %lx.\n",
                    sender_badge);
        error = -1;
    }
    else if (server_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "add_rde_req: Failed to find server badge %lx.\n",
                    server_badge);
        error = -1;
    }
    else if (resource_manager_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "add_rde_req: Failed to find resource manager ID %ld.\n",
                    manager_id);
        error = -1;
    }
    else if (get_client_id_from_badge(sender_badge) != target_data->pd.pd_obj_id && target_data->pd.pd_started)
    {
        // (XXX) Arya: Allow a PD to update its own RDE mid-flight, but not another PD's
        OSDB_PRINTF(PD_DEBUG, PDSERVS "add_rde_req: cannot add new RDEs to another PD after it has been started\n");
        error = -1;
    }
    else if (server_data->pd.pd_obj_id != resource_manager_data->pd->pd_obj_id)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "add_rde_req: wrong server PD provided (%d) for resource manager in PD %d\n",
                    server_data->pd.pd_obj_id,
                    resource_manager_data->pd->pd_obj_id);
        error = -1;
    }
    else
    {
        rde_type_t rde_type = {.type = resource_manager_data->resource_type};
        error = pd_add_rde(&target_data->pd,
                           rde_type,
                           resource_manager_data->manager_id,
                           ns_id,
                           resource_manager_data->server_ep);
    }

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_ADD_RDE_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_ADD_RDE_ACK_END);
    return reply(tag);
}

static void handle_share_rde_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    int error;

    seL4_Word type = seL4_GetMR(PDMSGREG_SHARE_RDE_REQ_TYPE);
    seL4_Word ns_id = seL4_GetMR(PDMSGREG_SHARE_RDE_REQ_NS);

    OSDB_PRINTF(PD_DEBUG, PDSERVS "share_rde_req: Got request from client badge %lx for RDE type %d with NS %d.\n",
                sender_badge, type, ns_id);

    seL4_Word client_id = get_client_id_from_badge(sender_badge);
    pd_component_registry_entry_t *target_data = pd_component_registry_get_entry_by_badge(sender_badge);
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_id(client_id);

    if (target_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "share_rde_req: Failed to find target badge %lx.\n",
                    sender_badge);
        error = -1;
    }
    else if (client_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "share_rde_req: Failed to find client ID %d.\n",
                    client_id);
        error = -1;
    }

    osmosis_rde_t *rde = pd_rde_get(&client_data->pd, type, ns_id);
    if (rde == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "share_rde_req: Failed to find RDE for type %d and NS_ID %ld.\n",
                    type, ns_id);
        error = -1;
    }

    pd_component_resource_manager_entry_t *resource_manager_data = pd_component_resource_manager_get_entry_by_id(rde->manager_id);

    if (resource_manager_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "share_rde_req: Failed to find resource manager ID %ld.\n",
                    rde->manager_id);
        error = -1;
    }
    else if (target_data->pd.pd_started)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "share_rde_req: cannot add new RDEs after PD has been started\n");
        error = -1;
    }
    else
    {
        rde_type_t rde_type = {.type = type};
        error = pd_add_rde(&target_data->pd,
                           rde_type,
                           resource_manager_data->manager_id,
                           ns_id,
                           resource_manager_data->server_ep);
    }

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SHARE_RDE_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_SHARE_RDE_ACK_END);
    return reply(tag);
}

static void handle_register_resource_manager_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    int error = 0;

    OSDB_PRINTF(PD_DEBUG, PDSERVS "Got register server request from client badge %lx.\n",
                sender_badge);

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "register_resource_manager: Failed to find client badge %lx.\n",
                    sender_badge);
        error = -1;
    }
    else
    {
        assert(seL4_MessageInfo_get_extraCaps(old_tag) == 1);

        pd_component_resource_manager_entry_t *rs_entry = malloc(sizeof(pd_component_resource_manager_entry_t));
        rs_entry->pd = &client_data->pd;
        rs_entry->server_ep = received_cap;
        rs_entry->resource_type = seL4_GetMR(PDMSGREG_REGISTER_SERV_REQ_TYPE);
        rs_entry->ns_index = NSID_DEFAULT;

        int manager_id = pd_component_resource_manager_insert(rs_entry);
        OSDB_PRINTF(PD_DEBUG, PDSERVS "Registered server, cap is at %ld.\n", rs_entry->server_ep);

        seL4_SetMR(PDMSGREG_REGISTER_SERV_ACK_ID, manager_id);
    }

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_REGISTER_SERV_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_REGISTER_SERV_ACK_END);
    return reply(tag);
}

static void handle_register_namespace_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    int error = 0;

    OSDB_PRINTF(PD_DEBUG, PDSERVS "Got register namespace request from client badge %lx.\n",
                sender_badge);

    seL4_Word manager_id = seL4_GetMR(PDMSGREG_REGISTER_NS_REQ_MANAGER_ID);
    seL4_Word target_id = seL4_GetMR(PDMSGREG_REGISTER_NS_REQ_CLIENT_ID);

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    pd_component_registry_entry_t *target_data = pd_component_registry_get_entry_by_id(target_id);
    pd_component_resource_manager_entry_t *resource_manager_data = pd_component_resource_manager_get_entry_by_id(manager_id);

    if (client_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "handle_register_namespace_req: Failed to find client PD with ID %ld.\n",
                    get_client_id_from_badge(sender_badge));
        error = -1;
    }
    else if (target_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "handle_register_namespace_req: Failed to find taret PD with ID %ld.\n",
                    target_data);
        error = -1;
    }
    else if (resource_manager_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "handle_register_namespace_req: Failed to find resource manager with ID %ld.\n",
                    manager_id);
        error = -1;
    }
    else if (resource_manager_data->pd->pd_obj_id != get_client_id_from_badge(sender_badge))
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "handle_register_namespace_req: resource manager PD (%ld) and client PD (%ld) do not match.\n",
                    resource_manager_data->pd->pd_obj_id, get_client_id_from_badge(sender_badge));
        error = -1;
    }
    else
    {
        resource_manager_data->ns_index++;
        uint64_t ns_id = resource_manager_data->ns_index;

        // Add the RDE for the NS to the target PD
        rde_type_t rde_type = {.type = resource_manager_data->resource_type};
        pd_add_rde(&target_data->pd, rde_type, manager_id,
                   ns_id, resource_manager_data->server_ep);

        OSDB_PRINTF(PD_DEBUG, PDSERVS "Registered namespace, ID is %ld.\n", ns_id);
        seL4_SetMR(PDMSGREG_REGISTER_NS_ACK_NSID, ns_id);
    }

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_REGISTER_NS_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_REGISTER_NS_ACK_END);
    return reply(tag);
}

static void handle_create_resource_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    int error = 0;

    OSDB_PRINTF(PD_DEBUG, PDSERVS "Got create resource request from client badge %lx.\n",
                sender_badge);

    seL4_Word server_id = get_object_id_from_badge(sender_badge);
    seL4_Word manager_id = seL4_GetMR(PDMSGREG_CREATE_RES_REQ_MANAGER_ID);
    seL4_Word resource_id = get_global_object_id_from_local(manager_id, seL4_GetMR(PDMSGREG_CREATE_RES_REQ_RES_ID));

    pd_component_registry_entry_t *server_data = pd_component_registry_get_entry_by_id(server_id);
    pd_component_resource_manager_entry_t *resource_manager_data = pd_component_resource_manager_get_entry_by_id(manager_id);
    if (server_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "handle_create_resource_req: Failed to find resource server with ID %ld.\n",
                    server_id);
        error = -1;
    }
    else if (resource_manager_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "handle_create_resource_req: Failed to find resource manager with ID %ld.\n",
                    manager_id);
        error = -1;
    }
    else
    {
        gpi_cap_t resource_type = resource_manager_data->resource_type;

        OSDB_PRINTF(PD_DEBUG, PDSERVS "resource manager %ld creates resource with ID %ld\n",
                    manager_id, resource_id);

        osmosis_pd_cap_t *osm_cap;
        HASH_FIND_INT(server_data->pd.has_access_to, &resource_id, osm_cap);
        if (osm_cap == NULL)
        {
            // Resource is not already in the hash
            osm_cap = pd_add_resource(&server_data->pd, resource_type, resource_id, seL4_CapNull, seL4_CapNull, seL4_CapNull);
        }
        else
        {
            OSDB_PRINTF(PD_DEBUG, PDSERVS "handle_create_resource_req: Resource already exists %ld.\n",
                        resource_id);
        }
    }

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_CREATE_RES_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_CREATE_RES_ACK_END);
    return reply(tag);
}

static void handle_give_resource_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    int error = 0;

    OSDB_PRINTF(PD_DEBUG, PDSERVS "Got give resource request from client badge %lx, resource ID %ld.\n",
                sender_badge, seL4_GetMR(PDMSGREG_GIVE_RES_REQ_RES_ID));

    seL4_Word server_id = get_object_id_from_badge(sender_badge);
    seL4_Word recipient_id = seL4_GetMR(PDMSGREG_GIVE_RES_REQ_CLIENT_ID);
    seL4_Word manager_id = seL4_GetMR(PDMSGREG_GIVE_RES_REQ_MANAGER_ID);
    seL4_Word ns_id = seL4_GetMR(PDMSGREG_GIVE_RES_REQ_NS_ID);
    seL4_Word resource_id = get_global_object_id_from_local(manager_id, seL4_GetMR(PDMSGREG_GIVE_RES_REQ_RES_ID));
    pd_component_registry_entry_t *server_data = pd_component_registry_get_entry_by_id(server_id);
    pd_component_registry_entry_t *recipient_data = pd_component_registry_get_entry_by_id(recipient_id);
    pd_component_resource_manager_entry_t *resource_manager_data = pd_component_resource_manager_get_entry_by_id(manager_id);

    if (server_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "handle_give_resource_req: Failed to find resource server with ID %ld.\n",
                    server_id);
        error = -1;
    }
    else if (resource_manager_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "handle_give_resource_req: Failed to find resource manager with ID %ld.\n",
                    manager_id);
        error = -1;
    }
    else if (recipient_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "handle_give_resource_req: Failed to find recipient id %ld.\n",
                    recipient_id);
        error = -1;
    }

    osmosis_pd_cap_t *resource_data;
    HASH_FIND_INT(server_data->pd.has_access_to, &resource_id, resource_data);

    if (resource_data == NULL)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "handle_give_resource_req: Failed to find resource with id %ld.\n",
                    resource_id);
        error = -1;
    }
    else
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "resource manager %ld gives resource ID %ld to client %ld\n",
                    manager_id, resource_id, recipient_id);

        osmosis_pd_cap_t *osm_cap;
        HASH_FIND_INT(recipient_data->pd.has_access_to, &resource_id, osm_cap);
        if (osm_cap == NULL)
        {
            // Resource is not already in the recipient's hash table
            osm_cap = pd_add_resource(&recipient_data->pd, resource_manager_data->resource_type, resource_id, seL4_CapNull, seL4_CapNull, seL4_CapNull);
        }

        seL4_Word badge = gpi_new_badge(resource_manager_data->resource_type,
                                        0x00,
                                        recipient_id,
                                        ns_id,
                                        resource_id);

        // (XXX) Arya: How to handle duplicate entries to the same resource?
        // The hash table is keyed by resource ID
        // Badge the resource manager's endpoint to create a resource capability
        cspacepath_t src_path;
        seL4_CPtr dest;
        vka_cspace_make_path(get_pd_component()->server_vka, resource_manager_data->server_ep, &src_path);
        error = pd_mint(&recipient_data->pd, &src_path, badge, &dest);
        osm_cap->slot_in_PD_Debug = dest;
        seL4_SetMR(PDMSGREG_GIVE_RES_ACK_DEST, dest);
    }

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_GIVE_RES_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_GIVE_RES_ACK_END);
    return reply(tag);
}

static void handle_ipc_bench_req(void)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_BENCH_IPC_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
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
    case PD_FUNC_SHARE_RDE_REQ:
        handle_share_rde_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_REGISTER_SERV_REQ:
        handle_register_resource_manager_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_REGISTER_NS_REQ:
        handle_register_namespace_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_CREATE_RES_REQ:
        handle_create_resource_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_GIVE_RES_REQ:
        handle_give_resource_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_BENCH_IPC_REQ:
        handle_ipc_bench_req();
        break;
    default:
        gpi_panic(PDSERVS "Unknown func type.", (seL4_Word)func);
        break;
    }
}