/**
 * @file ads_clientapi.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the ads client API from ads_client.h.
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <vka/vka.h>
#include <vka/capops.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/gpi_client.h>
#include <sel4gpi/pd_utils.h>

// Defined for utility printing macros
#define DEBUG_ID ADS_DEBUG
#define SERVER_ID ADSSERVC

int ads_component_client_connect(seL4_CPtr server_ep_cap,
                                 seL4_CPtr free_slot,
                                 ads_client_context_t *ret_conn)
{

    /* Send a REQ message to the server on its public EP */
    seL4_SetCapReceivePath(SEL4UTILS_CNODE_SLOT, /* Position of the cap to the CNODE */
                           free_slot,            /* CPTR in this CSPACE */
                           /* This works coz we have a single level cnode with no guard.*/
                           seL4_WordBits); /* Depth i.e. how many bits of free_slot to interpret*/

    OSDB_PRINTF("Set a receive path for the badged ep: %d\n", (int)free_slot);

    /* Set request type */
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_CONNECT_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_CONNECT_REQ_END);

    tag = seL4_Call(server_ep_cap, tag);
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);

    ret_conn->badged_server_ep_cspath.capPtr = free_slot;
    ret_conn->id = seL4_GetMR(ADSMSGREG_CONNECT_ACK_VMR_SPACE_ID);
    // OSDB_PRINTF(ADS_DEBUG, ADSSERVC"Received badged endpoint and it was kept in:");
    // debug_cap_identify(ADSSERVC, ret_conn->badged_server_ep_cspath.capPtr);
    return seL4_MessageInfo_get_label(tag);
}

int ads_client_attach(ads_client_context_t *conn,
                      void *vaddr,
                      mo_client_context_t *mo_cap,
                      sel4utils_reservation_type_t vmr_type,
                      void **ret_vaddr)
{
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_ATTACH_REQ);
    seL4_SetMR(ADSMSGREG_ATTACH_REQ_VA, (seL4_Word)vaddr);
    seL4_SetMR(ADSMSGREG_ATTACH_REQ_TYPE, (seL4_Word)vmr_type);
    seL4_SetCap(0, mo_cap->badged_server_ep_cspath.capPtr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  ADSMSGREG_ATTACH_REQ_END);

    OSDB_PRINTF("Sending attach request to server via EP: %lu.\n",
                conn->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    *ret_vaddr = (void *)seL4_GetMR(ADSMSGREG_ATTACH_ACK_VA);

    return seL4_MessageInfo_get_label(tag);
}

int ads_client_reserve(ads_client_context_t *conn,
                       seL4_CPtr free_slot,
                       void *vaddr,
                       size_t size,
                       sel4utils_reservation_type_t vmr_type,
                       ads_vmr_context_t *ret_conn,
                       void **ret_vaddr)
{
    // Set the receive path for a new connection
    seL4_SetCapReceivePath(SEL4UTILS_CNODE_SLOT,
                           free_slot,
                           seL4_WordBits);

    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_RESERVE_REQ);
    seL4_SetMR(ADSMSGREG_RESERVE_REQ_VA, (seL4_Word)vaddr);
    seL4_SetMR(ADSMSGREG_RESERVE_REQ_TYPE, (seL4_Word)vmr_type);
    seL4_SetMR(ADSMSGREG_RESERVE_REQ_SIZE, (seL4_Word)size);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  ADSMSGREG_RESERVE_REQ_END);

    OSDB_PRINTF("Sending reserve request to server via EP: %lu.\n",
                conn->badged_server_ep_cspath.capPtr);

    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    ret_conn->badged_server_ep_cspath.capPtr = free_slot;

    *ret_vaddr = (void *)seL4_GetMR(ADSMSGREG_RESERVE_ACK_VA);

    OSDB_PRINTF("Finished reserve request, result in slot: %lu.\n",
                free_slot);

    return seL4_MessageInfo_get_label(tag) || (*ret_vaddr == NULL);
}

int ads_client_attach_to_reserve(ads_vmr_context_t *reservation,
                                 mo_client_context_t *mo,
                                 size_t offset)
{
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_ATTACH_RESERVE_REQ);
    seL4_SetMR(ADSMSGREG_ATTACH_RESERVE_REQ_OFFSET, (seL4_Word)offset);

    seL4_SetCap(0, mo->badged_server_ep_cspath.capPtr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  ADSMSGREG_ATTACH_RESERVE_REQ_END);

    OSDB_PRINTF("Sending attach-to-reserve request to server via EP: %lu.\n",
                reservation->badged_server_ep_cspath.capPtr);

    tag = seL4_Call(reservation->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_get_label(tag);
}

