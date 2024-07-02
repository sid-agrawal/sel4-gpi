/**
 * @file cpu_clientapi.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the cpu client API from cpu_client.h.
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <sel4gpi/cpu_clientapi.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/gpi_rpc.h>
#include <cpu_component_rpc.pb.h>

// Defined for utility printing macros
#define DEBUG_ID CPU_DEBUG
#define SERVER_ID CPUSERVC

static sel4gpi_rpc_env_t rpc_env = {
    .request_desc = &CpuMessage_msg,
    .reply_desc = &CpuReturnMessage_msg,
};

int cpu_component_client_connect(seL4_CPtr server_ep_cap,
                                 cpu_client_context_t *ret_conn)
{
    OSDB_PRINTF("Sending connect request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .which_msg = CpuMessage_alloc_tag,
    };

    CpuReturnMessage ret_msg;

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

int cpu_client_config(cpu_client_context_t *cpu,
                      ads_client_context_t *ads,
                      pd_client_context_t *pd,
                      mo_client_context_t *ipc_buf_mo,
                      seL4_Word cnode_guard,
                      seL4_CPtr fault_ep_position,
                      seL4_Word ipc_buf_addr)
{
    OSDB_PRINTF("Sending config request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .which_msg = CpuMessage_config_tag,
        .msg.config = {
            .cnode_guard = cnode_guard,
            .fault_ep_cap = fault_ep_position,
            .ipc_buf_addr = ipc_buf_addr,
        }};

    CpuReturnMessage ret_msg;

    if (ipc_buf_mo && ipc_buf_mo->badged_server_ep_cspath.capPtr != 0)
    {
        seL4_CPtr caps[3] = {
            pd->badged_server_ep_cspath.capPtr,
            ads->badged_server_ep_cspath.capPtr,
            ipc_buf_mo->badged_server_ep_cspath.capPtr};
        error = sel4gpi_rpc_call(&rpc_env, cpu->badged_server_ep_cspath.capPtr, (void *)&msg,
                                 3, caps, (void *)&ret_msg);
    }
    else
    {
        seL4_CPtr caps[2] = {pd->badged_server_ep_cspath.capPtr, ads->badged_server_ep_cspath.capPtr};
        error = sel4gpi_rpc_call(&rpc_env, cpu->badged_server_ep_cspath.capPtr, (void *)&msg,
                                 2, caps, (void *)&ret_msg);
    }

    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_change_vspace(cpu_client_context_t *conn,
                             ads_client_context_t *ads_conn)
{
    OSDB_PRINTF("Sending 'change vspace' request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .which_msg = CpuMessage_change_vspace_tag,
    };

    CpuReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->badged_server_ep_cspath.capPtr, (void *)&msg,
                             1, &ads_conn->badged_server_ep_cspath.capPtr, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_start(cpu_client_context_t *conn)
{
    OSDB_PRINTF("Sending start request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .which_msg = CpuMessage_start_tag,
    };

    CpuReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->badged_server_ep_cspath.capPtr, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_elevate_privileges(cpu_client_context_t *conn)
{
    OSDB_PRINTF("Sending 'elevate privileges' request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .which_msg = CpuMessage_elevate_privilege_tag,
    };

    CpuReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->badged_server_ep_cspath.capPtr, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_set_tls_base(cpu_client_context_t *cpu, void *tls_base)
{
    OSDB_PRINTF("Sending 'set tls base' request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .which_msg = CpuMessage_tls_base_tag,
        .msg.tls_base = {
            .tls_base_addr = tls_base,
        }};

    CpuReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, cpu->badged_server_ep_cspath.capPtr, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_suspend(cpu_client_context_t *cpu)
{
    OSDB_PRINTF("Sending suspend request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .which_msg = CpuMessage_suspend_tag,
    };

    CpuReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, cpu->badged_server_ep_cspath.capPtr, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}
