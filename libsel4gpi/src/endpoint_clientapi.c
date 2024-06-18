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

int ep_client_get_raw_endpoint(ep_client_context_t *ep_conn)
{
    seL4_SetMR(EPMSGREG_FUNC, EP_FUNC_GET_RAW_ENDPOINT_REQ);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, EPMSGREG_GET_RAW_ENDPOINT_REQ_END);
    tag = seL4_Call(ep_conn->badged_server_ep_cspath.capPtr, tag);

    ep_conn->raw_endpoint = seL4_GetMR(EPMSGREG_GET_RAW_ENDPOINT_ACK_SLOT);

    return seL4_MessageInfo_ptr_get_label(&tag);
}
