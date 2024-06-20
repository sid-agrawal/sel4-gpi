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
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/error_handle.h>

/**
 * @file Client-side calls for interacting with the PD component
 */

// Defined for utility printing macros
#define DEBUG_ID PD_DEBUG
#define SERVER_ID PDSERVC

int pd_component_client_connect(seL4_CPtr server_ep_cap,
                                mo_client_context_t *osm_data_mo,
                                pd_client_context_t *ret_conn)
{
    /* Set request type */
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_CONNECT_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, PDMSGREG_CONNECT_REQ_END);
    seL4_SetCap(0, osm_data_mo->badged_server_ep_cspath.capPtr);

    tag = seL4_Call(server_ep_cap, tag);

    ret_conn->badged_server_ep_cspath.capPtr = seL4_GetMR(PDMSGREG_CONNECT_ACK_SLOT);
    ret_conn->id = seL4_GetMR(PDMSGREG_CONNECT_ACK_ID);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int pd_client_disconnect(pd_client_context_t *conn)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_DISCONNECT_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_DISCONNECT_REQ_END);

    OSDB_PRINTF("Sending disconnect request for PD via EP: %lu.\n",
                conn->badged_server_ep_cspath.capPtr);

    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_get_label(tag);
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

    OSDB_PRINTF("Sending dump RR request to PD via EP: %lu.\n",
                conn->badged_server_ep_cspath.capPtr);
    // (XXX) Linh: for some reason, this doesn't block if we try to dump a test PD's state and causes issues
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    return seL4_MessageInfo_ptr_get_label(&tag);
}

static int send_cap_req(pd_client_context_t *conn, seL4_CPtr cap_to_send, seL4_Word *slot, bool is_core)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SENDCAP_REQ);
    seL4_SetMR(PDMSGREG_SEND_CAP_REQ_IS_CORE, (seL4_Word)is_core);
    seL4_SetCap(0, cap_to_send);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  PDMSGREG_SEND_CAP_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    if (slot)
    {
        *slot = seL4_GetMR(PDMSGREG_SEND_CAP_PD_SLOT);
    }

    return seL4_MessageInfo_ptr_get_label(&tag);
}

// convenient wrapper for send_cap_req
int pd_client_send_cap(pd_client_context_t *conn,
                       seL4_CPtr cap_to_send,
                       seL4_Word *slot)
{
    return send_cap_req(conn, cap_to_send, slot, false);
}

// convenient wrapper for send_cap_req
int pd_client_send_core_cap(pd_client_context_t *conn,
                            seL4_CPtr cap_to_send,
                            seL4_Word *slot)
{
    return send_cap_req(conn, cap_to_send, slot, true);
}

int pd_client_next_slot(pd_client_context_t *conn,
                        seL4_Word *slot)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_NEXT_SLOT_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_NEXT_SLOT_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    *slot = seL4_GetMR(PDMSGREG_NEXT_SLOT_PD_SLOT);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int pd_client_free_slot(pd_client_context_t *conn,
                        seL4_CPtr slot)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_FREE_SLOT_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_FREE_SLOT_REQ_END);
    seL4_SetMR(PDMSGREG_FREE_SLOT_REQ_SLOT, slot);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int pd_client_clear_slot(pd_client_context_t *conn,
                         seL4_CPtr slot)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_CLEAR_SLOT_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_CLEAR_SLOT_REQ_END);
    seL4_SetMR(PDMSGREG_CLEAR_SLOT_REQ_SLOT, slot);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int pd_client_share_rde(pd_client_context_t *target_pd,
                        gpi_cap_t cap_type,
                        uint64_t space_id)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SHARE_RDE_REQ);
    seL4_SetMR(PDMSGREG_SHARE_RDE_REQ_TYPE, cap_type);
    seL4_SetMR(PDMSGREG_SHARE_RDE_REQ_SPACE_ID, space_id);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_SHARE_RDE_REQ_END);
    tag = seL4_Call(target_pd->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int pd_client_give_resource(pd_client_context_t *conn,
                            seL4_Word res_space_id,
                            seL4_Word recipient_id,
                            seL4_Word resource_id,
                            seL4_CPtr *dest)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_GIVE_RES_REQ);
    seL4_SetMR(PDMSGREG_GIVE_RES_REQ_SPACE_ID, res_space_id);
    seL4_SetMR(PDMSGREG_GIVE_RES_REQ_CLIENT_ID, recipient_id);
    seL4_SetMR(PDMSGREG_GIVE_RES_REQ_RES_ID, resource_id);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_GIVE_RES_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    *dest = seL4_GetMR(PDMSGREG_GIVE_RES_ACK_DEST);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

