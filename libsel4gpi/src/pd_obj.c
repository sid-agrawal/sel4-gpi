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


int pd_new(pd_t *pd,
           vka_t *vka,
           vspace_t *server_vspace,
           simple_t *simple
){

    printf(PDSERVS"new PD: \n");


    pd->vka = vka;
    // Allocate a new cspace


    sel4utils_process_config_t config = process_config_default_simple(simple, "hello", 255);
    config = process_config_mcp(config, seL4_MaxPrio);
    config = process_config_auth(config, simple_get_tcb(simple));
    config = process_config_create_cnode(config, 17);
    int error = sel4utils_configure_process_custom(&(pd->proc), pd->vka, server_vspace, config);
    assert(error == 0);

    /* set up caps about the process */
    pd->stack_pages = CONFIG_SEL4UTILS_STACK_SIZE / PAGE_SIZE_4K;
    pd->stack = pd->proc.thread.stack_top - CONFIG_SEL4UTILS_STACK_SIZE;
    pd->page_directory = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, pd->proc.pd.cptr);
    pd->root_cnode = SEL4UTILS_CNODE_SLOT;
    pd->tcb = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, pd->proc.thread.tcb.cptr);
    // if (config_set(CONFIG_HAVE_TIMER)) {
    //     pd->timer_ntfn = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, env->timer_notify_test.cptr);
    // }

    /* NOTE:
       The return from sel4utils_copy_cap_to_process is the slot in the cnode where the cap was placed
       in the child process' cspace
    */
    pd->domain = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, simple_get_init_cap(simple,
                                                                                                           seL4_CapDomain));
    pd->asid_pool = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, simple_get_init_cap(simple,
                                                                                                              seL4_CapInitThreadASIDPool));
    pd->asid_ctrl = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, simple_get_init_cap(simple,
                                                                                                              seL4_CapASIDControl));
#ifdef CONFIG_IOMMU
    pd->io_space = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, simple_get_init_cap(simple,
                                                                                                             seL4_CapIOSpace));
#endif /* CONFIG_IOMMU */
#ifdef CONFIG_TK1_SMMU
    env->init->io_space_caps = arch_copy_iospace_caps_to_process(&(pd->proc), &env);
#endif
    pd->cores = simple_get_core_count(simple);
    /* copy the sched ctrl caps to the remote process */
    if (config_set(CONFIG_KERNEL_MCS)) {
        seL4_CPtr sched_ctrl = simple_get_sched_ctrl(simple, 0);
        pd->sched_ctrl = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, sched_ctrl);
        for (int i = 1; i < pd->cores; i++) {
            sched_ctrl = simple_get_sched_ctrl(simple, i);
            sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, sched_ctrl);
        }
    }
    /* setup data about untypeds */
    // pd->untypeds = copy_untypeds_to_process(&(pd->proc),
    //                                                env->untypeds,
    //                                                env->num_untypeds,
    //                                                env);
    /* copy the fault endpoint - we wait on the endpoint for a message
     * or a fault to see when the test finishes */
    pd->fault_endpoint = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, pd->proc.fault_endpoint.cptr);

    // For the child's as cap in the child
    // First forge a cap to the child's vspace
    // seL4_CPtr child_ads_cap_in_parent;
    // error = forge_ads_cap_from_vspace(&pd->proc.vspace, pd->vka, &child_ads_cap_in_parent);
    // if (error){
    //     ZF_LOGF("Failed to forge child's as cap");
    // }

    // env->child_ads_cptr_in_child = sel4utils_copy_cap_to_process(&(pd->proc),
    //                                                             pd->vka, child_ads_cap_in_parent);
    // For the ads-server
    // pd->gpi_endpoint_in_child = sel4utils_copy_cap_to_process(&(pd->proc),
                                                                // pd->vka, env->gpi_endpoint_in_parent);

    /* copy the device frame, if any */
    // if (pd->device_frame_cap) {
    //     pd->device_frame_cap = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, env->device_obj.cptr);
    // }

    // /* map the cap into remote vspace */
    // What to do with env->init here
    // env->remote_vaddr = vspace_share_mem(pd->vspace, &(pd->proc).vspace, env->init, 1, PAGE_BITS_4K,
    //                                      seL4_AllRights, 1);

    // assert(env->remote_vaddr != 0);

    /* WARNING: DO NOT COPY MORE CAPS TO THE PROCESS BEYOND THIS POINT,
     * AS THE SLOTS WILL BE CONSIDERED FREE AND OVERRIDDEN BY THE TEST PROCESS. */
    /* set up free slot range */
    pd->cspace_size_bits = 17;//TEST_PROCESS_CSPACE_SIZE_BITS;
    if (pd->device_frame_cap) {
        pd->free_slots.start = pd->device_frame_cap + 1;
    } else {
        //pd->free_slots.start = pd->gpi_endpoint_in_child + 1;
        pd->free_slots.start = pd->fault_endpoint + 1;
        printf("%s:%d: free_slot.start %ld\n", __FUNCTION__, __LINE__, pd->free_slots.start);
    }
    pd->free_slots.end = (1u << 17);
    assert(pd->free_slots.start < pd->free_slots.end);
}

int pd_load_image(pd_t *pd,
                  const char *image_path)
                  
{

    printf(PDSERVS"load_image: loading image for pd %p\n", pd);
    // printf(PDSERVS"cspace info:");
    // debug_cap_identify(PDSERVS, cspace);
    // seL4_CPtr fault_endpoint = seL4_CapNull;
    // pd->thread_config = thread_config_fault_endpoint(pd->thread_config, fault_endpoint);
    // pd->thread_config = thread_config_cspace(pd->thread_config, cspace, 0 /*cspace_root_data*/);
    // pd->thread_config = thread_config_create_reply(pd->thread_config);

    // return sel4utils_configure_thread_config(vka, NULL, vspace, pd->thread_config, &pd->thread_obj);

    // Asign a AS_CAP
    // Asign a CPU Cap
    return 0;
}
int pd_start(pd_t *pd, vspace_t *server_vspace){

    // Phase1: Start it.
    // Phase2: start the CPU thread.

    /* set up args for the test process */
    seL4_Word argc = 0;
    char string_args[argc][WORD_STRING_SIZE];
    char *argv[argc];
    sel4utils_create_word_args(string_args, argv, argc);

    int num_res;
    /* spawn the process */
    int  error = sel4utils_spawn_process_v(&(pd->proc), pd->vka, server_vspace,
                                      argc, argv, 1);
    ZF_LOGF_IF(error != 0, "Failed to start test process!");
    printf(PDSERVS"pd_start: starting PD\n");
    return 0;
}
