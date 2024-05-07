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

#include <vka/capops.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/gpi_client.h>

int pd_component_client_connect(seL4_CPtr server_ep_cap,
                                seL4_CPtr free_slot,
                                pd_client_context_t *ret_conn)
{

    /* Send a REQ message to the server on its public EP */
    seL4_SetCapReceivePath(SEL4UTILS_CNODE_SLOT, /* Position of the cap to the CNODE */
                           free_slot,            /* CPTR in this CSPACE */
                           /* This works coz we have a single level cnode with no guard.*/
                           seL4_WordBits); /* Depth i.e. how many bits of free_slot to interpret*/

    OSDB_PRINTF(PD_DEBUG, PDSERVC "%s %d pd_endpoint is %lu:__ \n", __FUNCTION__, __LINE__, server_ep_cap);
    OSDB_PRINTF(PD_DEBUG, PDSERVC "Set a receive path for the badged ep: %d\n", (int)free_slot);

    /* Set request type */
    seL4_SetMR(0, GPICAP_TYPE_PD);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);

    tag = seL4_Call(server_ep_cap, tag);
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);

    ret_conn->badged_server_ep_cspath.capPtr = free_slot;

    OSDB_PRINTF(PD_DEBUG, PDSERVC "received badged endpoint and it was kept in %d:__\n", (int)free_slot);
    return 0;
}

int pd_client_disconnect(pd_client_context_t *conn)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_DISCONNECT_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_DISCONNECT_REQ_END);

    OSDB_PRINTF(PD_DEBUG, PDSERVC "Sending disconnect request for PD via EP: %lu.\n",
                conn->badged_server_ep_cspath.capPtr);

    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_get_label(tag);
}

int pd_client_load(pd_client_context_t *pd_os_cap,
                   ads_client_context_t *ads_os_cap,
                   cpu_client_context_t *cpu_os_cap,
                   const char *image)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_LOAD_REQ);

    /* Send the badged endpoint cap of the ads client as a cap */
    int image_id = sel4gpi_image_name_to_id(image);
    if (image_id == -1)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVC "invalid image name received %s\n", image);
        return -1;
    }

    seL4_SetMR(PDMSGREG_LOAD_FUNC_IMAGE, image_id);
    seL4_SetCap(0, ads_os_cap->badged_server_ep_cspath.capPtr);
    seL4_SetCap(1, cpu_os_cap->badged_server_ep_cspath.capPtr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 2,
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

    seL4_SetMR(PDMSGREG_DUMP_REQ_BUF_VA, (seL4_Word)buf);
    seL4_SetMR(PDMSGREG_DUMP_REQ_BUF_SZ, (seL4_Word)buf_sz);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_DUMP_REQ_END);

    OSDB_PRINTF(PD_DEBUG, PDSERVC "Sending dump RR request to PD via EP: %lu.\n",
                conn->badged_server_ep_cspath.capPtr);
    // (XXX) Linh: for some reason, this doesn't block if we try to dump a test PD's state and causes issues
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
    OSDB_PRINTF(PD_DEBUG, "pd_client_next_slot call\n");
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    *slot = seL4_GetMR(PDMSGREG_NEXT_SLOT_PD_SLOT);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    assert(*slot != 0);
    return 0;
}

int pd_client_free_slot(pd_client_context_t *conn,
                        seL4_CPtr slot)
{
    // Now get the server to free the slot
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_FREE_SLOT_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_FREE_SLOT_REQ_END);
    seL4_SetMR(PDMSGREG_FREE_SLOT_REQ_SLOT, slot);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    return 0;
}

/**
 * @brief Create a badged copy of an endpoint capability
 *
 * @param conn client connection object
 * @param ret_ep location of result endpoint
 * @return int 0 on success, -1 on failure.
 */
int pd_client_alloc_ep(pd_client_context_t *conn,
                       seL4_CPtr *ret_ep)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_ALLOC_EP_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_ALLOC_EP_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    *ret_ep = seL4_GetMR(PDMSGREG_ALLOC_EP_PD_SLOT);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    assert(*ret_ep != 0);
    return 0;
}

/**
 * @brief Create a badged copy of an endpoint capability
 *
 * @param conn client connection object
 * @param src_ep raw endpoint in pd's cspace
 * @param badge badge to apply to the endpoint
 * @param ret_ep location of result endpoint
 * @return int 0 on success, -1 on failure.
 */
int pd_client_badge_ep(pd_client_context_t *conn,
                       seL4_CPtr src_ep,
                       seL4_Word badge,
                       seL4_CPtr *ret_ep)
{
    int error;
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_BADGE_EP_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  PDMSGREG_BADGE_EP_REQ_END);
    seL4_SetMR(PDMSGREG_BADGE_EP_REQ_BADGE, badge);
    seL4_SetMR(PDMSGREG_BADGE_EP_REQ_SRC, src_ep);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    *ret_ep = seL4_GetMR(PDMSGREG_BADGE_EP_PD_SLOT);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    assert(*ret_ep != 0);
    return 0;
}