#if TRACK_MAP_RELATIONS
int pd_client_map_resource(pd_client_context_t *conn,
                           seL4_Word src_res_id,
                           seL4_Word dest_res_id)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_MAP_RES_REQ);
    seL4_SetMR(PDMSGREG_MAP_RES_REQ_SRC_ID, src_res_id);
    seL4_SetMR(PDMSGREG_MAP_RES_REQ_DEST_ID, dest_res_id);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_MAP_RES_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
}
#endif

void pd_client_exit(pd_client_context_t *conn)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_EXIT_REQ);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_EXIT_REQ_END);
    seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
}

int pd_client_remove_rde(pd_client_context_t *conn, gpi_cap_t type, uint64_t space_id)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_REMOVE_RDE_REQ);
    seL4_SetMR(PDMSGREG_REMOVE_RDE_REQ_TYPE, type);
    seL4_SetMR(PDMSGREG_REMOVE_RDE_REQ_SPACE_ID, space_id);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  PDMSGREG_REMOVE_RDE_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
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

int pd_client_runtime_setup(pd_client_context_t *target_pd,
                            ads_client_context_t *target_ads,
                            cpu_client_context_t *target_cpu,
                            void *stack_pos,
                            int argc,
                            seL4_Word *args,
                            void *entry_point,
                            void *ipc_buf_addr,
                            void *osm_data_in_PD,
                            pd_setup_type_t setup_type)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SETUP_REQ);
    seL4_SetCap(0, target_ads->badged_server_ep_cspath.capPtr);
    seL4_SetCap(1, target_cpu->badged_server_ep_cspath.capPtr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 2,
                                                  PDMSGREG_SETUP_REQ_END);

    seL4_SetMR(PDMSGREG_SETUP_REQ_ARGC, argc);

    OSDB_PRINTF("Setting up process with %d args: [", argc);
    for (int i = 0; i < argc; i++)
    {
        OSDB_PRINTF_2("%ld, ", args[i]);

        switch (i)
        {
        case 0:
            seL4_SetMR(PDMSGREG_SETUP_REQ_ARG0, args[i]);
            break;
        case 1:
            seL4_SetMR(PDMSGREG_SETUP_REQ_ARG1, args[i]);
            break;
        case 2:
            seL4_SetMR(PDMSGREG_SETUP_REQ_ARG2, args[i]);
            break;
        case 3:
            seL4_SetMR(PDMSGREG_SETUP_REQ_ARG3, args[i]);
            break;
        }
    }
    OSDB_PRINTF_2("]\n");

    seL4_SetMR(PDMSGREG_SETUP_REQ_STACK, (seL4_Word)stack_pos);
    seL4_SetMR(PDMSGREG_SETUP_REQ_ENTRY_POINT, (seL4_Word)entry_point);
    seL4_SetMR(PDMSGREG_SETUP_REQ_IPC_BUF, (seL4_Word)ipc_buf_addr);
    seL4_SetMR(PDMSGREG_SETUP_REQ_OSM_DATA, (seL4_Word)osm_data_in_PD);
    seL4_SetMR(PDMSGREG_SETUP_REQ_TYPE, setup_type);

    tag = seL4_Call(target_pd->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_get_label(tag);
}

int pd_client_share_resource_by_type(pd_client_context_t *src_pd, pd_client_context_t *dest_pd, gpi_cap_t res_type)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SHARE_RES_TYPE_REQ);
    seL4_SetMR(PDMSGREG_SHARE_RES_TYPE_REQ_TYPE, res_type);
    seL4_SetCap(0, dest_pd->badged_server_ep_cspath.capPtr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, PDMSGREG_SHARE_RES_TYPE_REQ_END);
    tag = seL4_Call(src_pd->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_get_label(tag);
}
