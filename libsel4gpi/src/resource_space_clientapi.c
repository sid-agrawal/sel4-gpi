/**
 * @file resource_space_clientapi.c
 * @author Arya Stevinson (arya.stevinson@gmail.com)
 * @brief Implements the resource space CLIENT API
 * @version 0.1
 * @date 2024-05-15
 *
 * @copyright Copyright (c) 2024
 */

#include <vka/capops.h>

#include <sel4gpi/resource_space_clientapi.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/gpi_client.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/error_handle.h>

// Defined for utility printing macros
#define DEBUG_ID RESSPC_DEBUG
#define SERVER_ID RESSPC_SERVC

int resspc_client_connect(seL4_CPtr server_ep,
                          seL4_CPtr free_slot,
                          gpi_cap_t resource_type,
                          seL4_CPtr resource_server_ep,
                          seL4_CPtr client_id,
                          resspc_client_context_t *ret_conn)
{
    // (XXX) Arya: Eventually we can replace all of this "free slot" business with the server allocating the next slot
    seL4_SetCapReceivePath(SEL4UTILS_CNODE_SLOT,
                           free_slot,
                           seL4_WordBits);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, RESSPCMSGREG_CONNECT_REQ_END);
    seL4_SetMR(RESSPCMSGREG_FUNC, RESSPC_FUNC_CONNECT_REQ);
    seL4_SetMR(RESSPCMSGREG_CONNECT_REQ_TYPE, resource_type);
    seL4_SetMR(RESSPCMSGREG_CONNECT_REQ_CLIENT_ID, client_id);
    seL4_SetCap(0, resource_server_ep);

    tag = seL4_Call(server_ep, tag);

    ret_conn->badged_server_ep_cspath.capPtr = free_slot;
    ret_conn->id = seL4_GetMR(RESSPCMSGREG_CONNECT_ACK_ID);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int resspc_client_create_resource(resspc_client_context_t *conn,
                                  seL4_Word resource_id)
{
    seL4_SetMR(RESSPCMSGREG_FUNC, RESSPC_FUNC_CREATE_RES_REQ);
    seL4_SetMR(RESSPCMSGREG_CREATE_RES_REQ_RES_ID, resource_id);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  RESSPCMSGREG_CREATE_RES_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
}