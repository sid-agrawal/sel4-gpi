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
                          char *resource_type,
                          seL4_CPtr resource_server_ep,
                          seL4_CPtr client_id,
                          resspc_client_context_t *ret_conn)
{
    OSDB_PRINTF("Creating resource space\n");

    // Allocate an MO to send the resource type name
    ads_client_context_t ads_conn = {0};
    ads_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_space_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_VMR);

    mo_client_context_t mo_conn;
    void *mo_vaddr = sel4gpi_get_vmr(&ads_conn, 1, NULL, SEL4UTILS_RES_TYPE_SHARED_FRAMES, &mo_conn);

    if (mo_vaddr == NULL) {
        return 1;
    }

    strcpy(mo_vaddr, resource_type);

    // Send the message
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 2, RESSPCMSGREG_CONNECT_REQ_END);
    seL4_SetMR(RESSPCMSGREG_FUNC, RESSPC_FUNC_CONNECT_REQ);
    seL4_SetMR(RESSPCMSGREG_CONNECT_REQ_CLIENT_ID, client_id);
    seL4_SetCap(0, resource_server_ep);
    seL4_SetCap(1, mo_conn.badged_server_ep_cspath.capPtr);

    tag = seL4_Call(server_ep, tag);

    // Setup the return context
    ret_conn->badged_server_ep_cspath.capPtr = seL4_GetMR(RESSPCMSGREG_CONNECT_ACK_SLOT);
    ret_conn->id = seL4_GetMR(RESSPCMSGREG_CONNECT_ACK_ID);
    ret_conn->resource_type = (gpi_cap_t) seL4_GetMR(RESSPCMSGREG_CONNECT_ACK_TYPE);

    // Free the MO
    sel4gpi_destroy_vmr(&ads_conn, mo_vaddr, &mo_conn);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int resspc_client_map_space(resspc_client_context_t *conn,
                            seL4_Word space_id)
{
    OSDB_PRINTF("Mapping space\n");

    seL4_SetMR(RESSPCMSGREG_FUNC, RESSPC_FUNC_MAP_SPACE_REQ);
    seL4_SetMR(RESSPCMSGREG_MAP_SPACE_REQ_SPACE_ID, space_id);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  RESSPCMSGREG_MAP_SPACE_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int resspc_client_create_resource(resspc_client_context_t *conn,
                                  seL4_Word resource_id)
{
    OSDB_PRINTF("Creating resource\n");

    seL4_SetMR(RESSPCMSGREG_FUNC, RESSPC_FUNC_CREATE_RES_REQ);
    seL4_SetMR(RESSPCMSGREG_CREATE_RES_REQ_RES_ID, resource_id);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  RESSPCMSGREG_CREATE_RES_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
}