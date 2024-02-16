/**
 * @file pd_clientapi.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the pd client API from pd_client.h.
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */

#include<sel4gpi/pd_clientapi.h>
#include<sel4gpi/badge_usage.h>
#include<sel4gpi/debug.h>

int pd_component_client_connect(seL4_CPtr server_ep_cap,
                              vka_t *client_vka,
                              pd_client_context_t *ret_conn){

    /* Send a REQ message to the server on its publicS EP */

    // Alloc a slot for the incoming cap.
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(client_vka, &dest_cptr);
    cspacepath_t path;
    vka_cspace_make_path(client_vka, dest_cptr, &path);
    seL4_SetCapReceivePath(
        /* _service */      path.root,
        /* index */         path.capPtr,
        /* depth */         path.capDepth
    );

    OSDB_PRINTF(PDSERVC "%s %d pd_endpoint is %lu:__ \n", __FUNCTION__, __LINE__, server_ep_cap);
   // debug_cap_identify(PDSERVC, server_ep_cap);

    OSDB_PRINTF(PDSERVC"Set a receive path for the badged ep: %lu\n", path.capPtr);
    /* Set request type */
    seL4_SetMR(0, GPICAP_TYPE_PD);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);

    tag = seL4_Call(server_ep_cap,tag);
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);

    ret_conn->badged_server_ep_cspath = path;;

    OSDB_PRINTF(PDSERVC"received badged endpoint and it was kept in %lu:__\n", path.capPtr);
    // debug_cap_identify(PDSERVC, path.capPtr);
    return 0;
}

int pd_client_load(pd_client_context_t *pd_os_cap,
                   ads_client_context_t *ads_os_cap,
                   const char *image)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_LOAD_REQ);

    /* Send the badged endpoint cap of the ads client as a cap */
    int image_id = -1;
    for (int i = 0; i < PD_N_IMAGES; i++) {
        if (strcmp(image, pd_images[i]) == 0) {
            OSDB_PRINTF(PDSERVC "image id is %d\n", i);
            image_id = i;
            break;
        }
    }

    if (image_id == -1) {
        OSDB_PRINTF(PDSERVC "invalid image name received %s\n", image);
        return -1;
    }

    seL4_SetMR(PDMSGREG_LOAD_FUNC_IMAGE, image_id);
    seL4_SetCap(0, ads_os_cap->badged_server_ep_cspath.capPtr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  PDMSGREG_LOAD_REQ_END);

    tag = seL4_Call(pd_os_cap->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    return 0;
}

int pd_client_dump(pd_client_context_t *conn,
                    char *buf,
                    size_t buf_sz)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_DUMP_REQ);

    seL4_SetMR(PDMSGREG_DUMP_REQ_BUF_VA, (seL4_Word) buf);
    seL4_SetMR(PDMSGREG_DUMP_REQ_BUF_SZ, (seL4_Word) buf_sz);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                    PDMSGREG_DUMP_REQ_END);

    OSDB_PRINTF(ADSSERVC "Sending dump RR request to PD via EP: %lu.\n",
           conn->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    return 0;
}

int pd_client_send_cap(pd_client_context_t *conn, seL4_CPtr cap_to_send,
                       seL4_Word *slot)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SENDCAP_REQ);
    seL4_SetCap(0, cap_to_send);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  PDMSGREG_SEND_CAP_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    *slot = seL4_GetMR(PDMSGREG_SEND_CAP_PD_SLOT);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    assert(*slot != 0);
    return 0;
}

int pd_client_next_slot(pd_client_context_t *conn,
                        seL4_Word *slot)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_NEXT_SLOT_REQ);
   seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_NEXT_SLOT_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    *slot = seL4_GetMR(PDMSGREG_NEXT_SLOT_PD_SLOT);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    assert(*slot != 0);
    return 0;
}

int pd_client_start(pd_client_context_t *conn, seL4_Word arg0)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_START_REQ);
    seL4_SetMR(PDMSGREG_START_ARG0, arg0);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_START_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    return 0;
}
