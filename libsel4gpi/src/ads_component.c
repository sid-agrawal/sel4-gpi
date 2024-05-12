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
#include <sel4gpi/gpi_client.h>

static int forge_ads_attachments_from_vspace(ads_t *ads, uint32_t client_pd_id);

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

int ads_component_initialize(simple_t *server_simple,
                             vka_t *server_vka,
                             seL4_CPtr server_cspace,
                             vspace_t *server_vspace,
                             sel4utils_thread_t server_thread,
                             vka_object_t server_ep_obj)
{
    ads_component_context_t *component = get_ads_component();

    component->server_simple = server_simple;
    component->server_vka = server_vka;
    component->server_cspace = server_cspace;
    component->server_vspace = server_vspace;
    component->server_thread = server_thread;
    component->server_ep_obj = server_ep_obj;

    resource_server_initialize_registry(&component->ads_registry, NULL);
    // (XXX) Arya: Dirty hack to prevent overlap with ADS NS ID, will be fixed when I add resource spaces
    component->ads_registry.n_entries = NSID_DEFAULT;
}

/**
 * @brief Lookup the client registry entry for the given objectID in the badge.
 *
 * @param badge
 * @return ads_component_registry_entry_t*
 */
ads_component_registry_entry_t *ads_component_registry_get_entry_by_badge(seL4_Word badge)
{
    return (ads_component_registry_entry_t *)resource_server_registry_get_by_badge(&get_ads_component()->ads_registry, badge);
}

/**
 * @brief Lookup the client registry entry for the given objectID
 *
 * @param res_id
 * @return ads_component_registry_entry_t*
 */
ads_component_registry_entry_t *ads_component_registry_get_entry_by_id(seL4_Word object_id)
{
    return (ads_component_registry_entry_t *)resource_server_registry_get_by_id(&get_ads_component()->ads_registry, object_id);
}

// Utility function to create an ADS, add to registry, badge an endpoint, etc.
static int ads_component_allocate_ads(uint64_t client_id, bool forge, ads_component_registry_entry_t **ret_entry, seL4_CPtr *ret_cap)
{
    int error = 0;

    /* Create the registry entry */
    ads_component_registry_entry_t *client_reg_ptr = malloc(sizeof(ads_component_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to allocate new badge for client.\n");
        return 1;
    }
    memset((void *)client_reg_ptr, 0, sizeof(ads_component_registry_entry_t));

    client_reg_ptr->ads.ads_obj_id = resource_server_registry_insert_new_id(&get_ads_component()->ads_registry, (resource_server_registry_node_t *)client_reg_ptr);
    *ret_entry = client_reg_ptr;

    /* Create the ADS object */
    if (!forge)
    {
        error = ads_new(get_ads_component()->server_vspace,
                        get_ads_component()->server_vka,
                        &client_reg_ptr->ads);
        if (error)
        {
            OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to create new ads object\n");
            return 1;
        }
    }

    /* Create the badged endpoint */
    *ret_cap = resource_server_make_badged_ep(get_ads_component()->server_vka, get_ads_component()->server_ep_obj.cptr,
                                              (resource_server_registry_node_t *)client_reg_ptr, GPICAP_TYPE_ADS, NSID_DEFAULT, client_id);

    if (ret_cap == seL4_CapNull)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Failed to make badged ep for new ADS\n");
        return 1;
    }

    /* Add the resource to the client */
    osmosis_pd_cap_t *res = pd_add_resource_by_id(client_id, GPICAP_TYPE_ADS, client_reg_ptr->ads.ads_obj_id);
    if (res)
    {
        res->slot_in_RT_Debug = *ret_cap;
    }

    /* Add the RDE for the client */

    // (XXX) Linh: this is not very nice as we're coupling the PD and ADS components
    pd_component_registry_entry_t *client_pd_data = pd_component_registry_get_entry_by_id(client_id);
    ZF_LOGF_IF(client_pd_data == NULL, "Couldn't find PD client data");

    rde_type_t type = {.type = GPICAP_TYPE_ADS};
    error = pd_add_rde(&client_pd_data->pd, type, get_gpi_server()->ads_manager_id, client_reg_ptr->ads.ads_obj_id, get_ads_component()->server_ep_obj.cptr);
    if (error)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to add ADS to PD's RDE\n");
        return 1;
    }

    return 0;
}

