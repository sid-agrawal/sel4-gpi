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
#include <sel4/sel4.h>
#include <sel4runtime.h>

int cpu_start(cpu_t *cpu, void *entry_point, void *init_stack, seL4_Word arg0)
{
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "cpu_start: starting CPU at entry point %p and arg0 %lx\n", entry_point, arg0);
    int error;
    seL4_UserContext regs = {0};
    // void *stack_top = init_stack == 0 ? cpu->thread.stack_top : init_stack;
    // OSDB_PRINTF(CPU_DEBUG, "init_stack %lx\n", stack_top);

    // /* Initialize the TLS */
    // size_t tls_size = sel4runtime_get_tls_size();
    // uintptr_t tls_base = (uintptr_t)stack_top - tls_size;
    // cpu->tls_base = (uintptr_t)sel4runtime_write_tls_image((void *)tls_base);
    // cpu->thread.stack_top = ALIGN_UP(tls_base, STACK_CALL_ALIGNMENT);

    // error = seL4_TCB_SetTLSBase(cpu->thread.tcb.cptr, cpu->tls_base);
    // assert(error == 0);

    /* Write context and registers */
    // sel4utils_arch_init_local_context((void *)entry_point, (void *)arg0,
    //                                   (void *)cpu->tls_base, (void *)cpu->thread.ipc_buffer_addr, cpu->thread.stack_top, &regs);

    // assert(error == 0);
    // sel4utils_arch_init_context(entry_point)
    error = sel4utils_arch_init_context(entry_point, init_stack, &regs);
    if (error)
    {
        return error;
    }

    error = seL4_TCB_WriteRegisters(cpu->thread.tcb.cptr, 1, 0, sizeof(regs) / sizeof(seL4_Word), &regs);
    return error;
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
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "cpu_config_vspace: Configuring CPU\n");

    seL4_CPtr vspace_root = vspace->get_root(vspace); // root page table
    assert(vspace_root != 0);

    cpu->cspace = root_cnode;
    cpu->cspace_guard = cnode_guard;
    cpu->fault_ep = fault_ep;
    cpu->thread.ipc_buffer_addr = ipc_buf_addr;
    cpu->thread.ipc_buffer = ipc_buffer_frame;

    printf("TEMPA IPC buf vaddr %p\n", ipc_buf_addr);

    int error = seL4_TCB_Configure(cpu->thread.tcb.cptr,
                                   fault_ep,   // fault endpoint
                                   root_cnode, // root cnode
                                   cnode_guard,
                                   vspace_root,
                                   0, // domain
                                   ipc_buf_addr,
                                   ipc_buffer_frame);
    assert(error == 0);

    error = seL4_TCB_SetPriority(cpu->thread.tcb.cptr, seL4_CapInitThreadTCB, seL4_MaxPrio - 1);
    assert(error == 0);

    return 0;
}

int cpu_change_vspace(cpu_t *cpu,
                      vka_t *vka,
                      vspace_t *vspace)
{
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "cpu_change_vspace: Configuring CPU\n");

    seL4_CPtr vspace_root = vspace->get_root(vspace); // root page table
    assert(vspace_root != 0);

    int error = seL4_TCB_Configure(cpu->thread.tcb.cptr,
                                   cpu->fault_ep, // fault endpoint
                                   cpu->cspace,   // root cnode
                                   cpu->cspace_guard,
                                   vspace_root,
                                   0, // domain
                                   cpu->thread.ipc_buffer_addr,
                                   cpu->thread.ipc_buffer);

    return error;
}

int cpu_new(cpu_t *cpu,
            vka_t *vka)
{
    int error = vka_alloc_tcb(vka, &cpu->thread.tcb);
    assert(error == 0);

    /*
            sel4utils_thread_config_t thread_config;
        sel4utils_thread_t thread_obj;
        uint64_t cpu_obj_id;
        vka_object_t *tcb;
        void *stack_top;
        void *tls_base;
        void *ipc_buffer_addr;
        seL4_CPtr ipc_buffer_frame;
    */
}

void cpu_dump_rr(cpu_t *cpu, model_state_t *ms, gpi_model_node_t *pd_node)
{
    gpi_model_node_t *root_node = get_root_node(ms);

    /* Add the Virtual CPU node */
    // (XXX) Arya: Virtual CPU does not belong to a resource space! To fix
    gpi_model_node_t *cpu_node = add_resource_node(ms, GPICAP_TYPE_CPU, 1, cpu->cpu_obj_id);
    add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, cpu_node);

    seL4_Word affinity = 0;
#if CONFIG_MAX_NUM_NODES > 1
    seL4_TCB_GetAffinity_t affinity_res = seL4_TCB_GetAffinity(cpu->thread.tcb.cptr);
    affinity = affinity_res.affinity;
#endif

    /* Add the Physical CPU (core) node */
    gpi_model_node_t *cpu_core_node = add_resource_node(ms, GPICAP_TYPE_PCPU, 1, affinity);
    add_edge(ms, GPI_EDGE_TYPE_MAP, cpu_node, cpu_core_node);
    add_edge(ms, GPI_EDGE_TYPE_HOLD, root_node, cpu_core_node);

// (XXX) Arya: Do not actually show CPU->ADS arrow... do we need it?
#if 0
    // this isn't really an RR, but will be changed in the interp layer
    char ads_res_id[CSV_MAX_STRING_SIZE];
    make_res_id(ads_res_id, GPICAP_TYPE_ADS, cpu->binded_ads_id);
    add_resource_depends_on(ms, cpu_res_id, ads_res_id, REL_TYPE_MAP);
#endif
}
