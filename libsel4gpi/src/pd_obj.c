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

#include <vka/capops.h>

#include <sel4gpi/pd_component.h>
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/mo_component.h>
#include <sel4gpi/ads_component.h>
#include <sel4gpi/cap_tracking.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/ads_obj.h>
#include <sel4gpi/debug.h>

#include <vka/capops.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>

int pd_new(pd_t *pd,
           vka_t *vka,
           vspace_t *server_vspace,
           simple_t *simple)
{

    OSDB_PRINTF(PDSERVS"new PD: \n");

    // for (int i = 0; i < MAX_SYS_OSM_CAPS; i++)
    // {
    //     pd->osm_caps[i].type = GPICAP_TYPE_MAX;
    //     pd->osm_caps[i].slot = 0;
    // }

    pd->vka = vka;
}

int pd_next_slot(pd_t *pd,
                  vka_t *vka,
                  seL4_CPtr *next_free_slot) {

    cspacepath_t path;
    int error = vka_cspace_alloc_path(vka, &path);
    *next_free_slot = error == seL4_NoError ? path.capPtr : seL4_CapNull;
    return error;
}

int pd_alloc_ep(pd_t *pd,
                vka_t *server_vka,
                seL4_CPtr *ret_ep)
{
    // alloc slot in pd
    cspacepath_t dest;

    int error = vka_cspace_alloc_path(&pd->pd_vka, &dest);
    if (error) {
        return error;
    }

    // alloc ep from gpi server's untyped
    seL4_Word res;
    error = vka_utspace_alloc(server_vka, &dest, seL4_EndpointObject, seL4_EndpointBits, &res);

    *ret_ep = error == seL4_NoError ? dest.capPtr : seL4_CapNull;
    return error;
}

int pd_badge_ep(pd_t *pd,
                seL4_CPtr src_ep,
                seL4_Word badge,
                seL4_CPtr *ret_ep)
{
    cspacepath_t src, dest;
    vka_cspace_make_path(&pd->pd_vka, src_ep, &src);

    printf("make src path %lu %lu %lu\n", src.root, src.capPtr, src.capDepth);

    seL4_CPtr dest_cptr;
    vka_cspace_alloc(&pd->pd_vka, &dest_cptr);
    vka_cspace_make_path(&pd->pd_vka, dest_cptr, &dest);

    printf("make dest path %lu %lu %lu \n", dest.root, dest.capPtr, dest.capDepth);

    int error = vka_cnode_mint(&dest,
                               &src,
                               seL4_AllRights,
                               badge);

    *ret_ep = error == seL4_NoError ? dest.capPtr : seL4_CapNull;
    return error;
}

int pd_load_image(pd_t *pd,
                  vka_t *vka,
                  simple_t *simple,
                  const char *image_path,
                  vspace_t *server_vspace,
                  vspace_t *target_vspace,
                  vka_object_t *target_vspace_root_page_dir)
{

    int error = 0;
    OSDB_PRINTF(PDSERVS"load_image: loading image %s for pd %p\n", image_path, pd);


    /* There are just setting up the config */
    pd->config   = process_config_default_simple(simple, image_path, 255);
    pd->config = process_config_mcp(pd->config, seL4_MaxPrio);
    pd->config = process_config_auth(pd->config, simple_get_tcb(simple));
    pd->config = process_config_create_cnode(pd->config, 17);

    sel4utils_process_config_t config = pd->config;
    /* This is doing actual works of setting up the PD's address space */
     error = sel4utils_osm_configure_process_custom(&(pd->proc),
                                                    // get_pd_component()->server_vka,
                                                     pd->vka,
                                                       server_vspace,
                                                       target_vspace,
                                                       target_vspace_root_page_dir,
                                                       config);
    assert(error == 0);

    /* Add the forged MOs*/

    /* set up caps about the process */
    pd->stack_pages = CONFIG_SEL4UTILS_STACK_SIZE / PAGE_SIZE_4K;
    pd->stack = pd->proc.thread.stack_top - CONFIG_SEL4UTILS_STACK_SIZE;
    pd->page_directory_in_pd = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, pd->proc.pd.cptr);
    pd->root_cnode_in_pd = SEL4UTILS_CNODE_SLOT;
    pd->tcb_in_pd = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, pd->proc.thread.tcb.cptr);
    // if (config_set(CONFIG_HAVE_TIMER)) {
    //     pd->timer_ntfn = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, env->timer_notify_test.cptr);
    // }

    /* NOTE:
       The return from sel4utils_copy_cap_to_process is the slot in the cnode where the cap was placed
       in the child process' cspace
    */
    pd->domain_in_pd = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, simple_get_init_cap(simple, seL4_CapDomain));
    pd->asid_pool_in_pd = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, simple_get_init_cap(simple, seL4_CapInitThreadASIDPool));
    pd->asid_ctrl_in_pd = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, simple_get_init_cap(simple, seL4_CapASIDControl));
