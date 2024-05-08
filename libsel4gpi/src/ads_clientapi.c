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

int ads_component_client_connect(seL4_CPtr server_ep_cap,
                                 seL4_CPtr free_slot,
                                 ads_client_context_t *ret_conn)
{

    /* Send a REQ message to the server on its public EP */
    seL4_SetCapReceivePath(SEL4UTILS_CNODE_SLOT, /* Position of the cap to the CNODE */
                           free_slot,            /* CPTR in this CSPACE */
                           /* This works coz we have a single level cnode with no guard.*/
                           seL4_WordBits); /* Depth i.e. how many bits of free_slot to interpret*/

    OSDB_PRINTF(ADS_DEBUG, ADSSERVC "Set a receive path for the badged ep: %d\n", (int)free_slot);

    /* Set request type */
    seL4_SetMR(0, GPICAP_TYPE_ADS);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);

    tag = seL4_Call(server_ep_cap, tag);
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);

    ret_conn->badged_server_ep_cspath.capPtr = free_slot;
    ret_conn->id = seL4_GetMR(ADSMSGREG_CONNECT_ACK_ADS_NS);
    // OSDB_PRINTF(ADS_DEBUG, ADSSERVC"Received badged endpoint and it was kept in:");
    // debug_cap_identify(ADSSERVC, ret_conn->badged_server_ep_cspath.capPtr);
    return 0;
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
    // seL4_SetMR(ADSMSGREG_ATTACH_REQ_SZ, (seL4_Word) size);
    seL4_SetCap(0, mo_cap->badged_server_ep_cspath.capPtr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  ADSMSGREG_ATTACH_REQ_END);

    OSDB_PRINTF(ADS_DEBUG, ADSSERVC "Sending attach request to server via EP: %lu.\n",
                conn->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    *ret_vaddr = (void *)seL4_GetMR(ADSMSGREG_ATTACH_REQ_VA);
    assert(*ret_vaddr != NULL);

    return 0;
}

int ads_client_shallow_copy(ads_client_context_t *conn, seL4_CPtr free_slot, void *omit_vaddr, ads_client_context_t *ret_conn)
{
    // Alloc a slot for the incoming cap.
    /* Send a REQ message to the server on its public EP */
    seL4_SetCapReceivePath(SEL4UTILS_CNODE_SLOT, /* Position of the cap to the CNODE */
                           free_slot,            /* CPTR in this CSPACE */
                           /* This works coz we have a single level cnode with no guard.*/
                           seL4_WordBits); /* Depth i.e. how many bits of free_slot to interpret*/

    OSDB_PRINTF(ADS_DEBUG, ADSSERVC "Set a receive path for the badged ep: %d\n", (int)free_slot);

    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_SHALLOW_COPY_REQ);
    seL4_SetMR(ADSMSGREG_SHALLOW_COPY_REQ_OMIT_VA, omit_vaddr != NULL ? (seL4_Word)omit_vaddr : 0);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  ADSMSGREG_SHALLOW_COPY_REQ_END);

    OSDB_PRINTF(ADS_DEBUG, ADSSERVC "Sending clone request to server via EP: %lu.\n",
                conn->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);
    ret_conn->badged_server_ep_cspath.capPtr = free_slot;
    return 0;
}

/* To Change:
    Alloc a page frame cap:
    Pass it to the server

    On return from the server,
    map the page frame cap to the vaddr
    copy the contents to a malloced region of memory
    unmap the page frame cap
    free the page frame cap

*/
int ads_client_dump_rr(ads_client_context_t *conn, char *ads_rr, size_t size)
{

    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_GET_RR_REQ);
    seL4_SetMR(ADSMSGREG_GET_RR_REQ_BUF_VA, (seL4_Word)ads_rr);
    seL4_SetMR(ADSMSGREG_GET_RR_REQ_BUF_SZ, (seL4_Word)size);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  ADSMSGREG_GET_RR_REQ_END);

    OSDB_PRINTF(ADS_DEBUG, ADSSERVC "Sending dump RR request to server via EP: %lu.\n",
                conn->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    return 0;
}

int ads_client_rm(ads_client_context_t *conn, void *vaddr)
{
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_RM_REQ);
    seL4_SetMR(ADSMSGREG_RM_REQ_VA, (seL4_Word)vaddr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  ADSMSGREG_RM_REQ_END);

    OSDB_PRINTF(ADS_DEBUG, ADSSERVC "Sending remove request to server via EP: %lu.\n",
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
    return 0;
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
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_LOAD_ELF_REQ);
    int image_id = sel4gpi_image_name_to_id(image_name);
    if (image_id == -1)
    {
        OSDB_PRINTF(ADS_DEBUG, ADSSERVC "invalid image name received %s\n", image_name);
        return -1;
    }

    seL4_SetMR(ADSMSGREG_LOAD_ELF_REQ_IMAGE, image_id);
    seL4_SetCap(0, loadee_pd->badged_server_ep_cspath.capPtr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  ADSMSGREG_LOAD_ELF_REQ_END);

    tag = seL4_Call(loadee_ads->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_get_label(tag) == 0);

    *ret_entry_point = (void *)seL4_GetMR(ADSMSGREG_LOAD_ELF_ACK_ENTRY_PT);
    return 0;
}

int ads_client_prepare_stack(ads_client_context_t *target_ads,
                             pd_client_context_t *target_pd,
                             void *stack_top,
                             int stack_size,
                             int argc,
                             seL4_Word *args,
                             void **ret_init_stack)
{
    int error = 0;
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_PROC_SETUP_REQ);
    seL4_SetCap(0, target_pd->badged_server_ep_cspath.capPtr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  ADSMSGREG_PROC_SETUP_REQ_END);

    seL4_SetMR(ADSMSGREG_PROC_SETUP_REQ_ARGC, argc);

    OSDB_PRINTF(ADS_DEBUG, ADSSERVC "Setting up process with args: [");
    for (int i = 0; i < argc; i++)
    {
        OSDB_PRINTF(ADS_DEBUG, "%ld, ", args[i]);

        switch (i)
        {
        case 0:
            seL4_SetMR(ADSMSGREG_PROC_SETUP_REQ_ARG0, args[i]);
            break;
        case 1:
            seL4_SetMR(ADSMSGREG_PROC_SETUP_REQ_ARG1, args[i]);
            break;
        case 2:
            seL4_SetMR(ADSMSGREG_PROC_SETUP_REQ_ARG2, args[i]);
            break;
        case 3:
            seL4_SetMR(ADSMSGREG_PROC_SETUP_REQ_ARG3, args[i]);
            break;
        }
    }
    OSDB_PRINTF(ADS_DEBUG, "]\n");

    seL4_SetMR(ADSMSGREG_PROC_SETUP_REQ_STACK, (seL4_Word)stack_top);
    seL4_SetMR(ADSMSGREG_PROC_SETUP_REQ_STACK_SZ, stack_size);

    tag = seL4_Call(target_ads->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_get_label(tag) == 0);

    *ret_init_stack = (void *)seL4_GetMR(ADSMSGREG_PROC_SETUP_ACK_INIT_STACK);
    return 0;
}
