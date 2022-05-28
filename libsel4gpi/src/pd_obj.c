/**
 * @file pd_obj.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the methods to manipulate the pd object
 * @version 0.1
 * @date 2022-04-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include <sel4utils/process.h>
#include <sel4utils/vspace.h>

#include <sel4gpi/pd_component.h>
#include <sel4gpi/pd_obj.h>

int pd_load_image(pd_t *pd,
                      vka_t *vka,
                      vspace_t *vspace,
                      seL4_CNode cspace)
{

    printf(PDSERVS"config_vspace: configuring vspace for pd\n");
    // printf(PDSERVS"cspace info:");
    // debug_cap_identify(PDSERVS, cspace);
    // seL4_CPtr fault_endpoint = seL4_CapNull;
    // pd->thread_config = thread_config_fault_endpoint(pd->thread_config, fault_endpoint);
    // pd->thread_config = thread_config_cspace(pd->thread_config, cspace, 0 /*cspace_root_data*/);
    // pd->thread_config = thread_config_create_reply(pd->thread_config);

    // return sel4utils_configure_thread_config(vka, NULL, vspace, pd->thread_config, &pd->thread_obj);
    return 0;
}

int pd_new(pd_t *pd,
           vka_t *vka)
{

    return 0;
}

int pd_start(pd_t *pd, vka_t *vka){

    printf(PDSERVS"pd_start: starting PD\n");
    return 1;
}