#ifdef CONFIG_IOMMU
    pd->io_space = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, simple_get_init_cap(simple, seL4_CapIOSpace));
#endif /* CONFIG_IOMMU */
#ifdef CONFIG_TK1_SMMU
    env->init->io_space_caps = arch_copy_iospace_caps_to_process(&(pd->proc), &env);
#endif
    pd->cores = simple_get_core_count(simple);
    /* copy the sched ctrl caps to the remote process */
    if (config_set(CONFIG_KERNEL_MCS))
    {
        seL4_CPtr sched_ctrl = simple_get_sched_ctrl(simple, 0);
        pd->sched_ctrl_in_pd = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, sched_ctrl);
        for (int i = 1; i < pd->cores; i++)
        {
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
    pd->fault_endpoint_in_pd = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, pd->proc.fault_endpoint.cptr);

    /*
        These are RDE Entries.

    */
    seL4_CPtr child_ads_cap_in_parent;
    error = forge_ads_cap_from_vspace(&pd->proc.vspace, pd->vka, &child_ads_cap_in_parent);
    if (error)
    {
        ZF_LOGF("Failed to forge child's as cap");
    }
    pd->child_ads_cptr_in_child = sel4utils_copy_cap_to_process(&(pd->proc),
                                                                pd->vka, child_ads_cap_in_parent);
    assert(pd->child_ads_cptr_in_child != 0);

    OSDB_PRINTF("copied ads ep at %d\n", (int) pd->child_ads_cptr_in_child);

    // SID : Finishd this RDE addting
    pd->rde[0].type.type = GPICAP_TYPE_ADS;
    pd->rde[0].slot_in_RT = child_ads_cap_in_parent;
    pd->rde[0].slot_in_PD = pd->child_ads_cptr_in_child;
    pd->rde[0].pd_obj_id = 0x00;

    // For the GPI server, no need to forge
    pd->gpi_endpoint_in_child = sel4utils_copy_cap_to_process(&(pd->proc),
                                                              pd->vka,
                                                              get_gpi_server()->server_ep_obj.cptr);
    assert(pd->gpi_endpoint_in_child != 0);

    OSDB_PRINTF("copied gpi ep at %d\n", (int) pd->gpi_endpoint_in_child);

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
    pd->cspace_size_bits = pd->proc.cspace_size;

    printf("%s: %d\n", __FUNCTION__, __LINE__);
    uint32_t num_mo_caps = 0;
    seL4_CPtr mo_caps[MAX_MO_CHILD];
    printf("%s: %d\n", __FUNCTION__, __LINE__);
    error = forge_mo_caps_from_vspace(target_vspace,
                                      pd->vka,
                                      &num_mo_caps,
                                      mo_caps);
    assert(error == 0);

    memcpy(&pd->proc.vspace, target_vspace, sizeof(vspace_t));

    pd->free_slots.start = pd->proc.cspace_next_free;
    OSDB_PRINTF("%s:%d: free_slot.start %ld\n", __FUNCTION__, __LINE__, pd->free_slots.start);

    pd->free_slots.end = (1u << pd->cspace_size_bits);
    assert(pd->free_slots.start < pd->free_slots.end);

    /* Initialize a vka for the PD's cspace */
    allocman_t *allocator = bootstrap_use_current_1level(pd->proc.cspace.cptr,
                                                         pd->cspace_size_bits,
                                                         pd->free_slots.start,
                                                         pd->free_slots.end,
                                                         PD_ALLOCATOR_STATIC_POOL_SIZE,
                                                         pd->allocator_mem_pool);
    if (allocator == NULL)
    {
        ZF_LOGF("Failed to bootstrap allocator for pd");
    }
    allocman_make_vka(&pd->pd_vka, allocator);

    return 0;
}

