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
#include <sel4gpi/error_handle.h>

// Defined for utility printing macros
#define DEBUG_ID ADS_DEBUG
#define SERVER_ID ADSSERVS

static int forge_ads_attachments_from_vspace(ads_t *ads, uint32_t client_pd_id);

ads_component_context_t *get_ads_component(void)
{
    return &get_gpi_server()->ads_component;
}

// Called when an item from the ADS registry is deleted
static void on_ads_registry_delete(resource_server_registry_node_t *node_gen)
{
    ads_component_registry_entry_t *node = (ads_component_registry_entry_t *)node_gen;

    OSDB_PRINTF("Destroying ADS (%d)\n", node->ads.id);

    ads_destroy(&node->ads);
}

static seL4_MessageInfo_t handle_ads_allocation(seL4_Word sender_badge)
{
    OSDB_PRINTF("Got ADS allocation request from %lx\n", sender_badge);

    int error = 0;
    seL4_CPtr ret_cap;
    ads_component_registry_entry_t *new_entry;
    uint32_t client_id = get_client_id_from_badge(sender_badge);

    error = resource_component_allocate(get_ads_component(), client_id, false, NULL,
                                        (resource_server_registry_node_t **)&new_entry, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new ADS\n");

    /* Add the RDE for the client */

    // (XXX) Linh: this is not very nice as we're coupling the PD and ADS components
    pd_component_registry_entry_t *client_pd_data = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), client_id);
    SERVER_GOTO_IF_COND(client_pd_data == NULL, "Couldn't find PD (%d)\n", client_id);

    rde_type_t type = {.type = GPICAP_TYPE_ADS};
    error = pd_add_rde(&client_pd_data->pd, type, get_gpi_server()->ads_manager_id, new_entry->ads.id, get_ads_component()->server_ep);
    SERVER_GOTO_IF_ERR(error, "Couldn't find add ADS to PD (%d)'s RDE\n", client_id);

    OSDB_PRINTF("Successfully allocated a new ads.\n");

    /* Return this badged end point in the return message. */
    seL4_SetCap(0, ret_cap);
    seL4_SetMR(ADSMSGREG_CONNECT_ACK_ADS_NS, new_entry->ads.id);

err_goto:
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, ADSMSGREG_CONNECT_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_reserve_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got ADS reserve request from %lx\n", sender_badge);
    int error = 0;

    uint32_t client_id = get_client_id_from_badge(sender_badge);
    void *vaddr = (void *)seL4_GetMR(ADSMSGREG_RESERVE_REQ_VA);
    size_t size = (size_t)seL4_GetMR(ADSMSGREG_RESERVE_REQ_SIZE);
    sel4utils_reservation_type_t vmr_type = (sel4utils_reservation_type_t)seL4_GetMR(ADSMSGREG_RESERVE_REQ_TYPE);
    uint32_t num_pages = DIV_ROUND_UP(size, SIZE_BITS_TO_BYTES(MO_PAGE_BITS));
    seL4_CPtr ret_cap;

    /* Find the ADS */
    uint64_t ads_id = get_ns_id_from_badge(sender_badge);
    ads_component_registry_entry_t *ads_entry = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), ads_id);
    SERVER_GOTO_IF_COND(ads_entry == NULL, "Couldn't find ADS (%ld)\n", ads_id);

    // Make the reservation
    attach_node_t *reservation;
    error = ads_reserve(&ads_entry->ads, vaddr, num_pages, MO_PAGE_BITS, vmr_type, &reservation);
    SERVER_GOTO_IF_ERR(error, "Failed to make reservation (%p)\n", vaddr);

    // Make a cap for the reservation
    // The object ID is the shorter map entry ID, not the full vaddr of the reservation
    ret_cap = resource_server_make_badged_ep(get_ads_component()->server_vka, NULL, get_ads_component()->server_ep,
                                             reservation->map_entry->gen.object_id, GPICAP_TYPE_VMR, ads_id, client_id);
    SERVER_GOTO_IF_COND(ret_cap == seL4_CapNull, "Failed to make badged ep for reservation\n");

    OSDB_PRINTF("Successfully reserved an ads region at %p.\n", reservation->vaddr);

    // Return the badged EP
    seL4_SetMR(ADSMSGREG_RESERVE_ACK_VA, (seL4_Word)reservation->vaddr);
    seL4_SetCap(0, ret_cap);