int ads_client_shallow_copy(ads_client_context_t *conn, seL4_CPtr free_slot, void *omit_vaddr, ads_client_context_t *ret_conn)
{
    // Alloc a slot for the incoming cap.
    /* Send a REQ message to the server on its public EP */
    seL4_SetCapReceivePath(SEL4UTILS_CNODE_SLOT, /* Position of the cap to the CNODE */
                           free_slot,            /* CPTR in this CSPACE */
                           /* This works coz we have a single level cnode with no guard.*/
                           seL4_WordBits); /* Depth i.e. how many bits of free_slot to interpret*/

    OSDB_PRINTF("Set a receive path for the badged ep: %d\n", (int)free_slot);

    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_SHALLOW_COPY_REQ);
    seL4_SetMR(ADSMSGREG_SHALLOW_COPY_REQ_OMIT_VA, omit_vaddr != NULL ? (seL4_Word)omit_vaddr : 0);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  ADSMSGREG_SHALLOW_COPY_REQ_END);

    OSDB_PRINTF("Sending clone request to server via EP: %lu.\n",
                conn->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    ret_conn->badged_server_ep_cspath.capPtr = free_slot;
    return seL4_MessageInfo_get_label(tag);
}

int ads_client_rm(ads_client_context_t *conn, void *vaddr)
{
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_RM_REQ);
    seL4_SetMR(ADSMSGREG_RM_REQ_VA, (seL4_Word)vaddr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  ADSMSGREG_RM_REQ_END);

    OSDB_PRINTF("Sending remove request to server via EP: %lu.\n",
                conn->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_get_label(tag);
}

int ads_client_bind_cpu(ads_client_context_t *conn, seL4_CPtr cpu_cap)
{
    return 0;
}

int ads_client_testing(ads_client_context_t *conn, vka_t *vka,
                       ads_client_context_t *clone1,
                       ads_client_context_t *clone2,
                       ads_client_context_t *clone3)
{

    int error = 0;

    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_TESTING_REQ);
    seL4_SetCap(0, clone1->badged_server_ep_cspath.capPtr);
    seL4_SetCap(1, clone2->badged_server_ep_cspath.capPtr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 2,
                                                  ADSMSGREG_TESTING_REQ_END);

    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    return seL4_MessageInfo_get_label(tag);
}

int ads_client_copy(ads_client_context_t *src_ads, ads_client_context_t *dst_ads, vmr_config_t *vmr_cfg)
{
    OSDB_PRINTF("Sending %s request for VMR (%s)\n",
                sel4gpi_share_degree_to_str(vmr_cfg->share_mode), human_readable_va_res_type(vmr_cfg->type));
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_SHALLOW_COPY_REQ);
    seL4_SetCap(0, dst_ads->badged_server_ep_cspath.capPtr);
    seL4_SetMR(ADSMSGREG_SHALLOW_COPY_REQ_PAGES, vmr_cfg->region_pages);
    seL4_SetMR(ADSMSGREG_SHALLOW_COPY_REQ_TYPE, (seL4_Word)vmr_cfg->type);
    seL4_SetMR(ADSMSGREG_SHALLOW_COPY_REQ_VA, (seL4_Word)vmr_cfg->start);
    seL4_SetMR(ADSMSGREG_SHALLOW_COPY_REQ_MODE, (seL4_Word)vmr_cfg->share_mode);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  ADSMSGREG_SHALLOW_COPY_REQ_END);

    tag = seL4_Call(src_ads->badged_server_ep_cspath.capPtr, tag);
    return seL4_MessageInfo_get_label(tag);
}

/* ======================================= CONVENIENCE FUNCTIONS (NOT PART OF FRAMEWORK) ================================================= */

/**
 * @brief Load an image's ELF into the given ADS
 *
 * @param loadee_ads
 * @param image_name
 * @return int
 */
int ads_client_load_elf(ads_client_context_t *loadee_ads,
                        pd_client_context_t *loadee_pd,
                        const char *image_name,
                        void **ret_entry_point)
{
    int error = 0;

    // Allocate an MO to send the image name
    ads_client_context_t ads_conn = {0};
    ads_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_space_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_VMR);

    mo_client_context_t mo_conn;
    void *mo_vaddr = sel4gpi_get_vmr(&ads_conn, 1, NULL, SEL4UTILS_RES_TYPE_SHARED_FRAMES, &mo_conn);

    if (mo_vaddr == NULL) {
        return 1;
    }

    strcpy(mo_vaddr, image_name);

    // Make the request
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_LOAD_ELF_REQ);
    seL4_SetCap(0, loadee_pd->badged_server_ep_cspath.capPtr);
    seL4_SetCap(1, mo_conn.badged_server_ep_cspath.capPtr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 2,
                                                  ADSMSGREG_LOAD_ELF_REQ_END);

    tag = seL4_Call(loadee_ads->badged_server_ep_cspath.capPtr, tag);

    *ret_entry_point = (void *)seL4_GetMR(ADSMSGREG_LOAD_ELF_ACK_ENTRY_PT);

    // Free the MO
    sel4gpi_destroy_vmr(&ads_conn, mo_vaddr, &mo_conn);

    return seL4_MessageInfo_get_label(tag);
}
