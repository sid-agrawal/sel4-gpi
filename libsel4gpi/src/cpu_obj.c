/**
 * @file ads_obj.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the methods to manipulate the ads object
 * @version 0.1
 * @date 2022-04-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include <sel4gpi/cpu_obj.h>
#include <sel4utils/process.h>
#include <sel4utils/vspace.h>
#include <sel4utils/thread.h>

int cpu_start(cpu_t *cpu, sel4utils_thread_entry_fn entry_point){
    // Use all of parts of sel4utils_start_thread
    return sel4utils_start_thread(&cpu->obj,
                                  entry_point,
                                  NULL, // arg0
                                  NULL, // arg1
                                  1, /*resume*/);
}

int cpu_config_vspace(cpu_t *cpu,  vka_t *vka, vspace_t *vspace, seL4_CNode cspace){

    seL4_CPtr fault_endpoint = seL4_CapNull;
    cpu->config = thread_config_fault_endpoint(cpu->config, fault_endpoint);
    cpu->config = thread_config_cspace(cpu->config, cspace, 0 /*cspace_root_data*/);
    cpu->config = thread_config_create_reply(cpu->config);

    return sel4utils_configure_thread_config(vka, NULL, vspace, cpu->config, &cpu->obj);
}

int cpu_new(cpu_t *cpu){


}