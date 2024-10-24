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
#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/ads_component.h>
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/gpi_rpc.h>
#include <sel4gpi/gpi_elf.h>
#include <ads_component_rpc.pb.h>

// Defined for utility printing macros
#define DEBUG_ID ADS_DEBUG
#define SERVER_ID ADSSERVS
#define DEFAULT_ERR AdsComponentError_UNKNOWN

static int forge_ads_attachments_from_vspace(ads_t *ads, gpi_obj_id_t client_pd_id);
static int forge_ads_attachment_from_res(ads_t *ads, sel4utils_res_t *res, gpi_obj_id_t client_pd_id);

resource_component_context_t *get_ads_component(void)
{
    return &get_gpi_server()->ads_component;
}

// Called when an item from the ADS registry is deleted
static void on_ads_registry_delete(resource_registry_node_t *node_gen, void *arg)
{
    ads_component_registry_entry_t *node = (ads_component_registry_entry_t *)node_gen;

    OSDB_PRINTF("Destroying ADS (%u)\n", node->ads.id);

    resource_component_remove_from_rt(get_ads_component(), node->ads.id);

    // Destroy the VMR space with the ADS object
    int error = resource_component_dec(get_resspc_component(), node->ads.id);

    if (error)
    {
        OSDB_PRINTWARN("Failed to destroy VMR space while destroying ADS (%u)\n", node->ads.id);
    }

    // Finally destroy the ADS object
    ads_destroy(&node->ads);
}

// Create a VMR space for an ADS and create the corresponding RDE
static int create_vmr_space(gpi_obj_id_t client_id, resspc_component_registry_entry_t **ret_space)
{
    int error = 0;

    /* ADS is also a VMR resource space, allocate a new VMR space */
    resspc_component_registry_entry_t *space_entry;

    resspc_config_t resspc_config = {
        .type = GPICAP_TYPE_VMR,
        .ep = get_gpi_server()->server_ep_obj.cptr,
        .pd_id = get_gpi_server()->rt_pd_id,
    };

    error = resource_component_allocate(get_resspc_component(), get_gpi_server()->rt_pd_id, BADGE_OBJ_ID_NULL, false,
                                        (void *)&resspc_config, (resource_registry_node_t **)&space_entry, NULL);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new VMR resource space\n");
    *ret_space = space_entry;

    // Add the VMR RDE for the VMR space
    if (client_id != get_gpi_server()->rt_pd_id)
    {
        pd_component_registry_entry_t *client_pd_data = pd_component_registry_get_entry_by_id(client_id);
        SERVER_GOTO_IF_COND(client_pd_data == NULL, "Couldn't find PD (%u)\n", client_id);

        rde_type_t type = {.type = GPICAP_TYPE_VMR};
        error = pd_add_rde(&client_pd_data->pd, type, "VMR", space_entry->space.id, get_ads_component()->server_ep);
        SERVER_GOTO_IF_ERR(error, "Couldn't add VMR (%u) to PD (%u)'s RDE\n", space_entry->space.id, client_id);
    }

    /* Map the VMR space to the default MO space */
    error = resspc_component_map_space(space_entry->space.id, get_mo_component()->space_id);
    SERVER_GOTO_IF_ERR(error, "Failed to map new VMR space (%u) to MO space (%u)\n",
                       space_entry->space.id, get_mo_component()->space_id);

    OSDB_PRINTF("Added new VMR (%u) RDE to PD (%u)\n", space_entry->space.id, client_id);

err_goto:
    return error;
}

