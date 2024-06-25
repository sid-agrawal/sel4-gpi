/**
 * @file endpoint_clientapi.c
 * @author Linh Pham (phamhlinh01@gmail.com)
 * @brief client facing functions for tracked endpoint operations
 * @version 0.1
 * @date 2024-06-25
 *
 * @copyright Copyright (c) 2024
 *
 */
#include <sel4gpi/endpoint_clientapi.h>
#include <sel4gpi/endpoint_component.h>

// Defined for utility printing macros
#define DEBUG_ID EP_DEBUG
#define SERVER_ID EPSERVC

int ep_component_client_connect(seL4_CPtr server_ep_cap, ep_client_context_t *ret_conn)
{
    seL4_SetMR(EPMSGREG_FUNC, EP_FUNC_CONNECT_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, EPMSGREG_CONNECT_REQ_END);
    tag = seL4_Call(server_ep_cap, tag);

    ret_conn->badged_server_ep_cspath.capPtr = seL4_GetMR(EPMSGREG_CONNECT_ACK_SLOT);
    ret_conn->raw_endpoint = seL4_GetMR(EPMSGREG_CONNECT_ACK_RAW_EP);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

static int get_raw_endpoint(ep_client_context_t *ep_conn, int extraCaps)
{
    seL4_SetMR(EPMSGREG_FUNC, EP_FUNC_GET_RAW_ENDPOINT_REQ);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, extraCaps, EPMSGREG_GET_RAW_ENDPOINT_REQ_END);
    tag = seL4_Call(ep_conn->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int ep_client_get_raw_endpoint(ep_client_context_t *ep_conn)
{
    int error = get_raw_endpoint(ep_conn, 0);
    ep_conn->raw_endpoint = seL4_GetMR(EPMSGREG_GET_RAW_ENDPOINT_ACK_SLOT);
    return error;
}

int ep_client_get_raw_endpoint_in_PD(pd_client_context_t *target_PD, ep_client_context_t *ep_conn, seL4_CPtr *ret_ep)
{
    int error = 0;
    seL4_SetCap(0, target_PD->badged_server_ep_cspath.capPtr);
    error = get_raw_endpoint(ep_conn, 1);
    if (ret_ep)
    {
        *ret_ep = seL4_GetMR(EPMSGREG_GET_RAW_ENDPOINT_ACK_SLOT);
    }

    return error;
}

int ep_client_forge(seL4_CPtr server_ep_cap, seL4_CPtr ep_to_forge, ep_client_context_t *ret_conn)
{
    seL4_SetMR(EPMSGREG_FUNC, EP_FUNC_FORGE_REQ);
    seL4_SetCap(0, ep_to_forge);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, EPMSGREG_FORGE_REQ_END);
    tag = seL4_Call(server_ep_cap, tag);

    ret_conn->badged_server_ep_cspath.capPtr = seL4_GetMR(EPMSGREG_FORGE_ACK_SLOT);
    ret_conn->raw_endpoint = ep_to_forge;

    return seL4_MessageInfo_ptr_get_label(&tag);
}