err_goto:
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_RESERVE_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, ADSMSGREG_RESERVE_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_attach_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr mo_cap)
{
    OSDB_PRINTF("Got attach request from client badge %lx.\n", sender_badge);

    int error = 0;

    uint64_t ads_id = get_ns_id_from_badge(sender_badge);
    sel4utils_reservation_type_t vmr_type = (sel4utils_reservation_type_t)seL4_GetMR(ADSMSGREG_ATTACH_REQ_TYPE);
    void *vaddr = (void *)seL4_GetMR(ADSMSGREG_ATTACH_REQ_VA);
    seL4_Word mo_badge = seL4_GetBadge(0);
    uint64_t mo_id = get_object_id_from_badge(mo_badge);

    SERVER_GOTO_IF_COND(get_cap_type_from_badge(mo_badge) != GPICAP_TYPE_MO, "Bad attach request, given MO EP is not an MO\n");

    error = ads_component_attach(ads_id, mo_id, vmr_type, vaddr, &vaddr);

err_goto:
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_ATTACH_ACK);
    seL4_SetMR(ADSMSGREG_ATTACH_ACK_VA, (seL4_Word)vaddr);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_ATTACH_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_attach_to_reserve_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr mo_cap)
{
    OSDB_PRINTF("Got attach-to-reserve request from client badge %lx.\n", sender_badge);

    int error = 0;

    size_t offset = seL4_GetMR(ADSMSGREG_ATTACH_RESERVE_REQ_OFFSET);
    uint64_t ads_id = get_ns_id_from_badge(sender_badge);
    uint64_t reservation_id = get_object_id_from_badge(sender_badge);
    seL4_Word mo_badge = seL4_GetBadge(0);
    uint64_t mo_id = get_object_id_from_badge(mo_badge);

    SERVER_GOTO_IF_COND(get_cap_type_from_badge(mo_badge) != GPICAP_TYPE_MO, "Bad attach request, given MO EP is not an MO\n");

    /* Find the ADS, MO, and reservation */
    ads_component_registry_entry_t *client_data = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), ads_id);
    mo_component_registry_entry_t *mo_reg = (mo_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_mo_component(), mo_id);
    attach_node_t *reservation = ads_get_res_by_id(&client_data->ads, reservation_id);

    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find ADS (%ld)\n", ads_id);
    SERVER_GOTO_IF_COND(mo_reg == NULL, "Couldn't find MO (%ld)\n", mo_id);
    SERVER_GOTO_IF_COND(reservation == NULL, "Couldn't find reservation (%ld)\n", reservation_id);

    error = ads_attach_to_res(&client_data->ads, get_ads_component()->server_vka, reservation, offset, &mo_reg->mo);

err_goto:
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_ATTACH_RESERVE_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, ADSMSGREG_ATTACH_RESERVE_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_remove_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got remove request from client badge %lx.\n", sender_badge);

    int error = 0;
    uint64_t ads_id = get_ns_id_from_badge(sender_badge);
    void *vaddr = (void *)seL4_GetMR(ADSMSGREG_RM_REQ_VA);

    /* Find the client */
    error = ads_component_rm_by_vaddr(ads_id, vaddr);

err_goto:
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_RM_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, ADSMSGREG_RM_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_testing_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got testing request from client badge %lx."
                " extraCaps: %lu capsUnWrapped %lu\n",
                sender_badge, seL4_MessageInfo_get_extraCaps(old_tag),
                seL4_MessageInfo_get_capsUnwrapped(old_tag));

    for (int i = 0; i < 5; i++)
    {
        OSDB_PRINTF("MR[%d] = %lx\n", i, seL4_GetBadge(i));
    }

    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_TESTING_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_TESTING_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_shallow_copy_req(seL4_Word sender_badge)
{
    OSDB_PRINTF("Got Shallow copy request from client badge %lx.\n", sender_badge);

    int error = 0;
    seL4_CPtr ret_cap;
    seL4_Word client_id = get_client_id_from_badge(sender_badge);

    /* Find the client */
    ads_component_registry_entry_t *old_ads_entry = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_ads_component(), sender_badge);
    pd_component_registry_entry_t *pd_data = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), client_id);

    SERVER_GOTO_IF_COND(old_ads_entry == NULL, "Couldn't find source ADS (%ld)\n", get_object_id_from_badge(sender_badge));
    SERVER_GOTO_IF_COND(pd_data == NULL, "Couldn't find PD (%ld)\n", client_id);

    /* Make a new ADS */
    ads_component_registry_entry_t *new_ads_entry;
    error = resource_component_allocate(get_ads_component(), client_id, false, NULL,
                                        (resource_server_registry_node_t **)&new_ads_entry, &ret_cap);

    SERVER_GOTO_IF_ERR(error, "Failed to allocate new ADs for copy\n");

    /* Copy memory regions */
    void *omit_vaddr = (void *)seL4_GetMR(ADSMSGREG_SHALLOW_COPY_REQ_OMIT_VA);
    ads_t *src_ads = &old_ads_entry->ads;
    ads_t *dst_ads = &new_ads_entry->ads;

    error = ads_shallow_copy(get_ads_component()->server_vspace,
                             get_ads_component()->server_vka,
                             src_ads,
                             dst_ads,
                             omit_vaddr,
                             (void *)pd_data->pd.init_data_in_PD,
                             false);

    if (error != 0)
    {
        // Cleanup the dst_ads
    }