int ads_component_allocate(gpi_obj_id_t client_id, ads_t **ret_ads, seL4_CPtr *ret_cap)
{
    int error = 0;

    /* Create the VMR space */
    resspc_component_registry_entry_t *space_entry;
    error = create_vmr_space(client_id, &space_entry);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new VMR space\n");

    /* Create the ADS object, the ADS ID is the same as the VMR space */
    ads_component_registry_entry_t *new_entry;
    error = resource_component_allocate(get_ads_component(), client_id, space_entry->space.id, false, NULL,
                                        (resource_registry_node_t **)&new_entry, ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new ADS\n");

    *ret_ads = &new_entry->ads;

err_goto:
    return error;
}

static void handle_ads_allocation(seL4_Word sender_badge,
                                  AdsReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got ADS allocation request from: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    seL4_CPtr ret_cap;
    ads_t *ads;
    gpi_obj_id_t client_id = get_client_id_from_badge(sender_badge);

    /* Create the ADS object */
    error = ads_component_allocate(client_id, &ads, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new ADS\n");

    OSDB_PRINTF("Successfully allocated a new ADS (%u).\n", ads->id);

    /* Return this badged end point in the return message. */
    reply_msg->msg.alloc.id = ads->id;
    reply_msg->msg.alloc.slot = ret_cap;

err_goto:
    reply_msg->which_msg = AdsReturnMessage_alloc_tag;
    reply_msg->errorCode = error;
}

static void handle_vmr_reserve_req(seL4_Word sender_badge,
                                   VmrReserveMessage *msg, AdsReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got ADS reserve request from %lx\n", sender_badge);
    int error = 0;

    gpi_obj_id_t client_id = get_client_id_from_badge(sender_badge);
    void *vaddr = (void *)msg->vaddr;
    size_t size = (size_t)msg->size;
    size_t page_bits = (size_t)msg->page_bits;
    sel4utils_reservation_type_t vmr_type = (sel4utils_reservation_type_t)msg->type;
    uint32_t num_pages = DIV_ROUND_UP(size, SIZE_BITS_TO_BYTES(page_bits));
    seL4_CPtr ret_cap;

    /* Find the ADS */
    gpi_obj_id_t ads_id = get_space_id_from_badge(sender_badge);
    ads_component_registry_entry_t *ads_entry = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), ads_id);
    SERVER_GOTO_IF_COND(ads_entry == NULL, "Couldn't find ADS (%u)\n", ads_id);

    /* Find the PD */
    pd_component_registry_entry_t *pd_data = pd_component_registry_get_entry_by_id(client_id);
    SERVER_GOTO_IF_COND(pd_data == NULL, "Couldn't find PD (%u)\n", client_id);

    // Make the reservation
    // (XXX) Arya: add cacheable / rights to client api
    attach_node_t *reservation;
    error = ads_reserve(&ads_entry->ads, vaddr, num_pages, page_bits, vmr_type, 1, seL4_AllRights, &reservation);
    SERVER_GOTO_IF_ERR(error, "Failed to make reservation (%p)\n", vaddr);

    // Make a cap for the reservation
    // The object ID is the shorter map entry ID, not the full vaddr of the reservation
    ret_cap = resource_component_make_badged_ep(get_ads_component()->server_vka, pd_data->pd.pd_vka,
                                                get_ads_component()->server_ep, GPICAP_TYPE_VMR, ads_id,
                                                reservation->map_entry->gen.object_id, client_id);
    SERVER_GOTO_IF_COND(ret_cap == seL4_CapNull, "Failed to make badged ep for reservation\n");

    OSDB_PRINTF("Successfully reserved an ads region (%s) at %p.\n",
                human_readable_va_res_type(vmr_type), reservation->vaddr);

    // Return the badged EP
    reply_msg->msg.reserve.vaddr = (uint64_t)reservation->vaddr;
    reply_msg->msg.reserve.slot = (uint64_t)ret_cap;

err_goto:
    reply_msg->which_msg = AdsReturnMessage_reserve_tag;
    reply_msg->errorCode = error;
}

