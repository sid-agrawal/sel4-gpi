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

int cpu_start(cpu_t *cpu, sel4utils_thread_entry_fn entry_point, seL4_Word arg0){

    OSDB_PRINTF(CPUSERVS "cpu_start: starting CPU at entry point %p and arg0 %lx\n", entry_point, arg0);

    UNUSED seL4_UserContext regs = {0};
    int error = seL4_TCB_ReadRegisters(cpu->tcb->cptr,
                                       0, 0, sizeof(regs) / sizeof(seL4_Word), &regs);
    assert(error == 0);
    sel4utils_arch_init_local_context((void *)entry_point, cpu->ipc_buffer_addr,
                                      NULL, NULL, cpu->stack_top, &regs);
    assert(error == 0);

    error = seL4_TCB_WriteRegisters(cpu->tcb->cptr, 0, 0, sizeof(regs)/sizeof(seL4_Word), &regs);
    assert(error == 0);


    // resume the new thread
    error = seL4_TCB_Resume(cpu->tcb->cptr);
    assert(error == 0);
    return 0;
}

// (XXX) This does the allocation which is weird.
int cpu_config_vspace(cpu_t *cpu,
                      vka_t *vka,
                      vspace_t *vspace,
                      seL4_CNode root_cnode,
                      seL4_CPtr fault_ep)
{
    OSDB_PRINTF(CPUSERVS"cpu_config_vspace: Configuring CPU\n");


    seL4_CPtr vspace_root  = vspace->get_root(vspace); // root page table
    assert(vspace_root != 0);

    cpu->ipc_buffer_addr =  vspace_new_ipc_buffer(vspace, &cpu->ipc_buffer_frame);
    assert(cpu->ipc_buffer_addr != NULL);
    OSDB_PRINTF(CPUSERVS"%s: line %d\n", __func__, __LINE__);

    OSDB_PRINTF(CPUSERVS"%s: %d\n", __func__, __LINE__);
    cpu->stack_top = vspace_new_sized_stack(vspace, 8);
    assert(cpu->stack_top != NULL);

    cpu->tls_base = cpu->stack_top;
    cpu->stack_top -= 0x100;
    cpu->cspace = root_cnode;

    int error = seL4_TCB_Configure(cpu->tcb->cptr,
                               seL4_CapNull,             // fault endpoint
                               root_cnode,               // root cnode
                               0,                        // root cnode size
                               vspace_root,
                               0,                        // domain
                               (seL4_Word)cpu->ipc_buffer_addr,
                               cpu->ipc_buffer_frame);
    assert(error == 0);

    error = seL4_TCB_SetPriority(cpu->tcb->cptr, seL4_CapInitThreadTCB, 254);
    assert(error == 0);


    error = seL4_TCB_SetTLSBase(cpu->tcb->cptr, (seL4_Word) cpu->stack_top);
    assert(error == 0);

    cpu->stack_top -= 0x100;

    return 0;
}
int cpu_change_vspace(cpu_t *cpu,
                      vka_t *vka,
                      vspace_t *vspace)
{
    OSDB_PRINTF(CPUSERVS"cpu_change_vspace: Configuring CPU\n");

    /* Wheres is the cspace?*/

    seL4_CPtr vspace_root  = vspace->get_root(vspace); // root page table
    assert(vspace_root != 0);

    int error = seL4_TCB_Configure(cpu->tcb->cptr,
                               seL4_CapNull,             // fault endpoint
                               cpu->cspace,               // root cnode
                               0,                        // root cnode size
                               vspace_root,
                               0,                        // domain
                               (seL4_Word)cpu->ipc_buffer_addr,
                               cpu->ipc_buffer_frame);
    assert(error == 0);
    return 0;
}
int cpu_new(cpu_t *cpu,
            vka_t *vka)
{

    cpu->tcb = malloc(sizeof(vka_object_t)) ;
    assert (cpu->tcb != NULL);

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