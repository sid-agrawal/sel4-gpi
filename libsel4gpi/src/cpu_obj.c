/**
 * @file cpu_obj.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the methods to manipulate the cpu object
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <sel4utils/process.h>
#include <sel4utils/vspace.h>
#include <sel4utils/util.h>
#include <sel4utils/helpers.h>

#include <sel4gpi/cpu_component.h>
#include <sel4gpi/cpu_obj.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/model_exporting.h>
#include <sel4gpi/error_handle.h>
#include <sel4/sel4.h>
#include <sel4runtime.h>
#include <sel4debug/register_dump.h>

// Defined for utility printing macros
#define DEBUG_ID CPU_DEBUG
#define SERVER_ID CPUSERVS

int cpu_start(cpu_t *cpu)
{
    OSDB_PRINTF("cpu_start: starting CPU (%d) at PC: 0x%lx\n", cpu->id, cpu->reg_ctx->pc);
    seL4_TCB_Resume(cpu->tcb.cptr);
    return 0;
}

int cpu_stop(cpu_t *cpu)
{
    OSDB_PRINTF("cpu_start: stopping CPU (%d) at PC: 0x%lx\n", cpu->id, cpu->reg_ctx->pc);
    return seL4_TCB_Suspend(cpu->tcb.cptr);
}

int cpu_config_vspace(cpu_t *cpu,
                      vka_t *vka,
                      vspace_t *vspace,
                      seL4_CNode root_cnode,
                      seL4_Word cnode_guard,
                      seL4_CPtr fault_ep,
                      seL4_CPtr ipc_buffer_frame,
                      seL4_Word ipc_buf_addr)
{
    int error = 0;
    OSDB_PRINTF("Configuring CPU, cspace: %lx, cspace_guard: %lx, fault_ep: %lx, ipc_buf_addr: %lx, ipc_buf_frame: %lx\n",
                root_cnode, cnode_guard, fault_ep, ipc_buf_addr, ipc_buffer_frame);

    seL4_CPtr vspace_root = vspace->get_root(vspace); // root page table
    SERVER_GOTO_IF_COND(vspace_root == seL4_CapNull, "Couldn't find root page table\n");

    cpu->cspace = root_cnode;
    cpu->cspace_guard = cnode_guard;
    cpu->fault_ep = fault_ep;
    cpu->ipc_buf_addr = ipc_buf_addr;
    cpu->ipc_frame_cap = ipc_buffer_frame;

    error = seL4_TCB_Configure(cpu->tcb.cptr,
                               fault_ep,   // fault endpoint
                               root_cnode, // root cnode
                               cnode_guard,
                               vspace_root,
                               0, // domain
                               ipc_buf_addr,
                               ipc_buffer_frame);
    SERVER_GOTO_IF_ERR(error, "Failed to configure TCB\n");

    error = seL4_TCB_SetPriority(cpu->tcb.cptr, seL4_CapInitThreadTCB, seL4_MaxPrio - 1);

err_goto:
    return error;
}

int cpu_change_vspace(cpu_t *cpu,
                      vka_t *vka,
                      vspace_t *vspace)
{
    OSDB_PRINTF("cpu_change_vspace: Configuring CPU\n");
    int error = 0;
    seL4_CPtr vspace_root = vspace->get_root(vspace); // root page table
    SERVER_GOTO_IF_COND(vspace_root == seL4_CapNull, "Couldn't find root page table\n");

    error = seL4_TCB_Configure(cpu->tcb.cptr,
                               cpu->fault_ep, // fault endpoint
                               cpu->cspace,   // root cnode
                               cpu->cspace_guard,
                               vspace_root,
                               0, // domain
                               cpu->ipc_buf_addr,
                               cpu->ipc_frame_cap);

err_goto:
    return error;
}

int cpu_bind_notif(cpu_t *cpu, seL4_CPtr notif)
{
    OSDB_PRINTF("cpu_change_vspace: Binding notification to CPU\n");

    int error = seL4_TCB_BindNotification(cpu->tcb.cptr, notif);

    return error;
}

int cpu_new(cpu_t *cpu,
            vka_t *vka,
            vspace_t *vspace,
            void *arg0)
{
    int error = vka_alloc_tcb(vka, &cpu->tcb);
    SERVER_GOTO_IF_ERR(error, "Couldn't allocate TCB\n");

    cpu->reg_ctx = calloc(1, sizeof(seL4_UserContext));
    SERVER_GOTO_IF_COND(cpu->reg_ctx == NULL, "Couldn't malloc CPU's register context\n");

    cpu->ipc_buf_mo = 0;

err_goto:
    return error;
}

void cpu_dump_rr(cpu_t *cpu, model_state_t *ms, gpi_model_node_t *pd_node)
{
    gpi_model_node_t *root_node = get_root_node(ms);

    // Add the VCPU resource space
    gpi_model_node_t *vcpu_space_node = add_resource_space_node(ms, GPICAP_TYPE_CPU, get_cpu_component()->space_id);

    /* Add the Virtual CPU node */
    gpi_model_node_t *cpu_node = add_resource_node(
        ms,
        make_res_id(GPICAP_TYPE_CPU, get_cpu_component()->space_id, cpu->id));
    if (cpu->vcpu.cptr != seL4_CapNull)
    {
        set_node_extra(cpu_node, "elevated");
    }
    add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, cpu_node);
    add_edge(ms, GPI_EDGE_TYPE_SUBSET, cpu_node, vcpu_space_node);

    seL4_Word affinity = 0;
