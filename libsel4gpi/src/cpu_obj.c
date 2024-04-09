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

int cpu_start(cpu_t *cpu, sel4utils_thread_entry_fn entry_point, seL4_Word init_stack, seL4_Word arg0)
{
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "cpu_start: starting CPU at entry point %p and arg0 %lx\n", entry_point, arg0);
    int error;
    seL4_UserContext regs = {0};
    void *stack_top = init_stack == 0 ? cpu->stack_top : init_stack;
    OSDB_PRINTF(CPU_DEBUG, "init_stack %lx\n", stack_top);
    sel4utils_arch_init_local_context((void *)entry_point, (void *)arg0,
                                      NULL, (void *)cpu->ipc_buffer_addr, stack_top, &regs);

    error = seL4_TCB_WriteRegisters(cpu->tcb->cptr, 0, 0, sizeof(regs) / sizeof(seL4_Word), &regs);
    assert(error == 0);

    // resume the new thread
    error = seL4_TCB_Resume(cpu->tcb->cptr);
    assert(error == 0);
    return 0;
}

int cpu_config_vspace(cpu_t *cpu,
                      vka_t *vka,
                      vspace_t *vspace,
                      seL4_CNode root_cnode,
                      seL4_Word cnode_guard,
                      seL4_CPtr fault_ep,
                      seL4_CPtr ipc_buffer_frame,
                      seL4_Word ipc_buf_addr,
                      void *stack_top)
{
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "cpu_config_vspace: Configuring CPU\n");

    seL4_CPtr vspace_root = vspace->get_root(vspace); // root page table
    assert(vspace_root != 0);

    cpu->cspace = root_cnode;
    cpu->cspace_guard = cnode_guard;
    cpu->fault_ep = fault_ep;
    cpu->ipc_buffer_addr = ipc_buf_addr;
    cpu->ipc_buffer_frame = ipc_buffer_frame;

    int error = seL4_TCB_Configure(cpu->tcb->cptr,
                                   fault_ep,   // fault endpoint
                                   root_cnode, // root cnode
                                   cnode_guard,
                                   vspace_root,
                                   0, // domain
                                   ipc_buf_addr,
                                   ipc_buffer_frame);
    assert(error == 0);

    error = seL4_TCB_SetPriority(cpu->tcb->cptr, seL4_CapInitThreadTCB, 254);
    assert(error == 0);

    if (stack_top != NULL)
    {
        cpu->tls_base = stack_top;
        cpu->stack_top -= 0x100;
        error = seL4_TCB_SetTLSBase(cpu->tcb->cptr, (seL4_Word)cpu->stack_top);
        assert(error == 0);
        cpu->stack_top -= 0x100;
    }

    return 0;
}
int cpu_change_vspace(cpu_t *cpu,
                      vka_t *vka,
                      vspace_t *vspace)
{
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "cpu_change_vspace: Configuring CPU\n");

    seL4_CPtr vspace_root = vspace->get_root(vspace); // root page table
    assert(vspace_root != 0);

    int error = seL4_TCB_Configure(cpu->tcb->cptr,
                                   cpu->fault_ep, // fault endpoint
                                   cpu->cspace,   // root cnode
                                   cpu->cspace_guard,
                                   vspace_root,
                                   0, // domain
                                   cpu->ipc_buffer_addr,
                                   cpu->ipc_buffer_frame);

    return error;
}

int cpu_new(cpu_t *cpu,
            vka_t *vka)
{

    cpu->tcb = malloc(sizeof(vka_object_t));
    assert(cpu->tcb != NULL);

    int error = vka_alloc_tcb(vka, cpu->tcb);
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

void cpu_dump_rr(cpu_t *cpu, model_state_t *ms)
{
    char cpu_res_id[CSV_MAX_STRING_SIZE];
    make_res_id(cpu_res_id, GPICAP_TYPE_CPU, cpu->cpu_obj_id);
    add_resource(ms, cap_type_to_str(GPICAP_TYPE_CPU), cpu_res_id);
    seL4_Word affinity = 0;
#if CONFIG_MAX_NUM_NODES > 1
    seL4_TCB_GetAffinity_t affinity_res = seL4_TCB_GetAffinity(cpu->tcb->cptr);
    affinity = affinity_res.affinity;
#endif
    char core_res_id[CSV_MAX_STRING_SIZE];
    make_phys_res_id(core_res_id, cpu->cpu_obj_id, affinity, "PCPU");
    add_resource(ms, "PCPU", core_res_id);
    add_resource_depends_on(ms, cpu_res_id, core_res_id, REL_TYPE_MAP);

// (XXX) Arya: Do not actually show CPU->ADS arrow... do we need it?    
#if 0
    // this isn't really an RR, but will be changed in the interp layer
    char ads_res_id[CSV_MAX_STRING_SIZE];
    make_res_id(ads_res_id, GPICAP_TYPE_ADS, cpu->binded_ads_id);
    add_resource_depends_on(ms, cpu_res_id, ads_res_id, REL_TYPE_MAP);
#endif
}