int pd_send_cap(pd_t *to_pd,
                seL4_CPtr cap,
                seL4_Word badge,
                seL4_Word *slot)
{

    /*
        (XXX): Need to handle how sending OSM caps would leand to additional data tracking.
    */
    assert(cap != 0);
    ZF_LOGE("pd_send_cap: Sending cap %ld(badge:%lx) to pd %p\n", cap, badge, to_pd);



    /*
        Find out if the cap is an OSmosis cap or not.
    */
    if (badge) {
        // Find the pd from where ths cap came (do we need it)
        gpi_cap_t cap_type = get_cap_type_from_badge(badge);
        switch (cap_type){
            case GPICAP_TYPE_ADS:
                ZF_LOGF("Sending ADS cap is not supported yet");
                break;
            case GPICAP_TYPE_MO: ;
                seL4_Word new_badge = gpi_new_badge(cap_type,
                                                    get_perms_from_badge(badge),
                                                    to_pd->pd_obj_id, /* Client ID*/
                                                    get_object_id_from_badge(badge));
                // Increment the counter in the mo_t object.
                mo_component_registry_entry_t *mo_reg = mo_component_registry_get_entry_by_badge(badge);
                assert(mo_reg != NULL);
                mo_reg->count++;

                // Mint a new cap for the child.
                cspacepath_t src, dest;
                vka_cspace_make_path(get_mo_component()->server_vka,
                                     get_mo_component()->server_ep_obj.cptr, &src);
                seL4_CPtr dest_cptr;
                vka_cspace_alloc(get_mo_component()->server_vka, &dest_cptr);
                vka_cspace_make_path(get_mo_component()->server_vka, dest_cptr, &dest);

                int error = vka_cnode_mint(&dest,
                                           &src,
                                           seL4_AllRights,
                                           new_badge);
                if (error)
                {
                    OSDB_PRINTF(PDSERVS "%s: Failed to mint new_badge %lx.\n",
                                __FUNCTION__, new_badge);
                    return 1;
                }
                cap = dest_cptr;
                break;
            case GPICAP_TYPE_CPU:
                ZF_LOGF("Sending CPU cap is not supported yet");
                break;
            case GPICAP_TYPE_PD:
                ZF_LOGF("Sending PD cap is not supported yet");
                break;
            default:
                //ZF_LOGF("Unknown cap type in %s", __FUNCTION__);
                // (XXX) Arya: allowing unknown cap type for now to send parent ep
                ZF_LOGI("Unknown cap type in %s", __FUNCTION__);
            }

        // Find the pd where the cap is going, and basd
        // Create a new badge and then badge the unbadged gpi-server cap with the new badge
        // new badge = (old badge & client id mask) | (new client id << client id offset)

        // forge a copy of the cap with the type, perms, and obj id, but different client id
        // Insert it in the appropirate list

        // do the same copy as above
    } else {
        // This is a cap from the kernel.
        // Just copy it to the child.
    }

    OSDB_PRINTF(PDSERVS "pd_send_cap: copying cap to child: %lu\n", *slot);
    *slot = sel4utils_copy_cap_to_process(&(to_pd->proc), to_pd->vka, cap);
    if (*slot == 0)
    {
        ZF_LOGF("Failed to copy cap to process");
        return -1;
    }
    OSDB_PRINTF(PDSERVS "pd_send_cap: copied cap at %ld to child\n", *slot);

    /* Add to our caps data struct */

    return 0;
}

