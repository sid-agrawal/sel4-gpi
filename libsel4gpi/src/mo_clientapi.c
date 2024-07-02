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
#include <sel4gpi/gpi_rpc.h>
#include <mo_component_rpc.pb.h>

// Defined for utility printing macros
#define DEBUG_ID MO_DEBUG
#define SERVER_ID MOSERVS

static sel4gpi_rpc_env_t rpc_env = {
    .request_desc = &MoMessage_msg,
    .reply_desc = &MoReturnMessage_msg,
};

static int mo_connect(seL4_CPtr server_ep_cap,
                      seL4_Word num_pages,
                      size_t page_bits,
                      uintptr_t paddr,
                      mo_client_context_t *ret_conn)
{
    OSDB_PRINTF("Sending connect request to MO component\n");

    int error = 0;

    MoMessage msg = {
        .which_msg = MoMessage_alloc_tag,
        .msg.alloc = {
            .num_pages = num_pages,
            .page_bits = page_bits,
            .phys_addr = paddr,
        }
    };

    MoReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, server_ep_cap, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error)
    {
        ret_conn->badged_server_ep_cspath.capPtr = ret_msg.msg.alloc.slot;
        ret_conn->id = ret_msg.msg.alloc.id;
    }

    return error;
}

int mo_component_client_connect(seL4_CPtr server_ep_cap,
                                seL4_Word num_pages,
                                size_t page_bits,
                                mo_client_context_t *ret_conn)
{
    return mo_connect(server_ep_cap, num_pages, page_bits, 0, ret_conn);
}

int mo_component_client_connect_paddr(seL4_CPtr server_ep_cap,
                                      seL4_Word num_pages,
                                      size_t page_bits,
                                      uintptr_t paddr,
                                      mo_client_context_t *ret_conn)
{
    return mo_connect(server_ep_cap, num_pages, page_bits, paddr, ret_conn);
}

int mo_component_client_disconnect(mo_client_context_t *conn)
{
    OSDB_PRINTF("Sending disconnect request to MO component\n");

    int error = 0;

    MoMessage msg = {
        .which_msg = MoMessage_disconnect_tag,
    };

    MoReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->badged_server_ep_cspath.capPtr, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}