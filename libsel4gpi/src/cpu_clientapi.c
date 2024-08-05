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
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/error_handle.h>
#include <cpu_component_rpc.pb.h>

// Defined for utility printing macros
#define DEBUG_ID CPU_DEBUG
#define SERVER_ID CPUSERVC
#define DEFAULT_ERR CpuComponentError_UNKNOWN

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
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_alloc_tag,
    };

    CpuReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, server_ep_cap, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error)
    {
        ret_conn->ep = ret_msg.msg.alloc.slot;
        ret_conn->id = ret_msg.msg.alloc.id;
    }

    return error;
}

int cpu_component_client_disconnect(cpu_client_context_t *conn)
{
    OSDB_PRINTF("Sending disconnect request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_disconnect_tag,
    };

    CpuReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_config(cpu_client_context_t *cpu,
                      ads_client_context_t *ads,
                      pd_client_context_t *pd,
                      mo_client_context_t *ipc_buf_mo,
                      seL4_Word cnode_guard,
                      seL4_CPtr fault_ep_position,
                      void *ipc_buf_addr)
{
    OSDB_PRINTF("Sending config request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_config_tag,
        .msg.config = {
            .cnode_guard = cnode_guard,
            .fault_ep_cap = fault_ep_position,
            .ipc_buf_addr = (uint64_t)ipc_buf_addr,
        }};

    CpuReturnMessage ret_msg = {0};

    if (ipc_buf_mo && ipc_buf_mo->ep != 0)
    {
        seL4_CPtr caps[3] = {
            pd->ep,
            ads->ep,
            ipc_buf_mo->ep};
        error = sel4gpi_rpc_call(&rpc_env, cpu->ep, (void *)&msg,
                                 3, caps, (void *)&ret_msg);
    }
    else
    {
        seL4_CPtr caps[2] = {pd->ep, ads->ep};
        error = sel4gpi_rpc_call(&rpc_env, cpu->ep, (void *)&msg,
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
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_change_vspace_tag,
    };

    CpuReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             1, &ads_conn->ep, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_start(cpu_client_context_t *conn)
{
    OSDB_PRINTF("Sending start request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_start_tag,
    };

    CpuReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_elevate_privileges(cpu_client_context_t *conn)
{
    OSDB_PRINTF("Sending 'elevate privileges' request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_elevate_privilege_tag,
    };

    CpuReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, conn->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_set_tls_base(cpu_client_context_t *cpu, void *tls_base)
{
    OSDB_PRINTF("Sending 'set tls base' request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_tls_base_tag,
        .msg.tls_base = {
            .tls_base_addr = (uint64_t)tls_base,
        }};

    CpuReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, cpu->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_suspend(cpu_client_context_t *cpu)
{
    OSDB_PRINTF("Sending suspend request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_suspend_tag,
    };

    CpuReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, cpu->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_read_registers(cpu_client_context_t *cpu, seL4_UserContext *regs)
{
    ads_client_context_t vmr_rde = sel4gpi_get_bound_vmr_rde();
    mo_client_context_t msg_mo = {0};
    void *shared_msg_vaddr = sel4gpi_get_vmr(&vmr_rde, 1, NULL, SEL4UTILS_RES_TYPE_SHARED_FRAMES, MO_PAGE_BITS, &msg_mo);
    OSDB_PRINTF("Sending read registers request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_read_reg_tag,
    };

    CpuReturnMessage ret_msg = {0};

    seL4_CPtr caps[1] = {msg_mo.ep};

    printf("a\n");
    error = sel4gpi_rpc_call(&rpc_env, cpu->ep, (void *)&msg,
                             1, caps, (void *)&ret_msg);
    error |= ret_msg.errorCode;
    printf("b\n");
    if (!error)
    {
        memcpy(regs, shared_msg_vaddr, sizeof(seL4_UserContext));
    }

    int unmap_error = sel4gpi_destroy_vmr(&vmr_rde, shared_msg_vaddr, &msg_mo);
    SERVER_WARN_IF_COND(unmap_error, "Failed to destroy VMR and MO for message buffer\n");

    return error;
}