static void handle_ads_allocation(seL4_Word sender_badge, seL4_MessageInfo_t *reply_tag)
{
    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Got ADS connect request from %lx\n", sender_badge);
    badge_print(sender_badge);

    int error = 0;
    seL4_CPtr ret_cap;
    ads_component_registry_entry_t *new_entry;
    uint32_t client_id = get_client_id_from_badge(sender_badge);

    error = ads_component_allocate_ads(client_id, false, &new_entry, &ret_cap);

    /* Return this badged end point in the return message. */
    seL4_SetCap(0, ret_cap);
    seL4_SetMR(ADSMSGREG_CONNECT_ACK_ADS_NS, new_entry->ads.ads_obj_id);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, ADSMSGREG_CONNECT_ACK_END);
    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Successfully allocated a new ads.\n");
    return reply(tag);
}

static void handle_reserve_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    int error = 0;

    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Got ADS reserve request from %lx\n", sender_badge);
    badge_print(sender_badge);

    uint32_t client_id = get_client_id_from_badge(sender_badge);
    void *vaddr = (void *)seL4_GetMR(ADSMSGREG_RESERVE_REQ_VA);
    size_t size = (size_t)seL4_GetMR(ADSMSGREG_RESERVE_REQ_SIZE);
    sel4utils_reservation_type_t vmr_type = (sel4utils_reservation_type_t)seL4_GetMR(ADSMSGREG_RESERVE_REQ_TYPE);
    void *ret_vaddr;
    uint32_t num_pages = DIV_ROUND_UP(size, SIZE_BITS_TO_BYTES(MO_PAGE_BITS));
    seL4_CPtr ret_cap;

    /* Find the ADS */
    uint64_t ads_id = get_ns_id_from_badge(sender_badge);
    ads_component_registry_entry_t *ads_entry = ads_component_registry_get_entry_by_id(ads_id);

    if (ads_entry == NULL)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to find ADS with ID %ld.\n",
                    ads_id);
        error = 1;
    }
    else
    {
        // Make the reservation
        attach_node_t *reservation;
        error = ads_reserve(&ads_entry->ads, vaddr, num_pages, MO_PAGE_BITS, vmr_type, &reservation);
        ret_vaddr = reservation->vaddr;

        // Make a cap for the reservation
        // The object ID is the shorter map entry ID, not the full vaddr of the reservation
        ret_cap = resource_server_make_badged_ep(get_ads_component()->server_vka, get_ads_component()->server_ep_obj.cptr,
                                                 (resource_server_registry_node_t *)reservation->map_entry, GPICAP_TYPE_VMR, ads_id, client_id);
    }

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, ADSMSGREG_RESERVE_ACK_END);
    seL4_SetMR(ADSMSGREG_RESERVE_ACK_VA, (seL4_Word)ret_vaddr);
    seL4_SetCap(0, ret_cap);

    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Successfully reserved an ads region at %lx.\n", ret_vaddr);

    return reply(tag);
}

int ads_component_attach(uint64_t ads_id, uint64_t mo_id, sel4utils_reservation_type_t vmr_type, void *vaddr, void **ret_vaddr)
{
    int error;

    /* Find the client */
    ads_component_registry_entry_t *client_data = ads_component_registry_get_entry_by_id(ads_id);
    if (client_data == NULL)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to find ADS with ID %ld.\n",
                    ads_id);
        return -1;
    }

    /* Find the MO */
    mo_component_registry_entry_t *mo_reg = mo_component_registry_get_entry_by_id(mo_id);
    if (mo_reg == NULL)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to find MO with ID %ld.\n",
                    mo_id);
        return -1;
    }

    /* Attach the MO */
    if (vmr_type < SEL4UTILS_RES_TYPE_ELF || vmr_type >= SEL4UTILS_RES_TYPE_MAX)
    {
        vmr_type = SEL4UTILS_RES_TYPE_OTHER;
    }
    error = ads_attach(&client_data->ads,
                       get_ads_component()->server_vka,
                       vaddr,
                       &mo_reg->mo,
                       ret_vaddr,
                       vmr_type);

    if (error)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to attach at vaddr:%p to client ID %ld.\n",
                    vaddr, ads_id);
        return -1;
    }

    return error;
}