err_goto:
    /* Return the new ADS */
    seL4_SetCap(0, ret_cap);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_SHALLOW_COPY_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, ADSMSGREG_SHALLOW_COPY_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_load_elf_request(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got load elf request from client badge %lx.\n", sender_badge);

    int error = 0;
    SERVER_GOTO_IF_COND(seL4_MessageInfo_get_capsUnwrapped(old_tag) < 1, "Missing cap for target PD in capsUnwrapped\n");

    ads_component_registry_entry_t *target_ads = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_ads_component(), sender_badge);
    pd_component_registry_entry_t *target_pd = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), get_object_id_from_badge(seL4_GetBadge(0)));
    SERVER_GOTO_IF_COND(target_ads == NULL, "Couldn't find target ADS (%ld)\n", get_object_id_from_badge(sender_badge));
    SERVER_GOTO_IF_COND(target_pd == NULL, "Couldn't find target PD (%ld)\n", get_object_id_from_badge(seL4_GetBadge(0)));

    int image_id = (int)seL4_GetMR(ADSMSGREG_LOAD_ELF_REQ_IMAGE);
    SERVER_GOTO_IF_COND(image_id < 0 || image_id > PD_N_IMAGES, "Requested elf load of bad image ID %d\n", image_id);

    OSDB_PRINTF("Loading %s's ELF into PD %d\n", pd_images[image_id], target_pd->pd.id);
    void *entry_point;
    error = ads_load_elf(target_ads->ads.vspace, &target_pd->pd.proc, pd_images[image_id], &entry_point);
    SERVER_GOTO_IF_ERR(error, "Load ELF failed\n");

    OSDB_PRINTF("Successfully loaded ELF, entry point %p.\n", entry_point);

    // For now, we must fake the ADS attachments after loading elf
    error = forge_ads_attachments_from_vspace(&target_ads->ads, get_gpi_server()->rt_pd_id);
    SERVER_GOTO_IF_ERR(error, "Failed to forge attachments to ADS after elf load\n");

    seL4_SetMR(ADSMSGREG_LOAD_ELF_ACK_ENTRY_PT, (seL4_Word)entry_point);

    OSDB_PRINTF("Forged ADS attachments from ELF.\n");