int pd_start(pd_t *pd,
             vka_t *vka,
             seL4_CPtr pd_endpoint_in_root,
             vspace_t *server_vspace,
             seL4_Word arg0)
{

    OSDB_PRINTF(PDSERVS "pd_start: ARGS: pd_endpoint_in_root: %ld, arg0: %ld\n",
                pd_endpoint_in_root, arg0);
    // For the PD server, forge and copy
    /* No need to forge, you already have it */
    assert(&pd->proc != NULL);
    assert(&pd->proc.vspace != NULL);

    seL4_CPtr pd_cptr_in_child = sel4utils_copy_cap_to_process(&(pd->proc),
                                                                vka, pd_endpoint_in_root);
    assert(pd_cptr_in_child!= 0);
    // Phase1: Start it.
    // Phase2: start the CPU thread.

    /* set up args for the test process */
    seL4_Word argc = 1;
    char string_args[argc][WORD_STRING_SIZE];
    char *argv[argc];
    sel4utils_create_word_args(string_args, argv, argc, arg0);

    //argc = 1;
    //snprintf(argv[0], WORD_STRING_SIZE, "%ld", arg0);



    int num_res;
    /* spawn the process */
    seL4_CPtr osm_caps[] = {pd->child_ads_cptr_in_child,
                            pd->gpi_endpoint_in_child,
                            pd_cptr_in_child
                            };
    int error = sel4utils_osm_spawn_process_v(&(pd->proc),
                                              osm_caps,

                                              pd->vka,
                                              server_vspace,
                                              argc,
                                              argv,
                                              1);
    ZF_LOGF_IF(error != 0, "Failed to start test process!");
    OSDB_PRINTF(PDSERVS"pd_start: starting PD\n");
    return 0;
}

int pd_dump(pd_t *pd)
{
    OSDB_PRINTF(PDSERVS "pd_dump_cap: Dumping all details of PD:%u\n", pd->pd_obj_id);

    /*
        For all caps that belong to this PD
            switch {
                case: seL4:
                    Print Debug Info
                case: OSmosis:
                    Get the RR for that cap
            }
    */
    model_state_t *ms = (model_state_t *)malloc(sizeof(model_state_t));
    assert(ms != NULL);
    init_model_state(ms);

    /* These two do not belong here*/
    char pd_name[CSV_MAX_STRING_SIZE];
    snprintf(pd_name, CSV_MAX_STRING_SIZE, "Proc_%u", pd->pd_obj_id);
    char pd_id[CSV_MAX_STRING_SIZE];
    snprintf(pd_id, CSV_MAX_STRING_SIZE, "PD_%u", pd->pd_obj_id);
    add_pd(ms, pd_name, pd_id);

    gpi_server_context_t *gpis = get_gpi_server();
    for (int idx = 0; idx < MAX_PD_OSM_CAPS; idx++)
    {

        // if type seL4 cap
        //  print_pd_osm_cap_info(&pd->has_access_to[idx]);
        //  else if type osmosis cap
        //  get the RR for that cap
        switch (pd->has_access_to[idx].type)
        {
        case GPICAP_TYPE_ADS: ;
            char res_id[20];
            snprintf(res_id, 20, "ADS_%lu", pd->has_access_to[idx].res_id);
            add_has_access_to(ms,
                              pd_id,
                              res_id,
                              "true");
            ads_component_registry_entry_t *client_data =
                ads_component_registry_get_entry_by_id(pd->has_access_to[idx].res_id);
            assert(client_data != NULL);
            ads_dump_rr(&client_data->ads, ms);

            break;
        case GPICAP_TYPE_MO:
            break;
        case GPICAP_TYPE_CPU:
            break;
        case GPICAP_TYPE_PD:
            break;
        default:
            ZF_LOGF("Calling anothe PD to get the info %s", __FUNCTION__);
            break;
        }
    }

    /* Print RDE Info*/
    for (int idx = 0; idx < MAX_PD_OSM_RDE; idx++)
    {
        print_pd_osm_rde_info(&pd->rde[idx]);

        // Find pd from the pd_id
        // if pd found
        // pd_dump(&pd->rde[idx].pd_obj_id);
    }
    print_model_state(ms);
    free(ms);

    return 0;
}

inline void print_pd_osm_cap_info (osmosis_pd_cap_t *o) {
    printf("Slot_RT:%lx\t Slot_PD: %lx\t Slot_ServerPD: %lx\t T: %s\n",
           o->slot_in_RT,
           o->slot_in_PD,
           o->slot_in_ServerPD,
           cap_type_to_str(o->type));
}

inline void print_pd_osm_rde_info (osmosis_rde_t *o) {
    if (o->slot_in_RT == 0) {
        return;
    }
    printf("RDE: PD_ID: %u\t Slot_RT:%lu\t Slot_PD: %lu\t T: %s\n",
           o->pd_obj_id,
           o->slot_in_RT,
           o->slot_in_PD,
           cap_type_to_str(o->type.type));
}
