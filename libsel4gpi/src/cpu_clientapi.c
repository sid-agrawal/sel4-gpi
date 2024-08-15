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
#include <sel4debug/register_dump.h>

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
                      void *ipc_buf_addr,
                      int prio)
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
            .prio = prio,
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

int cpu_client_write_registers(cpu_client_context_t *cpu, seL4_UserContext *regs, size_t num_reg, bool resume)
{
    OSDB_PRINTF("Sending 'write registers' request to CPU component\n");
    int error = 0;

    CpuMessage msg = {
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_write_reg_tag,
        .msg.write_reg = {
            .resume = resume,
            .reg_buf_count = num_reg,
        },
    };

    memcpy(msg.msg.write_reg.reg_buf, (void *)regs, sizeof(seL4_Word) * num_reg);

    CpuReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, cpu->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_read_registers(cpu_client_context_t *cpu, seL4_UserContext *regs)
{
    OSDB_PRINTF("Sending read registers request to CPU component\n");
    int error = 0;

    CpuMessage msg = {
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_read_reg_tag,
    };

    CpuReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, cpu->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);

    if (!error)
    {
        memcpy(regs, ret_msg.msg.read_reg.reg_buf, sizeof(seL4_Word) * ret_msg.msg.read_reg.reg_buf_count);
    }

    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_write_vcpu_reg(cpu_client_context_t *cpu, uint64_t reg, uint64_t value)
{
    // Linh WIP
}

int cpu_client_inject_irq(cpu_client_context_t *cpu, int virq, int prio, int group, int idx)
{
    OSDB_PRINTF("Sending 'inject IRQ' request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_inject_irq_tag,
        .msg.inject_irq = {
            .virq = virq,
            .prio = prio,
            .group = group,
            .idx = idx,
        },
    };

    CpuReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, cpu->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_ack_vppi(cpu_client_context_t *cpu, uint64_t irq)
{
    OSDB_PRINTF("Sending 'ack VPPI' request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_ack_vppi_tag,
        .msg.ack_vppi = {
            .irq = irq,
        },
    };

    CpuReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, cpu->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int cpu_client_irq_handler_bind(cpu_client_context_t *cpu, int irq, seL4_CPtr *ret_slot)
{
    OSDB_PRINTF("Sending 'IRQ handler bind' request to CPU component\n");

    int error = 0;

    CpuMessage msg = {
        .magic = CPU_RPC_MAGIC,
        .which_msg = CpuMessage_irq_handler_bind_tag,
        .msg.irq_handler_bind = {
            .irq = irq},
    };

    CpuReturnMessage ret_msg = {0};
    error = sel4gpi_rpc_call(&rpc_env, cpu->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);

    if (!error && ret_slot)
    {
        *ret_slot = ret_msg.msg.irq_handler_bind.slot;
    }

    error |= ret_msg.errorCode;

    return error;
}
