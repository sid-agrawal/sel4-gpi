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
#include <vka/capops.h>

#include <sel4gpi/cpu_component.h>
#include <sel4gpi/mo_component.h>
#include <sel4gpi/cpu_obj.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/model_exporting.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/gpi_server.h>
#include <sel4/sel4.h>
#include <sel4runtime.h>
#include <sel4debug/register_dump.h>

// Defined for utility printing macros
#define DEBUG_ID CPU_DEBUG
#define SERVER_ID CPUSERVS
#define DEFAULT_ERR CpuComponentError_UNKNOWN

int cpu_start(cpu_t *cpu)
{
    OSDB_PRINTF("cpu_start: starting CPU (%u)\n", cpu->id);
    seL4_TCB_Resume(cpu->tcb.cptr);
    return 0;
}

int cpu_stop(cpu_t *cpu)
{
    OSDB_PRINTF("cpu_stop: stopping CPU (%u)\n", cpu->id);
    return seL4_TCB_Suspend(cpu->tcb.cptr);
}

int cpu_config_vspace(cpu_t *cpu,
                      vspace_t *vspace,
                      uint64_t root_cnode,
                      seL4_Word cnode_guard,
                      seL4_CPtr fault_ep,
                      seL4_CPtr ipc_buffer_frame,
                      void *ipc_buf_addr,
                      int prio)
{
    int error = 0;
    OSDB_PRINTF("Configuring CPU, cspace: %lx, cspace_guard: %lx, fault_ep: %lx, ipc_buf_addr: %p, ipc_buf_frame: %ld\n",
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
                               (seL4_Word)ipc_buf_addr,
                               ipc_buffer_frame);
    SERVER_GOTO_IF_ERR(error, "Failed to configure TCB\n");

    error = seL4_TCB_SetPriority(cpu->tcb.cptr, seL4_CapInitThreadTCB, prio);

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
                               (seL4_Word)cpu->ipc_buf_addr,
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

gpi_model_node_t *cpu_dump_rr(cpu_t *cpu, model_state_t *ms, gpi_model_node_t *pd_node)
{
    gpi_model_node_t *root_node = get_root_node(ms);

    // Add the VCPU resource space
    gpi_model_node_t *vcpu_space_node = get_resource_space_node(ms, GPICAP_TYPE_CPU,
                                                                get_cpu_component()->space_id);

    if (!vcpu_space_node)
    {
        vcpu_space_node = add_resource_space_node(ms, GPICAP_TYPE_CPU,
                                                  get_cpu_component()->space_id, false);
        add_edge(ms, GPI_EDGE_TYPE_HOLD, root_node, vcpu_space_node); // the RT holds this resource space
    }

    // Add the PCPU resource space
    // This is a space that only exists in extraction
    // Just reuse the vcpu space ID, since we know it is, and space IDs are only required to be unique per type,
    // not globally
    gpi_model_node_t *pcpu_space_node = get_resource_space_node(ms, GPICAP_TYPE_CPU,
                                                                get_cpu_component()->space_id);

    if (!pcpu_space_node)
    {
        pcpu_space_node = add_resource_space_node(ms, GPICAP_TYPE_CPU,
                                                  get_cpu_component()->space_id, false);
        add_edge(ms, GPI_EDGE_TYPE_HOLD, root_node, pcpu_space_node);      // the RT holds this resource space
        add_edge(ms, GPI_EDGE_TYPE_MAP, vcpu_space_node, pcpu_space_node); // vcpu space maps to pcpu space
    }

    /* Add the Virtual CPU node */
    gpi_res_id_t cpu_id = make_res_id(GPICAP_TYPE_CPU, get_cpu_component()->space_id, cpu->id);
    gpi_model_node_t *cpu_node = get_resource_node(ms, cpu_id);

    if (!cpu_node)
    {
        cpu_node = add_resource_node(ms, cpu_id, false);
    }

    if (!cpu_node->extracted)
    {

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
        gpi_model_node_t *cpu_core_node = add_resource_node(ms, make_res_id(GPICAP_TYPE_PCPU, 1, affinity), true);
        add_edge(ms, GPI_EDGE_TYPE_MAP, cpu_node, cpu_core_node);
        add_edge(ms, GPI_EDGE_TYPE_HOLD, root_node, cpu_core_node);
        add_edge(ms, GPI_EDGE_TYPE_SUBSET, cpu_core_node, pcpu_space_node);

        cpu_node->extracted = true;
    }

    return cpu_node;
}

void cpu_destroy(cpu_t *cpu)
{
    // Stop the CPU
    int error = cpu_stop(cpu);

    if (error)
    {
        OSDB_PRINTERR("Error while stopping CPU (%u)\n", cpu->id);
    }

    // Destroy the TCB
    vka_free_object(get_cpu_component()->server_vka, &cpu->tcb);

    // Decrement refcount of bound IPC buf MO
    if (cpu->ipc_buf_mo)
    {
        error = resource_component_dec(get_mo_component(), cpu->ipc_buf_mo);

        if (error)
        {
            OSDB_PRINTERR("Failed to decrement refcount of old IPC buf mo (%u)\n", cpu->ipc_buf_mo);
        }

        cpu->ipc_buf_mo = 0;
    }

    if (cpu->binded_ads_id)
    {
        error = resource_component_dec(get_ads_component(), cpu->binded_ads_id);

        if (error)
        {
            OSDB_PRINTERR("Failed to decrement refcount of old ADS (%u)\n", cpu->binded_ads_id);
        }
        cpu->binded_ads_id = 0;
    }

    if (cpu->vcpu.cptr != seL4_CapNull)
    {
        vka_free_object(get_cpu_component()->server_vka, &cpu->vcpu);
    }

    free(cpu->reg_ctx);

    return;
}

int cpu_set_tls_base(cpu_t *cpu, void *tls_base, bool write_reg)
{
    int error = 0;
    OSDB_PRINTF("Setting TLS base (%p) for CPU %u\n", tls_base, cpu->id);
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

int cpu_set_pd_entry_regs(cpu_t *cpu, void *entry_point,
                          void *init_stack, seL4_Word arg1)
{
    int error = 0;
    error = sel4utils_arch_init_context(entry_point, init_stack, cpu->reg_ctx);
    SERVER_GOTO_IF_ERR(error, "failed to set CPU context\n");

    sel4utils_set_arg1(cpu->reg_ctx, arg1);

    error = seL4_TCB_WriteRegisters(cpu->tcb.cptr, 0, 0, SEL4_USER_CONTEXT_COUNT, cpu->reg_ctx);

    SERVER_GOTO_IF_ERR(error, "failed to write TCB registers\n");

err_goto:
    return error;
}

int cpu_set_guest_entry_regs(cpu_t *cpu, uintptr_t kernel_entry, seL4_Word arg0)
{
    int error = 0;
    error = sel4utils_arch_init_context_guest(kernel_entry, arg0, cpu->reg_ctx);
    SERVER_GOTO_IF_ERR(error, "failed to set CPU context\n");

    error = seL4_TCB_WriteRegisters(cpu->tcb.cptr, 0, 0, SEL4_USER_CONTEXT_COUNT, cpu->reg_ctx);
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
    return seL4_TCB_ReadRegisters(cpu->tcb.cptr, seL4_False, 0, SEL4_USER_CONTEXT_COUNT, regs);
}

int cpu_write_registers(cpu_t *cpu, seL4_UserContext *regs, size_t num_reg, bool resume)
{
    int error = 0;
    SERVER_GOTO_IF_COND(num_reg > SEL4_USER_CONTEXT_COUNT,
                        "Cannot write more registers (%zu) than seL4_UserContext: %zu",
                        num_reg, SEL4_USER_CONTEXT_COUNT);
    OSDB_PRINTF("Writing %zu registers, %s CPU\n", num_reg, resume ? "resuming" : "stopping");
    return seL4_TCB_WriteRegisters(cpu->tcb.cptr, resume, 0, num_reg, regs);
err_goto:
    return 1;
}

int cpu_inject_irq(cpu_t *cpu, int virq, int prio, int group, int idx)
{
    return seL4_ARM_VCPU_InjectIRQ(cpu->vcpu.cptr, virq, prio, group, idx);
}

int cpu_ack_vppi(cpu_t *cpu, uint64_t irq)
{
    return seL4_ARM_VCPU_AckVPPI(cpu->vcpu.cptr, irq);
}

void cpu_read_vcpu_regs(cpu_t *cpu, vcpu_regs_t *regs)
{
    if (cpu->vcpu.cptr != seL4_CapNull)
    {
        vcpu_read_regs(cpu->vcpu.cptr, regs);
    }
}

int cpu_resume(cpu_t *cpu)
{
    return seL4_TCB_Resume(cpu->tcb.cptr);
}
