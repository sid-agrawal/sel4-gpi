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

int cpu_start(cpu_t *cpu, sel4utils_thread_entry_fn entry_point){

    printf(CPUSERVS"cpu_start: starting CPU\n");
    // Use all of parts of sel4utils_start_thread
    return sel4utils_start_thread(&cpu->thread_obj,
                                  entry_point,
                                  0xa, // arg0
                                  0xb, // arg1
                                  1/*resume*/);
}

int cpu_config_vspace(cpu_t *cpu,
                      vka_t *vka,
                      vspace_t *vspace,
                      seL4_CNode cspace)
{

    printf(CPUSERVS"config_vspace: configuring vspace for cpu\n");
    printf(CPUSERVS"cspace info: ");
    debug_cap_identify(CPUSERVS, cspace);
    seL4_CPtr fault_endpoint = seL4_CapNull;
    cpu->thread_config = thread_config_fault_endpoint(cpu->thread_config, fault_endpoint);
    cpu->thread_config = thread_config_cspace(cpu->thread_config, cspace, 0 /*cspace_root_data*/);
    cpu->thread_config = thread_config_create_reply(cpu->thread_config);
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