static void handle_attach_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr mo_cap)
{
    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Got attach request from client badge %lx.\n", sender_badge);
    badge_print(sender_badge);

    int error;

    uint64_t ads_id = get_ns_id_from_badge(sender_badge);
    sel4utils_reservation_type_t vmr_type = (sel4utils_reservation_type_t)seL4_GetMR(ADSMSGREG_ATTACH_REQ_TYPE);
    void *vaddr = (void *)seL4_GetMR(ADSMSGREG_ATTACH_REQ_VA);

    /*
        The MO will be one of the caps Unwrapped.
        Get its badge using seL4_GetBadge(0) see handle_config_req
        where ads cap is passed.
        Get frame cap from the MO cap.
    */
    seL4_Word mo_badge = seL4_GetBadge(0);
    uint64_t mo_id = get_object_id_from_badge(mo_badge);
    if (get_cap_type_from_badge(mo_badge) != GPICAP_TYPE_MO)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Bad attach request, given MO EP is not an MO\n");
        badge_print(mo_badge);

        return;
    }

    error = ads_component_attach(ads_id, mo_id, vmr_type, vaddr, &vaddr);

    // sel4utils_walk_vspace(client_data->ads.vspace, NULL);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_ATTACH_ACK);
    seL4_SetMR(ADSMSGREG_ATTACH_ACK_VA, (seL4_Word)vaddr);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_ATTACH_ACK_END);
    return reply(tag);
}

static void handle_attach_to_reserve_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr mo_cap)
{
    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Got attach-to-reserve request from client badge %lx.\n", sender_badge);
    badge_print(sender_badge);

    int error = 0;

    size_t offset = seL4_GetMR(ADSMSGREG_ATTACH_RESERVE_REQ_OFFSET);
    uint64_t ads_id = get_ns_id_from_badge(sender_badge);
    uint64_t reservation_id = get_object_id_from_badge(sender_badge);

    /*
        The MO will be one of the caps Unwrapped.
        Get its badge using seL4_GetBadge(0) see handle_config_req
        where ads cap is passed.
        Get frame cap from the MO cap.
    */
    seL4_Word mo_badge = seL4_GetBadge(0);
    uint64_t mo_id = get_object_id_from_badge(mo_badge);
    if (get_cap_type_from_badge(mo_badge) != GPICAP_TYPE_MO)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Bad attach request, given MO EP is not an MO\n");
        badge_print(mo_badge);

        return;
    }

    /* Find the ADS, MO, and reservation */
    ads_component_registry_entry_t *client_data = ads_component_registry_get_entry_by_id(ads_id);
    mo_component_registry_entry_t *mo_reg = mo_component_registry_get_entry_by_id(mo_id);
    attach_node_t *reservation = ads_get_res_by_id(&client_data->ads, reservation_id);

    if (client_data == NULL)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to find ADS with ID %ld.\n",
                    ads_id);
        error = 1;
    }
    else if (mo_reg == NULL)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to find MO with ID %ld.\n",
                    mo_id);
        error = 1;
    }
    else if (reservation == NULL)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to find ADS reservation with ID %ld.\n",
                    reservation_id);
        error = 1;
    }
    else
    {
        error = ads_attach_to_res(&client_data->ads, get_ads_component()->server_vka, reservation, offset, &mo_reg->mo);
    }

    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_ATTACH_RESERVE_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, ADSMSGREG_ATTACH_RESERVE_ACK_END);
    return reply(tag);
}

static void handle_remove_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Got remove request from client badge %lx.\n", sender_badge);
    badge_print(sender_badge);

    int error = 0;

    uint64_t ads_id = get_ns_id_from_badge(sender_badge);
    void *vaddr = (void *)seL4_GetMR(ADSMSGREG_RM_REQ_VA);

    /* Find the client */
    ads_component_registry_entry_t *client_data = ads_component_registry_get_entry_by_id(ads_id);
    if (client_data == NULL)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to find ADS with ID %ld.\n",
                    ads_id);
        error = 1;
    }
    else
    {
        error = ads_rm(&client_data->ads, get_ads_component()->server_vka, vaddr);
    }

    if (error)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to remove pages from vaddr %p.\n",
                    vaddr);
    }

    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "removed pages from %p\n", vaddr);

    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_RM_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, ADSMSGREG_RM_ACK_END);
    return reply(tag);
}

static void handle_testing_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Got testing request from client badge %lx."
                                    " extraCaps: %lu capsUnWrapped %lu\n",
                sender_badge, seL4_MessageInfo_get_extraCaps(old_tag),
                seL4_MessageInfo_get_capsUnwrapped(old_tag));

    for (int i = 0; i < 5; i++)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "MR[%d] = %lx\n", i, seL4_GetBadge(i));
    }

    // sel4utils_walk_vspace(client_data->ads.vspace, NULL);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_TESTING_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_TESTING_ACK_END);
    return reply(tag);
}

