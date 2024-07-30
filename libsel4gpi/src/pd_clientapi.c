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
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/gpi_rpc.h>

/**
 * @file Client-side calls for interacting with the PD component
 */

// Defined for utility printing macros
#define DEBUG_ID PD_DEBUG
#define SERVER_ID PDSERVC

static sel4gpi_rpc_env_t rpc_env = {
    .request_desc = &PdMessage_msg,
    .reply_desc = &PdReturnMessage_msg,
};

int pd_component_client_connect(seL4_CPtr server_ep,
                                mo_client_context_t *osm_data_mo,
                                pd_client_context_t *ret_conn)
{
    OSDB_PRINTF("Sending connect request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_alloc_tag,
        .msg = {0},
    };

    PdReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, server_ep, (void *)&msg,
                             1, &osm_data_mo->ep, (void *)&ret_msg);

    error |= ret_msg.errorCode;

    if (!error)
    {
        ret_conn->ep = ret_msg.msg.alloc.slot;
        ret_conn->id = ret_msg.msg.alloc.id;
    }

    return error;
}

int pd_client_terminate(pd_client_context_t *conn)
{
    OSDB_PRINTF("Sending terminate request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_terminate_tag,
    };

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int pd_client_dump(pd_client_context_t *conn,
                   char *buf,
                   size_t buf_sz)
{
    OSDB_PRINTF("Sending dump request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_dump_tag,
        // (XXX) Arya: not currently sending buffer, just printing state in the RT
    };

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

static int send_cap_req(pd_client_context_t *conn, seL4_CPtr cap_to_send, seL4_CPtr *slot, bool is_core)
{
    OSDB_PRINTF("Sending 'send cap' request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_send_cap_tag,
        .msg.send_cap = {
            .is_core_cap = is_core,
        }};

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             1, &cap_to_send, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error && slot)
    {
        *slot = ret_msg.msg.send_cap.slot;
    }

    return error;
}

// convenient wrapper for send_cap_req
int pd_client_send_cap(pd_client_context_t *conn,
                       seL4_CPtr cap_to_send,
                       seL4_CPtr *slot)
{
    return send_cap_req(conn, cap_to_send, slot, false);
}

// convenient wrapper for send_cap_req
int pd_client_send_core_cap(pd_client_context_t *conn,
                            seL4_CPtr cap_to_send,
                            seL4_CPtr *slot)
{
    return send_cap_req(conn, cap_to_send, slot, true);
}

int pd_client_next_slot(pd_client_context_t *conn,
                        seL4_CPtr *slot)
{
    OSDB_PRINT_VERBOSE("Sending 'next slot' request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_next_slot_tag,
    };

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error)
    {
        *slot = ret_msg.msg.next_slot.slot;
    }

    return error;
}

int pd_client_free_slot(pd_client_context_t *conn,
                        seL4_CPtr slot)
{
    OSDB_PRINT_VERBOSE("Sending 'free slot' request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_free_slot_tag,
    };

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int pd_client_clear_slot(pd_client_context_t *conn,
                         seL4_CPtr slot)
{
    OSDB_PRINT_VERBOSE("Sending 'clear slot' request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_clear_slot_tag,
        .msg.clear_slot = {
            .slot = slot,
        }};

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int pd_client_share_rde(pd_client_context_t *target_pd,
                        gpi_cap_t cap_type,
                        gpi_space_id_t space_id)
{
    OSDB_PRINTF("Sending 'share RDE' request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_share_rde_tag,
        .msg.share_rde = {
            .res_type = cap_type,
            .space_id = space_id,
        }};

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, target_pd->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int pd_client_give_resource(pd_client_context_t *conn,
                            gpi_space_id_t res_space_id,
                            gpi_obj_id_t recipient_id,
                            gpi_obj_id_t resource_id,
                            seL4_CPtr *dest)
{
    OSDB_PRINT_VERBOSE("Sending 'give resource' request to PD component: PD (%u), Space (%u), Object (%u)\n",
                       recipient_id, res_space_id, resource_id);

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_give_resource_tag,
        .msg.give_resource = {
            .space_id = res_space_id,
            .pd_id = recipient_id,
            .object_id = resource_id,
        }};

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error)
    {
        *dest = ret_msg.msg.give_resource.slot;
    }

    return error;
}

#if TRACK_MAP_RELATIONS
int pd_client_map_resource(pd_client_context_t *conn,
                           gpi_obj_id_t src_res_id,
                           gpi_obj_id_t dest_res_id)
{
    OSDB_PRINTF("Sending 'map resource' request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_map_resource_tag,
        .msg.map_resource = {
            .src_resource = src_res_id,
            .dest_resource = dest_res_id,
        }};

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}
#endif

int pd_client_get_work(pd_client_context_t *conn, PdWorkReturnMessage *work)
{
    OSDB_PRINTF("Sending 'get work' request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_get_work_tag,
    };

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error)
    {
        *work = ret_msg.msg.work;
    }

    return error;
}

