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
#include <sel4gpi/gpi_rpc.h>
#include <ep_component_rpc.pb.h>

// Defined for utility printing macros
#define DEBUG_ID EP_DEBUG
#define SERVER_ID EPSERVC
#define DEFAULT_ERR EpComponentError_UNKNOWN

static sel4gpi_rpc_env_t rpc_env = {
    .request_desc = &EpMessage_msg,
    .reply_desc = &EpReturnMessage_msg,
};

int ep_component_client_connect(seL4_CPtr server_ep_cap, ep_client_context_t *ret_conn)
{
    OSDB_PRINTF("Sending connect request to endpoint component\n");

    int error = 0;

    EpMessage msg = {
        .magic = EP_RPC_MAGIC,
        .which_msg = EpMessage_alloc_tag,
    };

    EpReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, server_ep_cap, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error)
    {
        ret_conn->ep = ret_msg.msg.alloc.slot;
        ret_conn->raw_endpoint = ret_msg.msg.alloc.raw_ep_slot;
    }

    return error;
}

int ep_component_client_disconnect(ep_client_context_t *conn)
{
    OSDB_PRINTF("Sending connect request to endpoint component\n");

    int error = 0;

    EpMessage msg = {
        .magic = EP_RPC_MAGIC,
        .which_msg = EpMessage_disconnect_tag,
    };

    EpReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

static int get_raw_endpoint(ep_client_context_t *ep_conn, pd_client_context_t *target_PD, seL4_CPtr *raw_endpoint)
{
    OSDB_PRINTF("Sending 'get raw endpoint' request to endpoint component\n");

    int error = 0;

    EpMessage msg = {
        .magic = EP_RPC_MAGIC,
        .which_msg = EpMessage_get_tag,
        .msg.get = {
            .for_other_PD = target_PD != NULL,
        }};

    EpReturnMessage ret_msg = {0};

    if (target_PD)
    {
        error = sel4gpi_rpc_call(&rpc_env, ep_conn->ep, (void *)&msg,
                                 1, &target_PD->ep, (void *)&ret_msg);
    }
    else
    {
        error = sel4gpi_rpc_call(&rpc_env, ep_conn->ep, (void *)&msg,
                                 0, NULL, (void *)&ret_msg);
    }

    error |= ret_msg.errorCode;

    if (!error)
    {
        *raw_endpoint = ret_msg.msg.get.slot;
    }

    return error;
}

int ep_client_get_raw_endpoint(ep_client_context_t *ep_conn)
{
    seL4_CPtr raw_endpoint;
    int error = get_raw_endpoint(ep_conn, NULL, &raw_endpoint);

    if (!error)
    {
        ep_conn->raw_endpoint = raw_endpoint;
    }

err_goto:
    return error;
}

int ep_client_get_raw_endpoint_in_PD(pd_client_context_t *target_PD, ep_client_context_t *ep_conn, seL4_CPtr *ret_ep)
{
    int error = 0;
    seL4_CPtr raw_endpoint;
    error = get_raw_endpoint(ep_conn, target_PD, &raw_endpoint);

    if (!error)
    {
        *ret_ep = raw_endpoint;
    }

    return error;
}

int ep_client_forge(seL4_CPtr server_ep_cap, seL4_CPtr ep_to_forge, ep_client_context_t *ret_conn)
{
    OSDB_PRINTF("Sending 'get raw endpoint' request to endpoint component\n");

    int error = 0;

    EpMessage msg = {
        .magic = EP_RPC_MAGIC,
        .which_msg = EpMessage_forge_tag,
    };

    EpReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, server_ep_cap, (void *)&msg,
                             1, &ep_to_forge, (void *)&ret_msg);

    error |= ret_msg.errorCode;

    if (!error)
    {
        ret_conn->ep = ret_msg.msg.alloc.slot;
        ret_conn->raw_endpoint = ep_to_forge;
    }

    return error;
}