err_goto:
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_LOAD_ELF_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, ADSMSGREG_LOAD_ELF_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_pd_setup_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got proc setup request from client badge %lx.\n", sender_badge);

    int error = 0;

    SERVER_GOTO_IF_COND(seL4_MessageInfo_get_capsUnwrapped(old_tag) < 1, "Missing cap for target PD in capsUnwrapped\n");

    ads_component_registry_entry_t *target_ads = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_ads_component(), sender_badge);
    pd_component_registry_entry_t *target_pd = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), get_object_id_from_badge(seL4_GetBadge(0)));

    SERVER_GOTO_IF_COND(target_ads == NULL, "Couldn't find target ADS (%ld)\n", get_object_id_from_badge(sender_badge));
    SERVER_GOTO_IF_COND(target_pd == NULL, "Couldn't find target PD (%ld)\n", get_object_id_from_badge(seL4_GetBadge(0)));

    /* parse the arguments */
    int argc = seL4_GetMR(ADSMSGREG_PD_SETUP_REQ_ARGC);

    // These brackets limit the scope of argc/argv so we may goto err_goto
    {
        seL4_Word args[argc];

        for (int i = 0; i < argc; i++)
        {
            switch (i)
            {
            case 0:
                args[i] = seL4_GetMR(ADSMSGREG_PD_SETUP_REQ_ARG0);
                break;
            case 1:
                args[i] = seL4_GetMR(ADSMSGREG_PD_SETUP_REQ_ARG1);
                break;
            case 2:
                args[i] = seL4_GetMR(ADSMSGREG_PD_SETUP_REQ_ARG2);
                break;
            case 3:
                args[i] = seL4_GetMR(ADSMSGREG_PD_SETUP_REQ_ARG3);
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

        target_pd->pd.proc.thread.stack_top = (void *)seL4_GetMR(ADSMSGREG_PD_SETUP_REQ_STACK);
        target_pd->pd.proc.thread.stack_size = seL4_GetMR(ADSMSGREG_PD_SETUP_REQ_STACK_SZ);

        void *init_stack;
        error = ads_proc_setup(&target_pd->pd.proc,
                               (void *)target_pd->pd.init_data_in_PD,
                               get_gpi_server()->server_vka,
                               get_pd_component()->server_vspace,
                               argc,
                               argv,
                               &init_stack);

        seL4_SetMR(ADSMSGREG_PD_SETUP_ACK_INIT_STACK, (seL4_Word)init_stack);
    }

    SERVER_GOTO_IF_ERR(error, "Failed to setup process stack\n");

err_goto:
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, ADSMSGREG_PD_SETUP_ACK_END);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_PD_SETUP_ACK);
    return tag;
}

/**
 * Walks the ADS' vspace and creates ADS attach nodes for any vspace reservations that
 * do not have an attach node.
 *
 * Eventually we should not need this at all.
 */
static int forge_ads_attachments_from_vspace(ads_t *ads, uint32_t client_pd_id)
{
    OSDB_PRINTF("Forging ADS attachments from vspace\n");

    int error = 0;

    /* Walk every reservation and create MO / attach node*/
    sel4utils_alloc_data_t *child_data = get_alloc_data(ads->vspace);
    sel4utils_res_t *res = child_data->reservation_head;

    OSDB_PRINTF("--- Begin forging ADS attach nodes from vspace --- \n");
    while (res != NULL)
    {
        OSDB_PRINTF("Found reservation [%p,%p]\n", (void *)res->start, (void *)res->end);

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
            OSDB_PRINTF("Forging MO/attach for reservation [%p,%p]\n", (void *)res->start, (void *)res->end);

            // (XXX) Arya: This may have issues if the region is not mapped to physical pages everywhere
            seL4_CPtr cap_ret;
            error = forge_mo_cap_from_frames(frame_caps,
                                             num_frames,
                                             get_ads_component()->server_vka,
                                             client_pd_id,
                                             &cap_ret,
                                             (mo_t **)&res->mo_ref);
            SERVER_GOTO_IF_ERR(error, "Failed to forge MO cap while forging ADS attach\n");

            // Add the attach node for this region
            error = ads_forge_attach(ads, res, res->mo_ref);
            SERVER_GOTO_IF_ERR(error, "Failed to forge ADS attach\n");
        }

        res = res->next;
    }

    OSDB_PRINTF("--- Finish forging ADS attach nodes from vspace --- \n");

err_goto:
    return error;
}

/**
 * @brief The starting point for the ads server's thread.
 *
 */
static seL4_MessageInfo_t ads_component_handle(seL4_MessageInfo_t tag,
                                               seL4_Word sender_badge,
                                               seL4_CPtr received_cap,
                                               bool *need_new_recv_cap)
{
    enum ads_component_funcs func = seL4_GetMR(ADSMSGREG_FUNC);
    seL4_MessageInfo_t reply_tag;

    if (get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL && get_ns_id_from_badge(sender_badge) == NSID_DEFAULT)
    {
        reply_tag = handle_ads_allocation(sender_badge);
    }
    else
    {
        switch (func)
        {
        case ADS_FUNC_ATTACH_REQ:
            reply_tag = handle_attach_req(sender_badge, tag, received_cap);
            *need_new_recv_cap = true;
            break;
        case ADS_FUNC_RM_REQ:
            reply_tag = handle_remove_req(sender_badge, tag);
            break;
        case ADS_FUNC_RESERVE_REQ:
            reply_tag = handle_reserve_req(sender_badge, tag);
            break;
        case ADS_FUNC_SHALLOW_COPY_REQ:
            reply_tag = handle_shallow_copy_req(sender_badge);
            break;
        case ADS_FUNC_TESTING_REQ:
            reply_tag = handle_testing_req(sender_badge, tag);
            break;
        case ADS_FUNC_LOAD_ELF_REQ:
            reply_tag = handle_load_elf_request(sender_badge, tag);
            break;
        case ADS_FUNC_PD_SETUP_REQ:
            reply_tag = handle_pd_setup_req(sender_badge, tag);
            break;
        case ADS_FUNC_ATTACH_RESERVE_REQ:
            reply_tag = handle_attach_to_reserve_req(sender_badge, tag, received_cap);
            *need_new_recv_cap = true;
            break;
        default:
            gpi_panic(ADSSERVS "Unknown func type.", (seL4_Word)func);
            break;
        }
    }

    // To replace with real value
    return reply_tag;
}