#if CONFIG_MAX_NUM_NODES > 1
    seL4_TCB_GetAffinity_t affinity_res = seL4_TCB_GetAffinity(cpu->tcb.cptr);
    affinity = affinity_res.affinity;
#endif

    /* Add the Physical CPU (core) node */
    gpi_model_node_t *cpu_core_node = add_resource_node(ms, make_res_id(GPICAP_TYPE_PCPU, 1, affinity));
    add_edge(ms, GPI_EDGE_TYPE_MAP, cpu_node, cpu_core_node);
    add_edge(ms, GPI_EDGE_TYPE_HOLD, root_node, cpu_core_node);
    add_edge(ms, GPI_EDGE_TYPE_HOLD, root_node, vcpu_space_node); // the RT holds this resource space

// (XXX) Arya: Do not actually show CPU->ADS arrow... do we need it?
#if 0
    // this isn't really an RR, but will be changed in the interp layer
    char ads_res_id[CSV_MAX_STRING_SIZE];
    make_res_id(ads_res_id, GPICAP_TYPE_ADS, cpu->binded_ads_id);
    add_resource_depends_on(ms, cpu_res_id, ads_res_id, REL_TYPE_MAP);
#endif
}

void cpu_destroy(cpu_t *cpu)
{
    // Stop the CPU
    int error = cpu_stop(cpu);

    if (error)
    {
        OSDB_PRINTERR("Error while stopping CPU (%d)\n", cpu->id);
    }

    // Destroy the TCB
    vka_free_object(get_cpu_component()->server_vka, &cpu->tcb);

    // Decrement refcount of bound IPC buf MO
    if (cpu->ipc_buf_mo)
    {
        error = resource_component_dec(get_mo_component(), cpu->ipc_buf_mo);

        if (error)
        {
            OSDB_PRINTERR("Failed to decrement refcount of old IPC buf mo (%d)\n", cpu->ipc_buf_mo);
        }

        cpu->ipc_buf_mo = 0;
    }

    if (cpu->vcpu.cptr != seL4_CapNull)
    {
        vka_free_object(get_cpu_component()->server_vka, &cpu->vcpu);
    }

    // Free other things
    free(cpu->reg_ctx);

    return;
}

int cpu_set_tls_base(cpu_t *cpu, void *tls_base, bool write_reg)
{
    int error = 0;
    OSDB_PRINTF("Setting TLS base (0x%lx) for CPU %d\n", tls_base, cpu->id);
    cpu->tls_base = (void *)tls_base;

    error = sel4utils_arch_init_context_tls_base(cpu->reg_ctx, tls_base);
    SERVER_GOTO_IF_ERR(error, "failed to set TLS base in user context\n");

    if (write_reg)
    {
        error = seL4_TCB_SetTLSBase(cpu->tcb.cptr, (seL4_Word)tls_base);
        SERVER_GOTO_IF_ERR(error, "Failed to write the TLS base register\n");
    }

err_goto:
    return error;
}

int cpu_set_local_context(cpu_t *cpu, void *entry_point,
                          void *arg0, void *arg1,
                          void *arg2, void *init_stack)
{
    int error = 0;
    OSDB_PRINTF("Setting local context with args: [%p, %p, %p]\n", arg0, arg1, arg2);
    error = sel4utils_arch_init_local_context(entry_point, arg0, arg1, arg2, init_stack, cpu->reg_ctx);
    SERVER_GOTO_IF_ERR(error, "failed to set CPU context\n");

    error = seL4_TCB_WriteRegisters(cpu->tcb.cptr, 0, 0, sizeof(seL4_UserContext) / sizeof(seL4_Word), cpu->reg_ctx);
    SERVER_GOTO_IF_ERR(error, "failed to write TCB registers\n");
err_goto:
    return error;
}

int cpu_set_remote_context(cpu_t *cpu, void *entry_point, void *init_stack)
{
    int error = 0;
    error = sel4utils_arch_init_context(entry_point, init_stack, cpu->reg_ctx);
    SERVER_GOTO_IF_ERR(error, "failed to set CPU context\n");

    error = seL4_TCB_WriteRegisters(cpu->tcb.cptr, 0, 0, sizeof(seL4_UserContext) / sizeof(seL4_Word), cpu->reg_ctx);
    SERVER_GOTO_IF_ERR(error, "failed to write TCB registers\n");

err_goto:
    return error;
}

int cpu_set_guest_context(cpu_t *cpu, uintptr_t kernel_entry, uintptr_t kernel_dtb)
{
    int error = 0;
    error = sel4utils_arch_init_context_guest(kernel_entry, kernel_dtb, cpu->reg_ctx);
    SERVER_GOTO_IF_ERR(error, "failed to set CPU context\n");

    error = seL4_TCB_WriteRegisters(cpu->tcb.cptr, 0, 0, sizeof(seL4_UserContext) / sizeof(seL4_Word), cpu->reg_ctx);
    SERVER_GOTO_IF_ERR(error, "failed to write TCB registers\n");

err_goto:
    return error;
}

int cpu_elevate(cpu_t *cpu)
{
    int error = 0;
    error = vka_alloc_vcpu(get_cpu_component()->server_vka, &cpu->vcpu);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate a VCPU\n");

    error = seL4_ARM_VCPU_SetTCB(cpu->vcpu.cptr, cpu->tcb.cptr);

err_goto:
    return error;
}

int cpu_read_registers(cpu_t *cpu, seL4_UserContext *regs)
{
    return seL4_TCB_ReadRegisters(cpu->tcb.cptr, seL4_False, 0, sizeof(seL4_UserContext) / sizeof(seL4_Word), regs);
}
