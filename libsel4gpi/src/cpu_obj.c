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

int cpu_start(cpu_t *cpu, sel4utils_thread_entry_fn entry_point, seL4_Word arg0){

    printf(CPUSERVS"cpu_start: starting CPU at entry point %p\n", entry_point);
    // Use all of parts of sel4utils_start_thread
    sel4utils_thread_t *thread = &cpu->thread_obj;
    seL4_Word arg1 = 0x0;
    // return sel4utils_start_thread(&cpu->thread_obj,
    //                               entry_point,
    //                              (void *) arg0,
    //                              (void *) 0xb, // arg1
    //                               1/*resume*/);
    seL4_UserContext context = {0};
    size_t context_size = sizeof(seL4_UserContext) / sizeof(seL4_Word);

    size_t tls_size = sel4runtime_get_tls_size();
    /* make sure we're not going to use too much of the stack */
    if (tls_size > thread->stack_size * PAGE_SIZE_4K / 8) {
        ZF_LOGE("TLS would use more than 1/8th of the application stack %zu/%zu", tls_size, thread->stack_size);
        return -1;
    }
    seL4_DebugDumpScheduler();
    uintptr_t tls_base = (uintptr_t)thread->initial_stack_pointer - tls_size;
    // uintptr_t tp = (uintptr_t)sel4runtime_write_tls_image((void *)tls_base);
    // seL4_IPCBuffer *ipc_buffer_addr = (void *)thread->ipc_buffer_addr;
    // sel4runtime_set_tls_variable(tp, __sel4_ipc_buffer, ipc_buffer_addr);

    uintptr_t aligned_stack_pointer = ALIGN_DOWN(tls_base, STACK_CALL_ALIGNMENT);

    int error = sel4utils_arch_init_local_context(entry_point, 
    thread->initial_stack_pointer, // arg0 
    thread->ipc_buffer_addr, // arg1
                                                  (void *) thread->ipc_buffer_addr,
                                                  (void *) aligned_stack_pointer,
                                                  &context);
    if (error) {
        return error;
    }

    error = seL4_TCB_WriteRegisters(thread->tcb.cptr, false, 0, context_size, &context);
    if (error) {
        return error;
    }

    // error = seL4_TCB_SetTLSBase(thread->tcb.cptr, tp);
    // if (error) {
    //     return error;
    // }

        return seL4_TCB_Resume(thread->tcb.cptr);
    return 0;
    printf(CPUSERVS"cpu_start: starting CPU at entry point %p\n", entry_point);
                                
}

int cpu_config_vspace(cpu_t *cpu,
                      vka_t *vka,
                      vspace_t *vspace,
                      seL4_CNode cspace,
                      seL4_CPtr fault_ep)
{

    printf(CPUSERVS"config_vspace: configuring vspace(%p) for cpu\n", vspace);
    printf(CPUSERVS"cspace info: ");
    debug_cap_identify(CPUSERVS, cspace);
    
    // (XXX) This EP needs to be setup.
    // sel4utils_thread_config_t config = thread_config_new(simple);
    cpu->thread_config = thread_config_auth(cpu->thread_config, seL4_CapInitThreadTCB);


    cpu->thread_config = thread_config_fault_endpoint(cpu->thread_config, fault_ep);
    cpu->thread_config = thread_config_cspace(cpu->thread_config, cspace, 0 /*cspace_root_data*/);
    cpu->thread_config = thread_config_create_reply(cpu->thread_config);
    cpu->thread_config = thread_config_priority(cpu->thread_config, 254);
    cpu->thread_config.no_ipc_buffer = false;//true;

    int error =  sel4utils_configure_thread_config(vka, NULL, vspace, cpu->thread_config, &cpu->thread_obj);
    printf(CPUSERVS"stack: %p ipc-buf: %p\n", 
    cpu->thread_obj.stack_top, cpu->thread_obj.ipc_buffer_addr);


    // What happens to stack ptr?
    // What happens to tls?

    return error;
}

int cpu_new(cpu_t *cpu){


}