static void handle_vmr_attach_no_reserve_req(seL4_Word sender_badge,
                                             VmrAttachNoReserveMessage *msg, AdsReturnMessage *reply_msg,
                                             seL4_CPtr mo_cap)
{
    OSDB_PRINTF("Got attach request from client badge: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    SERVER_GOTO_IF_COND(!sel4gpi_rpc_check_cap(GPICAP_TYPE_MO), "Did not receive MO cap\n");

    sel4utils_reservation_type_t vmr_type = (sel4utils_reservation_type_t)msg->type;
    void *vaddr = (void *)msg->vaddr;
    seL4_Word mo_badge = seL4_GetBadge(0);
    gpi_obj_id_t mo_id = get_object_id_from_badge(mo_badge);
    gpi_obj_id_t ads_id = get_space_id_from_badge(sender_badge);

    SERVER_GOTO_IF_COND(get_cap_type_from_badge(mo_badge) != GPICAP_TYPE_MO,
                        "Bad attach request, expected MO but got %s instead: %lx\n",
                        cap_type_to_str(get_cap_type_from_badge(mo_badge)), mo_badge);

    error = ads_component_attach(ads_id, mo_id, vmr_type, vaddr, &vaddr);

    OSDB_PRINTF("Successfully reserved an ads region (%s) at %p and attached MO (%u).\n",
                human_readable_va_res_type(vmr_type), vaddr, mo_id);

    reply_msg->msg.attach_no_reserve.vaddr = (uint64_t)vaddr;

err_goto:
    reply_msg->which_msg = AdsReturnMessage_attach_no_reserve_tag;
    reply_msg->errorCode = error;
}

static void handle_vmr_attach_req(seL4_Word sender_badge,
                                  VmrAttachMessage *msg, AdsReturnMessage *reply_msg,
                                  seL4_CPtr mo_cap)
{
    OSDB_PRINTF("Got attach-to-reserve request from client badge %lx.\n", sender_badge);

    int error = 0;
    SERVER_GOTO_IF_COND(!sel4gpi_rpc_check_cap(GPICAP_TYPE_MO), "Did not receive MO cap\n");

    size_t offset = msg->offset;
    gpi_obj_id_t reservation_id = get_object_id_from_badge(sender_badge);
    seL4_Word mo_badge = seL4_GetBadge(0);
    gpi_obj_id_t mo_id = get_object_id_from_badge(mo_badge);

    SERVER_GOTO_IF_COND(get_cap_type_from_badge(mo_badge) != GPICAP_TYPE_MO, "Bad attach request, given MO EP is not an MO\n");

    /* Find the ADS */
    gpi_obj_id_t ads_id = get_space_id_from_badge(sender_badge);
    ads_component_registry_entry_t *ads_entry = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), ads_id);
    SERVER_GOTO_IF_COND(ads_entry == NULL, "Couldn't find ADS (%u)\n", ads_id);

    /* Find the MO and reservation */
    mo_component_registry_entry_t *mo_reg = (mo_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_mo_component(), mo_id);

    attach_node_t *reservation = ads_get_res_by_id(&ads_entry->ads, reservation_id);

    SERVER_GOTO_IF_COND(mo_reg == NULL, "Couldn't find MO (%u)\n", mo_id);
    SERVER_GOTO_IF_COND(reservation == NULL, "Couldn't find reservation (%u)\n", reservation_id);

    error = ads_attach_to_res(&ads_entry->ads, get_ads_component()->server_vka, reservation, offset, &mo_reg->mo);

    OSDB_PRINTF("Successfully attached MO (%u) to ads region (%s, %p).\n",
                mo_id, human_readable_va_res_type(reservation->type), reservation->vaddr);