int ads_component_initialize(simple_t *server_simple,
                             vka_t *server_vka,
                             seL4_CPtr server_cspace,
                             vspace_t *server_vspace,
                             sel4utils_thread_t server_thread,
                             vka_object_t server_ep_obj)
{
    resource_component_initialize(get_ads_component(),
                                  GPICAP_TYPE_ADS,
                                  ads_component_handle,
                                  (int (*)(resource_component_object_t *, vka_t *, vspace_t *, void *))ads_new,
                                  on_ads_registry_delete,
                                  sizeof(ads_component_registry_entry_t),
                                  server_simple,
                                  server_vka,
                                  server_cspace,
                                  server_vspace,
                                  server_thread,
                                  server_ep_obj.cptr);
}

/** --- Functions callable by root task --- **/

int forge_ads_cap_from_vspace(vspace_t *vspace, vka_t *vka, uint32_t client_pd_id, seL4_CPtr *cap_ret, uint32_t *id_ret)
{
    OSDB_PRINTF("Forging ADS cap from vspace\n");

    int error = 0;
    seL4_CPtr ret_cap;
    ads_component_registry_entry_t *new_entry;

    error = error = resource_component_allocate(get_ads_component(), client_pd_id, false, NULL,
                                                (resource_server_registry_node_t **)&new_entry, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate ADS for forging\n");

    /* Update the ADS object with the vspace data */
    new_entry->ads.vspace = vspace;
    resource_server_initialize_registry(&new_entry->ads.attach_registry, NULL);
    resource_server_initialize_registry(&new_entry->ads.attach_id_to_vaddr_map, NULL);

    error = forge_ads_attachments_from_vspace(&new_entry->ads, client_pd_id);
    SERVER_GOTO_IF_ERR(error, "Failed to forge ADS attachments from vspace\n");

    if (id_ret)
    {
        *id_ret = new_entry->ads.id;
    }

    *cap_ret = ret_cap;

err_goto:
    return error;
}

int ads_component_attach(uint64_t ads_id, uint64_t mo_id, sel4utils_reservation_type_t vmr_type, void *vaddr, void **ret_vaddr)
{
    int error = 0;

    /* Find the client */
    ads_component_registry_entry_t *client_data = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), ads_id);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find ADS (%ld)\n", ads_id);

    /* Find the MO */
    mo_component_registry_entry_t *mo_reg = (mo_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_mo_component(), mo_id);
    SERVER_GOTO_IF_COND(mo_reg == NULL, "Couldn't find MO (%ld)\n", mo_id);

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

    SERVER_GOTO_IF_ERR(error, "Failed to attach at vaddr (%p) to ADS (%ld).\n",
                       vaddr, ads_id);

err_goto:
    return error;
}

int ads_component_rm_by_id(uint64_t ads_id, uint32_t vmr_id)
{
    int error = 0;

    ads_component_registry_entry_t *client_data = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), ads_id);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find ADS (%ld)\n", ads_id);

    attach_node_t *attach_node = ads_get_res_by_id(&client_data->ads, vmr_id);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find VMR (%d)\n", vmr_id);
    error = ads_rm(&client_data->ads, get_ads_component()->server_vka, attach_node->vaddr);

err_goto:
    return error;
}

int ads_component_rm_by_vaddr(uint64_t ads_id, void *vaddr)
{
    int error = 0;

    ads_component_registry_entry_t *client_data = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), ads_id);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find ADS (%ld)\n", ads_id);

    error = ads_rm(&client_data->ads, get_ads_component()->server_vka, vaddr);

err_goto:
    return error;
}