static void handle_get_rr_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Got get rr request from client badge %lx."
                                    " extraCaps: %lu capsUnWrapped %lu\n",
                sender_badge, seL4_MessageInfo_get_extraCaps(old_tag),
                seL4_MessageInfo_get_capsUnwrapped(old_tag));

    // for (int i = 0; i < 5; i++) {
    //     OSDB_PRINTF(ADS_DEBUG, ADSSERVS "MR[%d] = %lx\n", i, seL4_GetBadge(i));
    // }
    ads_component_registry_entry_t *client_data = ads_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to find client badge %lx.\n",
                    sender_badge);
        return;
    }
    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: found client_data with objID %u.\n", client_data->ads.ads_obj_id);

    void *buffer_addr = (void *)seL4_GetMR(ADSMSGREG_GET_RR_REQ_BUF_VA);
    size_t buffer_size = seL4_GetMR(ADSMSGREG_GET_RR_REQ_BUF_SZ);
    ZF_LOGF("Do not use implemented");
    // ads_dump_rr(&client_data->ads, buffer_addr, buffer_size, false);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_GET_RR_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_TESTING_ACK_END);
    return reply(tag);
}

static void handle_shallow_copy_req(seL4_Word sender_badge)
{
    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Got Shallow copy request from client badge %lx.\n",
                sender_badge);

    int error;
    seL4_CPtr ret_cap;
    seL4_Word client_id = get_client_id_from_badge(sender_badge);

    /* Find the client */
    ads_component_registry_entry_t *old_ads_entry = ads_component_registry_get_entry_by_badge(sender_badge);
    pd_component_registry_entry_t *pd_data = pd_component_registry_get_entry_by_id(get_client_id_from_badge(sender_badge));

    if (old_ads_entry == NULL)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to find client badge %lx.\n",
                    sender_badge);
        error = 1;
        goto done;
    }
    else if (pd_data == NULL)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Couldn't find sender's PD data\n");
        error = 1;
        goto done;
    }

    /* Make a new ADS */
    ads_component_registry_entry_t *new_ads_entry;
    error = ads_component_allocate_ads(client_id, false, &new_ads_entry, &ret_cap);

    if (error)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to allocate a new ADS for shallow copy.\n");
        error = 1;
        goto done;
    }

    /* Copy memory regions */
    void *omit_vaddr = (void *)seL4_GetMR(ADSMSGREG_SHALLOW_COPY_REQ_OMIT_VA);
    ads_t *src_ads = &old_ads_entry->ads;
    ads_t *dst_ads = &new_ads_entry->ads;

    error = ads_shallow_copy(get_ads_component()->server_vspace,
                             src_ads,
                             get_ads_component()->server_vka,
                             omit_vaddr,
                             (void *)pd_data->pd.init_data_in_PD,
                             false, // true,
                             dst_ads);
    if (error)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to clone from client badge %lx.\n",
                    sender_badge);
        error = 1;
        goto done;
    }
    assert(new_ads_entry->ads.vspace != NULL);
    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Shallow Copy done.\n");

done:
    /* Return the new ADS */
    seL4_SetCap(0, ret_cap);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_SHALLOW_COPY_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, ADSMSGREG_SHALLOW_COPY_ACK_END);
    return reply(tag);
}

void ads_handle_allocation_request(seL4_MessageInfo_t tag, seL4_Word sender_badge, cspacepath_t *received_cap, seL4_MessageInfo_t *reply_tag)
{
    enum ads_component_funcs func = seL4_GetMR(ADSMSGREG_FUNC);

    if (get_ns_id_from_badge(sender_badge) == NSID_DEFAULT)
    {
        handle_ads_allocation(sender_badge, reply_tag);
    }
    else
    {
        switch (func)
        {
        case ADS_FUNC_ATTACH_REQ:
            handle_attach_req(sender_badge, tag, received_cap->capPtr);
            break;
        case ADS_FUNC_RM_REQ:
            // (XXX) Arya: This isn't an allocation request, but workaround due to
            // not having a VMR endpoint
            handle_remove_req(sender_badge, tag);
            break;
        case ADS_FUNC_RESERVE_REQ:
            handle_reserve_req(sender_badge, tag);
            break;
        default:
            gpi_panic(ADSSERVS "Unknown func type for ADS allocation request.", (seL4_Word)func);
            break;
        }
    }
}

