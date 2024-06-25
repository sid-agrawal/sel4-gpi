/**
 * @file mo_clientapi.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the mo client API from moclient.h.
 * @version 0.1
 * @date 2024-01-07
 *
 * @copyright Copyright (c) 2024
 *
 */

#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>

// Defined for utility printing macros
#define DEBUG_ID MO_DEBUG
#define SERVER_ID MOSERVS

static int mo_connect(seL4_CPtr server_ep_cap,
                      seL4_Word num_pages,
                      uintptr_t paddr,
                      mo_client_context_t *ret_conn)
{
    seL4_SetMR(MOMSGREG_FUNC, MO_FUNC_CONNECT_REQ);
    seL4_SetMR(MOMSGREG_CONNECT_REQ_NUM_PAGES, num_pages);
    seL4_SetMR(MOMSGREG_CONNECT_REQ_PADDR, paddr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, MOMSGREG_CONNECT_REQ_END);
    tag = seL4_Call(server_ep_cap, tag);

    ret_conn->badged_server_ep_cspath.capPtr = seL4_GetMR(MOMSGREG_CONNECT_ACK_SLOT);
    ret_conn->id = seL4_GetMR(MOMSGREG_CONNECT_ACK_ID);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int mo_component_client_connect(seL4_CPtr server_ep_cap,
                                seL4_Word num_pages,
                                mo_client_context_t *ret_conn)
{
    return mo_connect(server_ep_cap, num_pages, 0, ret_conn);
}

int mo_component_client_connect_paddr(seL4_CPtr server_ep_cap,
                                      seL4_Word num_pages,
                                      uintptr_t paddr,
                                      mo_client_context_t *ret_conn)
{
    return mo_connect(server_ep_cap, num_pages, paddr, ret_conn);
}

int mo_component_client_disconnect(mo_client_context_t *conn)
{
    seL4_SetMR(MOMSGREG_FUNC, MO_FUNC_DISCONNECT_REQ);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, MOMSGREG_CONNECT_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    
    return seL4_MessageInfo_ptr_get_label(&tag);
}