err_goto:
    reply_msg->which_msg = AdsReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_vmr_delete_by_vaddr_req(seL4_Word sender_badge,
                                           VmrDeleteByVaddrMessage *msg, AdsReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got remove request from client badge %lx.\n", sender_badge);

    int error = 0;
    gpi_obj_id_t ads_id = get_space_id_from_badge(sender_badge);
    void *vaddr = (void *)msg->vaddr;

    /* Perform the removal */
    error = ads_component_rm_by_vaddr(ads_id, vaddr);

err_goto:
    reply_msg->which_msg = AdsReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_vmr_delete_req(seL4_Word sender_badge,
                                  VmrDeleteMessage *msg, AdsReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got vmr delete request from client badge %lx.\n", sender_badge);

    int error = 0;
    gpi_obj_id_t ads_id = get_space_id_from_badge(sender_badge);
    gpi_obj_id_t vmr_id = get_object_id_from_badge(sender_badge);

    /* Perform the removal */
    error = ads_component_rm_by_id(ads_id, vmr_id);

err_goto:
    reply_msg->which_msg = AdsReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_vmr_disconnect_req(seL4_Word sender_badge,
                                      VmrDisconnectMessage *msg, AdsReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got vmr disconnect request from client badge %lx.\n", sender_badge);

    int error = 0;
    gpi_obj_id_t ads_id = get_space_id_from_badge(sender_badge);

    assert(!"Not implemented\n");

err_goto:
    reply_msg->which_msg = AdsReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_copy_req(seL4_Word sender_badge, AdsShallowCopyMessage *msg,
                            AdsReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got copy request from client badge: ");
    BADGE_PRINT(sender_badge);
    int error = 0;
    seL4_MessageInfo_t reply_tag;
    seL4_CPtr ret_cap;
    gpi_obj_id_t client_id = get_client_id_from_badge(sender_badge);
    SERVER_GOTO_IF_COND(!sel4gpi_rpc_check_cap(GPICAP_TYPE_ADS), "Did not receive ADS cap\n");

    /* Find the client */
    ads_component_registry_entry_t *src_ads_data = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_ads_component(), sender_badge);
    SERVER_GOTO_IF_COND_BG(src_ads_data == NULL, sender_badge, "Couldn't find source ADS: ");

    seL4_Word dst_ads_badge = seL4_GetBadge(0);
    assert(get_cap_type_from_badge(dst_ads_badge) == GPICAP_TYPE_ADS);
    ads_component_registry_entry_t *dst_ads_data = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_ads_component(), dst_ads_badge);
    SERVER_GOTO_IF_COND_BG(dst_ads_data == NULL, dst_ads_badge, "Couldn't find dst ADS: ");

    vmr_config_t cfg = {.start = (void *)msg->src_vaddr,
                        .dest_start = (void *)msg->dest_vaddr,
                        .region_pages = msg->pages,
                        .type = (sel4utils_reservation_type_t)msg->type};

    error = ads_shallow_copy(get_ads_component()->server_vspace, get_ads_component()->server_vka,
                             &src_ads_data->ads, &dst_ads_data->ads, &cfg);

err_goto:
    reply_msg->which_msg = AdsReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

int ads_component_load_elf(ads_t *ads, pd_t *target_pd, char *image_name, void **entry_point)
{
    int error = 0;

    // Find root task's ADS
    ads_component_registry_entry_t *root_ads = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), get_gpi_server()->rt_ads_id);
    SERVER_GOTO_IF_COND(root_ads == NULL, "Couldn't find root task ADS (%u)\n",
                        get_gpi_server()->rt_ads_id);

    // Load the ELF
    OSDB_PRINTF("Loading %s's ELF into PD %u\n", image_name, target_pd->id);
    error = ads_load_elf(ads, &root_ads->ads, target_pd, image_name, entry_point);
    SERVER_GOTO_IF_ERR(error, "Load ELF failed\n");

    OSDB_PRINTF("Successfully loaded ELF, entry point %p.\n", entry_point);

    pd_set_name(target_pd, image_name);

err_goto:
    return error;
}

static void handle_load_elf_request(seL4_Word sender_badge,
                                    AdsLoadElfMessage *msg, AdsReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got load elf request from client badge %lx.\n", sender_badge);

    int error = 0;
    void *entry_point;
    SERVER_GOTO_IF_COND(!sel4gpi_rpc_check_cap(GPICAP_TYPE_PD), "Did not receive PD cap\n");

    // Find target ADS
    ads_component_registry_entry_t *target_ads = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_ads_component(), sender_badge);
    SERVER_GOTO_IF_COND(target_ads == NULL, "Couldn't find target ADS (%u)\n",
                        get_object_id_from_badge(sender_badge));

    // Find target PD
    pd_component_registry_entry_t *target_pd =
        pd_component_registry_get_entry_by_id(get_object_id_from_badge(seL4_GetBadge(0)));
    SERVER_GOTO_IF_COND(target_pd == NULL, "Couldn't find target PD (%u)\n",
                        get_object_id_from_badge(seL4_GetBadge(0)));

    error = ads_component_load_elf(&target_ads->ads, &target_pd->pd, msg->image_name, &entry_point);
    SERVER_GOTO_IF_ERR(error, "Load ELF failed\n");

    reply_msg->msg.load_elf.entry_point = (uint64_t)entry_point;