void handle_load_elf_request(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    int error;
    assert(seL4_MessageInfo_get_capsUnwrapped(old_tag) == 1);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_LOAD_ELF_ACK_END);

    ads_component_registry_entry_t *target_ads = ads_component_registry_get_entry_by_badge(sender_badge);
    if (!target_ads)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Couldn't find associated ADS data for: \n");
        badge_print(sender_badge);
        tag = seL4_MessageInfo_set_label(tag, 1);
        return reply(tag);
    }

    pd_component_registry_entry_t *target_pd = pd_component_registry_get_entry_by_id(get_object_id_from_badge(seL4_GetBadge(0)));
    if (!target_pd)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Couldn't find associated PD data for: \n");
        badge_print(seL4_GetBadge(0));
        tag = seL4_MessageInfo_set_label(tag, 1);
        return reply(tag);
    }

    int image_id = (int)seL4_GetMR(ADSMSGREG_LOAD_ELF_REQ_IMAGE);
    if (image_id < 0 || image_id > PD_N_IMAGES)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Requesting load of bad image ID %d\n", image_id);
        tag = seL4_MessageInfo_set_label(tag, 1);
        return reply(tag);
    }

    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Loading %s's ELF into PD %ld\n", pd_images[image_id], target_pd->pd.pd_obj_id);
    void *entry_point;
    error = ads_load_elf(target_ads->ads.vspace, &target_pd->pd.proc, pd_images[image_id], &entry_point);
    if (error)
    {
        tag = seL4_MessageInfo_set_label(tag, 1);
    }

    // For now, we must fake the ADS attachments after loading elf
    error = forge_ads_attachments_from_vspace(&target_ads->ads, get_gpi_server()->rt_pd_id);

    if (error) {
        tag = seL4_MessageInfo_set_label(tag, 1);
        return reply(tag);
    }

    seL4_SetMR(ADSMSGREG_LOAD_ELF_ACK_ENTRY_PT, (seL4_Word)entry_point);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_LOAD_ELF_ACK);
    return reply(tag);
}

void handle_proc_setup_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    int error;
    assert(seL4_MessageInfo_get_capsUnwrapped(old_tag) == 1);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_PROC_SETUP_ACK_END);

    ads_component_registry_entry_t *target_ads = ads_component_registry_get_entry_by_badge(sender_badge);
    if (!target_ads)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Couldn't find associated ADS data for: \n");
        badge_print(sender_badge);
        tag = seL4_MessageInfo_set_label(tag, 1);
        return reply(tag);
    }

    pd_component_registry_entry_t *target_pd = pd_component_registry_get_entry_by_id(get_object_id_from_badge(seL4_GetBadge(0)));
    if (!target_pd)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Couldn't find associated PD data for: \n");
        badge_print(seL4_GetBadge(0));
        tag = seL4_MessageInfo_set_label(tag, 1);
        return reply(tag);
    }

    /* parse the arguments */
    int argc = seL4_GetMR(ADSMSGREG_PROC_SETUP_REQ_ARGC);
    seL4_Word args[argc];

    for (int i = 0; i < argc; i++)
    {
        switch (i)
        {
        case 0:
            args[i] = seL4_GetMR(ADSMSGREG_PROC_SETUP_REQ_ARG0);
            break;
        case 1:
            args[i] = seL4_GetMR(ADSMSGREG_PROC_SETUP_REQ_ARG1);
            break;
        case 2:
            args[i] = seL4_GetMR(ADSMSGREG_PROC_SETUP_REQ_ARG2);
            break;
        case 3:
            args[i] = seL4_GetMR(ADSMSGREG_PROC_SETUP_REQ_ARG3);
            break;
        }
    }

    char string_args[argc][WORD_STRING_SIZE];
    char *argv[argc];

    for (int i = 0; i < argc; i++)
    {
        argv[i] = string_args[i];
        snprintf(argv[i], WORD_STRING_SIZE, "%" PRIuPTR "", args[i]);
    }

    target_pd->pd.proc.thread.stack_top = (void *)seL4_GetMR(ADSMSGREG_PROC_SETUP_REQ_STACK);
    target_pd->pd.proc.thread.stack_size = seL4_GetMR(ADSMSGREG_PROC_SETUP_REQ_STACK_SZ);

    void *init_stack;
    error = ads_proc_setup(&target_pd->pd.proc,
                           (void *)target_pd->pd.init_data_in_PD,
                           get_gpi_server()->server_vka,
                           get_pd_component()->server_vspace,
                           argc,
                           argv,
                           &init_stack);

    if (error)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Couldn't write into process's stack\n");
        tag = seL4_MessageInfo_set_label(tag, 1);
        return reply(tag);
    }

    seL4_SetMR(ADSMSGREG_PROC_SETUP_ACK_INIT_STACK, (seL4_Word)init_stack);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_PROC_SETUP_ACK);
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
    case ADS_FUNC_TESTING_REQ:
        handle_testing_req(sender_badge, tag);
        break;
    case ADS_FUNC_GET_RR_REQ:
        handle_get_rr_req(sender_badge, tag);
        break;
    case ADS_FUNC_LOAD_ELF_REQ:
        handle_load_elf_request(sender_badge, tag);
        break;
    case ADS_FUNC_PROC_SETUP_REQ:
        handle_proc_setup_req(sender_badge, tag);
        break;
    case ADS_FUNC_ATTACH_RESERVE_REQ:
        handle_attach_to_reserve_req(sender_badge, tag, received_cap->capPtr);
        break;
    default:
        gpi_panic(ADSSERVS "Unknown func type.", (seL4_Word)func);
        break;
    }
}