int pd_client_start(pd_client_context_t *conn, int argc, seL4_Word *args)
{
    if (argc > PD_MAX_ARGC)
    {
        ZF_LOGE(PDSERVC "invalid argc (%d) to start pd client, max is (%d)\n", argc, PD_MAX_ARGC);
        return -1;
    }
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_START_REQ);

    // Setup the arguments
    OSDB_PRINTF(PD_DEBUG, PDSERVC "Starting PD with %d args: [", argc);

    seL4_SetMR(PDMSGREG_START_ARGC, argc);

    for (int i = 0; i < argc; i++)
    {
        OSDB_PRINTF(PD_DEBUG, "%ld, ", args[i]);

        switch (i)
        {
        case 0:
            seL4_SetMR(PDMSGREG_START_ARG0, args[i]);
            break;
        case 1:
            seL4_SetMR(PDMSGREG_START_ARG1, args[i]);
            break;
        case 2:
            seL4_SetMR(PDMSGREG_START_ARG2, args[i]);
            break;
        case 3:
            seL4_SetMR(PDMSGREG_START_ARG3, args[i]);
            break;
        }
    }
    OSDB_PRINTF(PD_DEBUG, "]\n");

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_START_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    return 0;
}

int pd_client_add_rde(pd_client_context_t *conn,
                      seL4_CPtr server_pd,
                      uint64_t manager_id,
                      uint64_t ns_id)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_ADD_RDE_REQ);
    seL4_SetMR(PDMSGREG_ADD_RDE_REQ_MANAGER_ID, manager_id);
    seL4_SetMR(PDMSGREG_ADD_RDE_REQ_NSID, ns_id);
    seL4_SetCap(0, server_pd);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  PDMSGREG_ADD_RDE_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    return 0;
}

int pd_client_share_rde(pd_client_context_t *conn,
                        gpi_cap_t cap_type,
                        uint64_t ns_id)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SHARE_RDE_REQ);
    seL4_SetMR(PDMSGREG_SHARE_RDE_REQ_TYPE, cap_type);
    seL4_SetMR(PDMSGREG_SHARE_RDE_REQ_NS, ns_id);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  PDMSGREG_SHARE_RDE_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    return 0;
}

int pd_client_register_resource_manager(pd_client_context_t *conn,
                                        gpi_cap_t resource_type,
                                        seL4_CPtr server_ep,
                                        seL4_Word *manager_id)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  PDMSGREG_REGISTER_SERV_REQ_END);
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_REGISTER_SERV_REQ);
    seL4_SetMR(PDMSGREG_REGISTER_SERV_REQ_TYPE, resource_type);
    seL4_SetCap(0, server_ep);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    *manager_id = seL4_GetMR(PDMSGREG_REGISTER_SERV_ACK_ID);
    return 0;
}

int pd_client_register_namespace(pd_client_context_t *conn,
                                 seL4_Word manager_id,
                                 seL4_Word client_id,
                                 seL4_Word *ns_id)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_REGISTER_NS_REQ_END);
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_REGISTER_NS_REQ);
    seL4_SetMR(PDMSGREG_REGISTER_NS_REQ_MANAGER_ID, manager_id);
    seL4_SetMR(PDMSGREG_REGISTER_NS_REQ_CLIENT_ID, client_id);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    *ns_id = seL4_GetMR(PDMSGREG_REGISTER_NS_ACK_NSID);
    return 0;
}

int pd_client_create_resource(pd_client_context_t *conn,
                              gpi_cap_t manager_id,
                              seL4_Word resource_id)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_CREATE_RES_REQ);
    seL4_SetMR(PDMSGREG_CREATE_RES_REQ_MANAGER_ID, manager_id);
    seL4_SetMR(PDMSGREG_CREATE_RES_REQ_RES_ID, resource_id);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_CREATE_RES_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    return 0;
}

int pd_client_give_resource(pd_client_context_t *conn,
                            seL4_Word manager_id,
                            seL4_Word ns_id,
                            seL4_Word recipient_id,
                            seL4_Word resource_id,
                            seL4_CPtr *dest)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_GIVE_RES_REQ);
    seL4_SetMR(PDMSGREG_GIVE_RES_REQ_MANAGER_ID, manager_id);
    seL4_SetMR(PDMSGREG_GIVE_RES_REQ_NS_ID, ns_id);
    seL4_SetMR(PDMSGREG_GIVE_RES_REQ_CLIENT_ID, recipient_id);
    seL4_SetMR(PDMSGREG_GIVE_RES_REQ_RES_ID, resource_id);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_GIVE_RES_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    *dest = seL4_GetMR(PDMSGREG_GIVE_RES_ACK_DEST);
    return 0;
}

void pd_client_exit(pd_client_context_t *conn)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_EXIT_REQ);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_EXIT_REQ_END);
    seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
}

void pd_client_bench_ipc(pd_client_context_t *conn, seL4_CPtr dummy_send_cap, seL4_CPtr dummy_recv_cap, bool cap_transfer)
{
    seL4_SetCapReceivePath(SEL4UTILS_CNODE_SLOT, /* Position of the cap to the CNODE */
                           dummy_recv_cap,       /* CPTR in this CSPACE */
                           /* This works coz we have a single level cnode with no guard.*/
                           seL4_WordBits); /* Depth i.e. how many bits of free_slot to interpret*/
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_BENCH_IPC_REQ);
    seL4_SetMR(PDMSGREG_BENCH_IPC_REQ_CAP_TRANSFER, cap_transfer);
    int num_caps = 0;
    if (cap_transfer)
    {
        seL4_SetCap(0, dummy_send_cap);
        num_caps = 1;
    }
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, num_caps, PDMSGREG_BENCH_IPC_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
}