err_goto:
    reply_msg->which_msg = AdsReturnMessage_load_elf_tag;
    reply_msg->errorCode = error;
}

static void handle_get_res_request(seL4_Word sender_badge,
                                   AdsGetReservationMessage *msg,
                                   AdsReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got Get Reservation Request from Client: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    linked_list_t *found_res = NULL;

    // Find target ADS
    ads_component_registry_entry_t *target_ads = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_ads_component(), sender_badge);
    SERVER_GOTO_IF_COND(target_ads == NULL, "Couldn't find target ADS (%u)\n",
                        get_object_id_from_badge(sender_badge));

    found_res = ads_get_res_by_type(&target_ads->ads, msg->type);
    attach_node_t *res = (attach_node_t *)linked_list_get_at_idx(found_res, 0);
    if (res)
    {
        reply_msg->msg.get_res.vaddr = (uint64_t)res->vaddr;
        reply_msg->msg.get_res.num_pages = res->n_pages;
        reply_msg->msg.get_res.page_bits = res->page_bits;
    }

err_goto:
    if (found_res)
    {
        linked_list_destroy(found_res, false);
    }
    reply_msg->which_msg = AdsReturnMessage_get_res_tag;
    reply_msg->errorCode = error;
}

static void handle_disconnect_request(seL4_Word sender_badge,
                                      AdsDisconnectMessage *msg,
                                      AdsReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got disconnect request from Client: ");
    BADGE_PRINT(sender_badge);

    int error = 0;

    gpi_obj_id_t ads_id = get_object_id_from_badge(sender_badge);

    /* Find the PD */
    pd_component_registry_entry_t *pd_data =
        pd_component_registry_get_entry_by_id(get_client_id_from_badge(sender_badge));
    SERVER_GOTO_IF_COND(pd_data == NULL, "Couldn't find PD (%u)\n", get_client_id_from_badge(sender_badge));

    /* Remove the MO from the client ADS */
    error = pd_remove_resource(&pd_data->pd, make_res_id(GPICAP_TYPE_ADS, get_ads_component()->space_id, ads_id));
    SERVER_GOTO_IF_ERR(error, "Failed to remove MO from PD\n");

    // This will reduce the refcount of the ADS, and then it will be deleted if necessary

err_goto:

    reply_msg->which_msg = AdsReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

/**
 * @brief forges an ADS attachment (and MO) from a given reservation.
 * Since this is currently only used for forging ELF regions, assume that page sizes are 4KB
 *
 * @param ads the ADS to forge the attachment in
 * @param res the reservation to forge
 * @param client_pd_id the ID of the PD which should hold the MO backing the attachment (if it exists)
 * @return int 0 on success
 */
static int forge_ads_attachment_from_res(ads_t *ads, sel4utils_res_t *res, gpi_obj_id_t client_pd_id)
{
    assert(ads != NULL);
    assert(res != NULL);
    int error = 0;

    /* Get the caps in the reservation */
    uint32_t num_frames = (res->end - res->start) / PAGE_SIZE_4K;
    seL4_CPtr *frame_caps = calloc(num_frames, sizeof(seL4_CPtr));
    assert(frame_caps != NULL);

    int i = 0;
    for (void *start = (void *)res->start;
         start < (void *)res->end;
         start += PAGE_SIZE_4K)
    {
        frame_caps[i] = vspace_get_cap(ads->vspace, start);
        i++;
    }

    OSDB_PRINTF("Forging MO/attach for reservation [%p,%p]\n", (void *)res->start, (void *)res->end);

    // (XXX) Arya: This may have issues if the region is not mapped to physical pages everywhere
    seL4_CPtr cap_ret;
    mo_t *mo_ret;
    error = forge_mo_cap_from_frames(frame_caps,
                                     num_frames,
                                     client_pd_id,
                                     &cap_ret,
                                     &mo_ret);
    SERVER_GOTO_IF_ERR(error, "Failed to forge MO cap while forging ADS attach\n");

    res->mo_ref = (void *)mo_ret;

    // Add the attach node for this region
    error = ads_forge_attach(ads, res, res->mo_ref);
    SERVER_GOTO_IF_ERR(error, "Failed to forge ADS attach\n");

    // Since the root task "holds" these MOs, decrement the refcount
    // The refcount then will only be 1, for the attachment to the forged ADS
    if (client_pd_id == get_gpi_server()->rt_pd_id)
    {
        error = resource_component_dec(get_mo_component(), mo_ret->id);
        SERVER_GOTO_IF_ERR(error, "Failed to decrement refcount of MO\n");
    }

err_goto:
    if (frame_caps)
    {
        free(frame_caps);
    }

    return error;
}

/**
 * Walks the ADS' vspace and creates ADS attach nodes for any vspace reservations that
 * do not have an attach node.
 *
 * Eventually we should not need this at all.
 */
static int forge_ads_attachments_from_vspace(ads_t *ads, gpi_obj_id_t client_pd_id)
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
        error = forge_ads_attachment_from_res(ads, res, client_pd_id);
        SERVER_GOTO_IF_ERR(error, "Failed to forge attach node from reservation\n");
        res = res->next;
    }

    OSDB_PRINTF("--- Finish forging ADS attach nodes from vspace --- \n");

