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

#include <sel4gpi/cpu_component.h>
#include <sel4gpi/cpu_obj.h>
#include <sel4utils/util.h>
#include <sel4utils/helpers.h>

int cpu_start(cpu_t *cpu, sel4utils_thread_entry_fn entry_point, seL4_Word arg0){

    printf(CPUSERVS"cpu_start: starting CPU at entry point %p\n", entry_point);
    arg0 = (seL4_Word) cpu->ipc_buffer_addr;

   UNUSED seL4_UserContext regs = {0};
    int error = seL4_TCB_ReadRegisters(cpu->tcb.cptr,
                                       0, 0, sizeof(regs) / sizeof(seL4_Word), &regs);
    assert(error == 0);
    sel4utils_arch_init_local_context((void *)entry_point, (void *)arg0,
                                      NULL, NULL, cpu->stack_top, &regs);
    assert(error == 0);

    error = seL4_TCB_WriteRegisters(cpu->tcb.cptr, 0, 0, sizeof(regs)/sizeof(seL4_Word), &regs);
    assert(error == 0);


    // resume the new thread
    error = seL4_TCB_Resume(cpu->tcb.cptr);
    assert(error == 0);
    return 0;
}


int cpu_config_vspace(cpu_t *cpu,
                      vka_t *vka,
                      vspace_t *vspace,
                      seL4_CNode root_cnode,
                      seL4_CPtr fault_ep,
                      void *stack_top,
                      void *ipc_buf){

    printf(CPUSERVS"cpu_config_vspace: starting CPU at stack top %p and ipc buf %p\n",  stack_top, ipc_buf);

    int error = vka_alloc_tcb(vka, &cpu->tcb);
    assert(error == 0);

    seL4_CPtr vspace_root  = vspace->get_root(vspace); // root page table
    assert(vspace_root != 0);
    
    seL4_CPtr ipc_buf_frame  = vspace->get_cap(vspace, ipc_buf); // ipc buffer frame
    assert(ipc_buf_frame != 0);

    error = seL4_TCB_Configure(cpu->tcb.cptr,
                               seL4_CapNull,             // fault endpoint
                               root_cnode,               // root cnode
                               0,                        // root cnode size
                               vspace_root,
                               0,                        // domain
                               (seL4_Word)ipc_buf,
                               ipc_buf_frame);
    assert(error == 0);

    error = seL4_TCB_SetPriority(cpu->tcb.cptr, seL4_CapInitThreadTCB, 254);
    assert(error == 0);

                                
    error = seL4_TCB_SetTLSBase(cpu->tcb.cptr, (seL4_Word) stack_top);
    assert(error == 0);


    cpu->stack_top = stack_top - 0x100;
    cpu->ipc_buffer_addr = ipc_buf;
    return 0;



}

int cpu_new(cpu_t *cpu){



}