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
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/gpi_rpc.h>
#include <resspc_component_rpc.pb.h>

// Defined for utility printing macros
#define DEBUG_ID RESSPC_DEBUG
#define SERVER_ID RESSPC_SERVC

static sel4gpi_rpc_env_t rpc_env = {
    .request_desc = &ResSpcMessage_msg,
    .reply_desc = &ResSpcReturnMessage_msg,
};

int resspc_client_connect(seL4_CPtr server_ep,
                          char *resource_type,
                          ep_client_context_t *resource_server_ep,
                          seL4_CPtr client_id,
                          resspc_client_context_t *ret_conn)
{
    OSDB_PRINTF("Sending connect request to ResSpc component\n");

    int error = 0;

    ResSpcMessage msg = {
        .magic = RESSPC_RPC_MAGIC,
        .which_msg = ResSpcMessage_alloc_tag,
        .msg.alloc = {
            .client_id = client_id,
        },
    };

    assert(strlen(resource_type) < sizeof(msg.msg.alloc.type_name));
    strncpy(msg.msg.alloc.type_name, resource_type, sizeof(msg.msg.alloc.type_name));

    ResSpcReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, server_ep, (void *)&msg,
                             1, &resource_server_ep->ep, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error)
    {
        ret_conn->ep = ret_msg.msg.alloc.slot;
        ret_conn->id = ret_msg.msg.alloc.id;
        ret_conn->resource_type = ret_msg.msg.alloc.type_code;
    }

    return error;
}

int resspc_client_map_space(resspc_client_context_t *conn,
                            gpi_space_id_t space_id)
{
    OSDB_PRINTF("Sending 'map space' request to ResSpc component\n");

    int error = 0;

    ResSpcMessage msg = {
        .magic = RESSPC_RPC_MAGIC,
        .which_msg = ResSpcMessage_map_tag,
        .msg.map = {
            .space_id = space_id,
        },
    };

    ResSpcReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int resspc_client_create_resource(resspc_client_context_t *conn,
                                  gpi_obj_id_t resource_id)
{
    OSDB_PRINTF("Sending 'create resource' request to ResSpc component\n");

    int error = 0;

    ResSpcMessage msg = {
        .magic = RESSPC_RPC_MAGIC,
        .which_msg = ResSpcMessage_create_resource_tag,
        .msg.create_resource = {
            .resource_id = resource_id,
        },
    };

    ResSpcReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int resspc_client_delete_resource(resspc_client_context_t *conn,
                                  gpi_obj_id_t resource_id)
{
    OSDB_PRINTF("Sending 'delete resource' request to ResSpc component\n");

    int error = 0;

    ResSpcMessage msg = {
        .magic = RESSPC_RPC_MAGIC,
        .which_msg = ResSpcMessage_delete_resource_tag,
        .msg.delete_resource = {
            .resource_id = resource_id,
        },
    };

    ResSpcReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int resspc_client_revoke_resource(resspc_client_context_t *conn,
                                  gpi_obj_id_t resource_id,
                                  gpi_obj_id_t target_pd_id)
{
    OSDB_PRINTF("Sending 'delete resource' request to ResSpc component\n");

    int error = 0;

    ResSpcMessage msg = {
        .magic = RESSPC_RPC_MAGIC,
        .which_msg = ResSpcMessage_revoke_resource_tag,
        .msg.revoke_resource = {
            .resource_id = resource_id,
            .target_pd_id = target_pd_id,
        },
    };

    ResSpcReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int resspc_client_destroy(resspc_client_context_t *conn)
{
    OSDB_PRINTF("Sending 'destroy' request to ResSpc component\n");

    int error = 0;

    ResSpcMessage msg = {
        .magic = RESSPC_RPC_MAGIC,
        .which_msg = ResSpcMessage_destroy_tag,
    };

    ResSpcReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}