err_goto:
    return error;
}

static void vmr_component_handle(void *msg_p,
                                 seL4_Word sender_badge,
                                 seL4_CPtr received_cap,
                                 void *reply_msg_p,
                                 bool *need_new_recv_cap,
                                 bool *should_reply)
{
    int error = 0;
    AdsMessage *msg = (AdsMessage *)msg_p;
    AdsReturnMessage *reply_msg = (AdsReturnMessage *)reply_msg_p;

    SERVER_GOTO_IF_COND(msg->magic != VMR_RPC_MAGIC,
                        "VMR component received message with incorrect magic number %lx\n", msg->magic);

    // Message for the VMR component
    if (get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL)
    {
        switch (msg->which_msg)
        {
        case AdsMessage_attach_no_reserve_tag:
            handle_vmr_attach_no_reserve_req(sender_badge, &msg->msg.attach_no_reserve, reply_msg, received_cap);
            *need_new_recv_cap = true;
            break;
        case AdsMessage_reserve_tag:
            handle_vmr_reserve_req(sender_badge, &msg->msg.reserve, reply_msg);
            break;
        case AdsMessage_delete_by_vaddr_tag:
            handle_vmr_delete_by_vaddr_req(sender_badge, &msg->msg.delete_by_vaddr, reply_msg);
            break;
        default:
            SERVER_GOTO_IF_COND(1, "Unknown request received: %u\n", msg->which_msg);
            break;
        }
    }
    else
    {
        switch (msg->which_msg)
        {
        case AdsMessage_attach_tag:
            handle_vmr_attach_req(sender_badge, &msg->msg.attach, reply_msg, received_cap);
            *need_new_recv_cap = true;
            break;
        case AdsMessage_delete_tag:
            handle_vmr_delete_req(sender_badge, &msg->msg.delete, reply_msg);
            break;
        case AdsMessage_disconnect_tag:
            handle_vmr_disconnect_req(sender_badge, &msg->msg.vmr_disconnect, reply_msg);
            break;
        default:
            SERVER_GOTO_IF_COND(1, "Unknown request received: %u\n", msg->which_msg);
            break;
        }
    }

    OSDB_PRINTF("Returning from VMR component with error code %u\n", reply_msg->errorCode);
    return;

err_goto:
    OSDB_PRINTF("Returning from VMR component with error code %u\n", error);
    reply_msg->errorCode = error;
}