int pd_client_send_subgraph(pd_client_context_t *conn, mo_client_context_t *mo_conn, bool has_data, int n_requests)
{
    OSDB_PRINTF("Sending 'send subgraph' request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_send_subgraph_tag,
        .msg.send_subgraph = {
            .has_data = has_data,
            .n_requests = n_requests,
        }};

    PdReturnMessage ret_msg;

    if (has_data)
    {
        error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                                 1, &mo_conn->ep, (void *)&ret_msg);
    }
    else
    {
        error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                                 0, NULL, (void *)&ret_msg);
    }

    error |= ret_msg.errorCode;

    return error;
}

int pd_client_finish_work(pd_client_context_t *conn, int n_requests)
{
    OSDB_PRINTF("Sending 'finish work' request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_finish_work_tag,
        .msg.finish_work = {
            .n_requests = n_requests,
        }};

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);

    error |= ret_msg.errorCode;

    return error;
}

void pd_client_exit(pd_client_context_t *conn, int code)
{
    OSDB_PRINTF("Sending 'exit' message to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_exit_tag,
        .msg.exit = {
            .exit_code = code,
        }};

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);

    // This message should not return
    ZF_LOGF("Failed to send 'exit' message to PD component\n");
}

int pd_client_remove_rde(pd_client_context_t *conn, gpi_cap_t type, gpi_space_id_t space_id)
{
    OSDB_PRINTF("Sending 'remove RDE' request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_remove_rde_tag,
        .msg.remove_rde = {
            .res_type = type,
            .space_id = space_id,
        }};

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int pd_client_bench_ipc(pd_client_context_t *conn, seL4_CPtr dummy_send_cap, bool cap_transfer)
{
    OSDB_PRINTF("Sending 'bench IPC' request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_bench_ipc_tag,
        .msg.bench_ipc = {
            .do_cap_transfer = cap_transfer,
        }};

    PdReturnMessage ret_msg;

    if (cap_transfer)
    {
        error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                                 1, &dummy_send_cap, (void *)&ret_msg);
    }
    else
    {
        error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                                 0, NULL, (void *)&ret_msg);
    }

    error |= ret_msg.errorCode;

    return error;
}

int pd_client_runtime_setup(pd_client_context_t *target_pd,
                            ads_client_context_t *target_ads,
                            cpu_client_context_t *target_cpu,
                            void *stack_pos,
                            int argc,
                            seL4_Word *args,
                            void *entry_point,
                            void *ipc_buf_addr,
                            void *osm_data_in_PD)
{
    OSDB_PRINTF("Sending 'runtime setup' request to PD component\n");

    int error = 0;

    GOTO_IF_COND(argc > PD_MAX_ARGC, "Cannot setup PD with more than %d arguments\n", PD_MAX_ARGC);

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_setup_tag,
        .msg.setup = {
            .entry_point = (uint64_t)entry_point,
            .ipc_buf_addr = (uint64_t)ipc_buf_addr,
            .stack_top = (uint64_t)stack_pos,
            .osm_data_addr = (uint64_t)osm_data_in_PD,
            .args_count = argc,
        }};
    memcpy(msg.msg.setup.args, args, sizeof(seL4_Word) * argc);

    seL4_CPtr caps[2] = {target_ads->ep, target_cpu->ep};

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, target_pd->ep, (void *)&msg,
                             2, caps, (void *)&ret_msg);
    error |= ret_msg.errorCode;

err_goto:
    return error;
}

int pd_client_share_resource_by_type(pd_client_context_t *src_pd, pd_client_context_t *dest_pd, gpi_cap_t res_type)
{
    OSDB_PRINTF("Sending 'share resource by type' request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_share_res_type_tag,
        .msg.share_res_type = {
            .res_type = res_type,
        }};

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, src_pd->ep, (void *)&msg,
                             1, &dest_pd->ep, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

#ifdef CONFIG_DEBUG_BUILD
int pd_client_set_name(pd_client_context_t *conn, char *name)
{
    OSDB_PRINTF("Sending 'set name' request to PD component\n");

    int error = 0;

    PdMessage msg = {
        .magic = PD_RPC_MAGIC,
        .which_msg = PdMessage_set_name_tag,
    };

    assert(strlen(name) < sizeof(msg.msg.set_name.pd_name));
    strncpy(msg.msg.set_name.pd_name, name, sizeof(msg.msg.set_name.pd_name));

    PdReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}
#endif