int forge_ads_cap_from_vspace(vspace_t *vspace, vka_t *vka, uint32_t client_pd_id, seL4_CPtr *cap_ret, uint32_t *ads_obj_id_ret)
{
    int error = 0;
    seL4_CPtr ret_cap;
    ads_component_registry_entry_t *new_entry;

    error = ads_component_allocate_ads(client_pd_id, true, &new_entry, &ret_cap);

    if (error) {
        return error;
    }

    /* Update the ADS object with the vspace data */
    new_entry->ads.vspace = vspace;
    error = forge_ads_attachments_from_vspace(&new_entry->ads, client_pd_id);

    if (error) {
        return error;
    }

    if (ads_obj_id_ret)
    {
        *ads_obj_id_ret = new_entry->ads.ads_obj_id;
    }

    *cap_ret = ret_cap;
    return 0;
}

/**
 * Walks the ADS' vspace and creates ADS attach nodes for any vspace reservations that
 * do not have an attach node.
 * 
 * Eventually we should not need this at all.
*/
static int forge_ads_attachments_from_vspace(ads_t *ads, uint32_t client_pd_id)
{
    int error = 0;

    /* Walk every reservation and create MO / attach node*/
    sel4utils_alloc_data_t *child_data = get_alloc_data(ads->vspace);
    sel4utils_res_t *res = child_data->reservation_head;

    OSDB_PRINTF(ADS_DEBUG, "--- Begin forging ADS attach nodes from vspace --- \n");
    while (res != NULL)
    {
        OSDB_PRINTF(ADS_DEBUG, "Found reservation [%p,%p]\n", (void *)res->start, (void *)res->end);

        /* Get the caps in the reservation */
        uint32_t num_frames = (res->end - res->start) / PAGE_SIZE_4K;
        seL4_CPtr *frame_caps = malloc(sizeof(seL4_CPtr) * num_frames);
        assert(frame_caps != NULL);

        int i = 0;
        for (void *start = (void *)res->start;
             start < (void *)res->end;
             start += PAGE_SIZE_4K)
        {
            frame_caps[i] = vspace_get_cap(ads->vspace, start);
            i++;
        }

        /* This way, we can call forge_ads_cap_from_vspace again and again */
        // (XXX) Arya: is this check necessary?
        if (res->mo_ref == NULL)
        {
            OSDB_PRINTF(ADS_DEBUG, "Forging MO/attach for reservation [%p,%p]\n", (void *)res->start, (void *)res->end);

            // (XXX) Arya: This may have issues if the region is not mapped to physical pages everywhere
            seL4_CPtr cap_ret;
            error = forge_mo_cap_from_frames(frame_caps,
                                             num_frames,
                                             get_ads_component()->server_vka,
                                             client_pd_id,
                                             &cap_ret,
                                             (mo_t **)&res->mo_ref);

            if (error)
            {
                OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Failed to forge MO cap while forging ADS attach\n");
                return 1;
            }

            // Add the attach node for this region
            error = ads_forge_attach(ads, res, res->mo_ref);

            if (error)
            {
                OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Failed to forge ADS attach\n");
                return 1;
            }
        }

        res = res->next;
    }

    OSDB_PRINTF(ADS_DEBUG, "--- Finish forging ADS attach nodes from vspace --- \n");

    return 0;
}