static void ads_component_handle(void *msg_p,
                                 seL4_Word sender_badge,
                                 seL4_CPtr received_cap,
                                 void *reply_msg_p,
                                 bool *need_new_recv_cap,
                                 bool *should_reply)
{
    int error = 0; // unused, to appease the error handling macros

    // Redirect VMR component messages
    if (get_cap_type_from_badge(sender_badge) == GPICAP_TYPE_VMR)
    {
        vmr_component_handle(msg_p, sender_badge, received_cap, reply_msg_p, need_new_recv_cap, should_reply);
        return;
    }

    // Handle ADS component messages
    AdsMessage *msg = (AdsMessage *)msg_p;
    AdsReturnMessage *reply_msg = (AdsReturnMessage *)reply_msg_p;

    SERVER_GOTO_IF_COND(msg->magic != ADS_RPC_MAGIC,
                        "VMR component received message with incorrect magic number %lx\n", msg->magic);

    // Message for the ADS component
    if (get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL &&
        get_space_id_from_badge(sender_badge) == get_ads_component()->space_id)
    {
        SERVER_GOTO_IF_COND(msg->which_msg != AdsMessage_alloc_tag,
                            "Received invalid request on the ADS allocation endpoint\n");
        handle_ads_allocation(sender_badge, reply_msg);
    }
    else
    {
        switch (msg->which_msg)
        {
        case AdsMessage_shallow_copy_tag:
            handle_copy_req(sender_badge, &msg->msg.shallow_copy, reply_msg);
            break;
        case AdsMessage_load_elf_tag:
            handle_load_elf_request(sender_badge, &msg->msg.load_elf, reply_msg);
            break;
        case AdsMessage_get_res_tag:
            handle_get_res_request(sender_badge, &msg->msg.get_res, reply_msg);
            break;
        case AdsMessage_disconnect_tag:
            handle_disconnect_request(sender_badge, &msg->msg.disconnect, reply_msg);
            break;
        default:
            SERVER_GOTO_IF_COND(1, "Unknown request received: %u\n", msg->which_msg);
            break;
        }
    }

    OSDB_PRINTF("Returning from ADS component with error code %u\n", reply_msg->errorCode);
    return;

err_goto:
    OSDB_PRINTF("Returning from ADS component with error code %u\n", error);
    reply_msg->errorCode = error;
}

int ads_component_initialize(vka_t *server_vka,
                             vspace_t *server_vspace,
                             vka_object_t server_ep_obj)
{
    int error = 0;

    // Create the default ADS resource space
    resspc_component_registry_entry_t *space_entry;

    resspc_config_t resspc_config = {
        .type = GPICAP_TYPE_ADS,
        .ep = get_gpi_server()->server_ep_obj.cptr,
        .pd_id = get_gpi_server()->rt_pd_id,
    };

    error = resource_component_allocate(get_resspc_component(), get_gpi_server()->rt_pd_id, BADGE_OBJ_ID_NULL, false,
                                        (void *)&resspc_config, (resource_registry_node_t **)&space_entry, NULL);
    assert(error == 0);

    // Initialize the component
    resource_component_initialize(get_ads_component(),
                                  GPICAP_TYPE_ADS,
                                  space_entry->space.id,
                                  ads_component_handle,
                                  (int (*)(resource_component_object_t *, vka_t *, vspace_t *, void *))ads_new,
                                  on_ads_registry_delete,
                                  sizeof(ads_component_registry_entry_t),
                                  server_vka,
                                  server_vspace,
                                  server_ep_obj.cptr,
                                  &AdsMessage_msg,
                                  &AdsReturnMessage_msg);

    // Initial ASID pool is provided by the kernel
    get_ads_component()->pool = seL4_CapInitThreadASIDPool;
}

/** --- Functions callable by root task --- **/

int forge_ads_cap_from_vspace(vspace_t *vspace, vka_t *vka, gpi_obj_id_t client_pd_id,
                              seL4_CPtr *cap_ret, gpi_obj_id_t *id_ret)
{
    OSDB_PRINTF("Forging ADS cap from vspace\n");

    int error = 0;
    seL4_CPtr ret_cap;
    ads_component_registry_entry_t *new_entry;

    /* Create the VMR space */
    resspc_component_registry_entry_t *space_entry;
    error = create_vmr_space(client_pd_id, &space_entry);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new VMR space\n");

    /* Allocate the ADS */
    error = resource_component_allocate(get_ads_component(), client_pd_id, space_entry->space.id, false, NULL,
                                        (resource_registry_node_t **)&new_entry, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate ADS for forging\n");

    /* Perform any initialization of the forged ADS object */
    error = ads_initialize(&new_entry->ads);
    SERVER_GOTO_IF_ERR(error, "Failed to initialize forged ADS\n");

    /* Update the ADS object with the vspace data */
    new_entry->ads.vspace = vspace;

    /* Forge attachments for existing reservations in the vspace */
    // The client for these MOs is always the root task
    error = forge_ads_attachments_from_vspace(&new_entry->ads, get_gpi_server()->rt_pd_id);
    SERVER_GOTO_IF_ERR(error, "Failed to forge ADS attachments from vspace\n");

    if (id_ret)
    {
        *id_ret = new_entry->ads.id;
    }

    *cap_ret = ret_cap;

err_goto:
    return error;
}

int ads_component_attach(gpi_obj_id_t ads_id, gpi_obj_id_t mo_id, sel4utils_reservation_type_t vmr_type,
                         void *vaddr, void **ret_vaddr)
{
    int error = 0;

    /* Find the client */
    ads_component_registry_entry_t *ads_entry = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), ads_id);
    SERVER_GOTO_IF_COND(ads_entry == NULL, "Couldn't find ADS (%u)\n", ads_id);

    /* Find the MO */
    mo_component_registry_entry_t *mo_reg = (mo_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_mo_component(), mo_id);
    SERVER_GOTO_IF_COND(mo_reg == NULL, "Couldn't find MO (%u)\n", mo_id);

    /* Attach the MO */
    if (vmr_type <= SEL4UTILS_RES_TYPE_NONE || vmr_type >= SEL4UTILS_RES_TYPE_MAX)
    {
        vmr_type = SEL4UTILS_RES_TYPE_GENERIC;
    }

    // (XXX) Arya: Move cacheable/rights to parameters
    error = ads_attach(&ads_entry->ads,
                       get_ads_component()->server_vka,
                       vaddr,
                       &mo_reg->mo,
                       1,
                       seL4_AllRights,
                       ret_vaddr,
                       vmr_type);

    SERVER_GOTO_IF_ERR(error, "Failed to attach at vaddr (%p) to ADS (%u).\n",
                       vaddr, ads_id);

err_goto:
    return error;
}

int ads_component_rm_by_id(gpi_obj_id_t ads_id, gpi_obj_id_t vmr_id)
{
    int error = 0;

    ads_component_registry_entry_t *ads_entry = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), ads_id);
    SERVER_GOTO_IF_COND(ads_entry == NULL, "Couldn't find ADS (%u)\n", ads_id);

    attach_node_t *attach_node = ads_get_res_by_id(&ads_entry->ads, vmr_id);
    SERVER_GOTO_IF_COND(ads_entry == NULL, "Couldn't find VMR (%u)\n", vmr_id);
    error = ads_rm(&ads_entry->ads, get_ads_component()->server_vka, attach_node->vaddr);

err_goto:
    return error;
}

int ads_component_rm_by_vaddr(gpi_obj_id_t ads_id, void *vaddr)
{
    int error = 0;

    ads_component_registry_entry_t *ads_entry = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), ads_id);
    SERVER_GOTO_IF_COND(ads_entry == NULL, "Couldn't find ADS (%u)\n", ads_id);

    error = ads_rm(&ads_entry->ads, get_ads_component()->server_vka, vaddr);

err_goto:
    return error;
}

int ads_component_attach_to_rt(gpi_obj_id_t mo_id, void **ret_vaddr)
{
    return ads_component_attach(get_gpi_server()->rt_ads_id, mo_id, SEL4UTILS_RES_TYPE_GENERIC, NULL, ret_vaddr);
}

int ads_component_remove_from_rt(void *vaddr)
{
    return ads_component_rm_by_vaddr(get_gpi_server()->rt_ads_id, vaddr);
}