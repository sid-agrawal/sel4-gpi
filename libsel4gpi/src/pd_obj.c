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
#include <simple/simple_helpers.h>
#include <utils/uthash.h>

#define CSPACE_SIZE_BITS 17

static int copy_cap_to_pd(pd_t *to_pd,
                          seL4_CPtr cap,
                          seL4_Word *slot)
{
    int error;
    seL4_CPtr free_slot;

    error = pd_next_slot(to_pd, &free_slot);
    if (error != 0)
    {
        ZF_LOGE("copy_cap_to_pd: Failed to get a free slot in PD\n");
        return error;
    }

    cspacepath_t src, dest;
    vka_cspace_make_path(to_pd->vka, cap, &src);
    vka_cspace_make_path(&to_pd->pd_vka, free_slot, &dest);

    error = vka_cnode_copy(&dest, &src, seL4_AllRights);
    if (error != 0)
    {
        ZF_LOGE("copy_cap_to_pd: Failed to copy cap\n");
        return error;
    }

    if (slot != NULL)
    {
        *slot = free_slot;
    }

    return 0;
}

osmosis_pd_cap_t *pd_add_resource(pd_t *pd, gpi_cap_t type, seL4_Word res_id)
{
    osmosis_pd_cap_t *new = calloc(1, sizeof(osmosis_pd_cap_t));
    new->type = type;
    new->res_id = res_id;
    pd->has_access_to_count++;
    HASH_ADD(hh, pd->has_access_to, res_id, sizeof(seL4_Word), new);
    return new;
}

void pd_add_rde(pd_t *pd, rde_type_t type, seL4_CPtr server_ep)
{
    int idx = type.type;
    assert(idx > 0 && idx < MAX_PD_OSM_RDE);

    pd->rde[idx].pd_obj_id = gpi_server_next_pd_id();
    /* we don't really need to keep this if we index by type, but let's just keep it around for now */
    pd->rde[idx].type = type; 
    pd->rde[idx].server_ep = server_ep;

    OSDB_PRINTF("adding new RDE of type %d\n", type.type);
    pd->rde_count++;
}

int pd_new(pd_t *pd,
           vka_t *vka,
           vspace_t *server_vspace,
           simple_t *simple)
{

    OSDB_PRINTF(PDSERVS "new PD: \n");

    pd->has_access_to_count = 0;
    pd->has_access_to = NULL; // required for uthash initialization

    pd->rde_count = 0;
    memset(pd->rde, 0, sizeof(osmosis_rde_t) * MAX_PD_OSM_RDE);

    pd->pd_loaded = false;
    pd->vka = vka;
}

int pd_next_slot(pd_t *pd,
                 seL4_CPtr *next_free_slot)
{

    cspacepath_t path;
    int error = vka_cspace_alloc_path(&pd->pd_vka, &path);
    *next_free_slot = error == seL4_NoError ? path.capPtr : seL4_CapNull;
    return error;
}

int pd_free_slot(pd_t *pd,
                 seL4_CPtr slot)
{
    // First try to delete slot contents,
    // ignore error if slot is already empty
    cspacepath_t path;
    vka_cspace_make_path(&pd->pd_vka, slot, &path);
    vka_cnode_delete(&path);

    /*
    (XXX) Arya: Can't use vka_cspace_free because it tries to identify
    the cap based on the current cspace
    // vka_cspace_free(&pd->pd_vka, slot);
    */
    pd->pd_vka.cspace_free(pd->pd_vka.data, slot);
    return 0;
}

int pd_alloc_ep(pd_t *pd,
                vka_t *server_vka,
                seL4_CPtr *ret_ep)
{
    // alloc slot in pd
    cspacepath_t dest;

    int error = vka_cspace_alloc_path(&pd->pd_vka, &dest);
    if (error)
    {
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
    int error = vka_cspace_alloc_path(&pd->pd_vka, &dest);
    if (error)
    {
        return error;
    }

    error = vka_cnode_mint(&dest,
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
    OSDB_PRINTF(PDSERVS "load_image: loading image %s for pd %p\n", image_path, pd);

    /* There are just setting up the config */
    pd->config = process_config_default_simple(simple, image_path, 255);
    pd->config = process_config_mcp(pd->config, seL4_MaxPrio);
    pd->config = process_config_auth(pd->config, simple_get_tcb(simple));
    pd->config = process_config_create_cnode(pd->config, CSPACE_SIZE_BITS);

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

    /* Initialize a vka for the PD's cspace */
    allocman_t *allocator = bootstrap_create_allocman(PD_ALLOCATOR_STATIC_POOL_SIZE,
                                                      pd->allocator_mem_pool);

    cspace_single_level_t *cspace = malloc(sizeof(cspace_single_level_t));

    error = cspace_single_level_create(allocator, cspace, (struct cspace_single_level_config){.cnode = pd->proc.cspace.cptr, .cnode_size_bits = CSPACE_SIZE_BITS,
                                                                                              //.cnode_guard_bits = seL4_WordBits - pd->cspace_size_bits,
                                                                                              .cnode_guard_bits = 0,
                                                                                              .first_slot = pd->proc.cspace_next_free,
                                                                                              .end_slot = BIT(CSPACE_SIZE_BITS)});
    assert(error == 0);

    error = allocman_attach_cspace(allocator, cspace_single_level_make_interface(cspace));
    assert(error == 0);

    if (allocator == NULL)
    {
        ZF_LOGF("Failed to bootstrap allocator for pd");
    }
    allocman_make_vka(&pd->pd_vka, allocator);

    /* Add the forged MOs*/

    /* set up caps about the process */
    pd->stack_pages = CONFIG_SEL4UTILS_STACK_SIZE / PAGE_SIZE_4K;
    pd->stack = pd->proc.thread.stack_top - CONFIG_SEL4UTILS_STACK_SIZE;
    copy_cap_to_pd(pd, pd->proc.pd.cptr, &pd->page_directory_in_pd);
    pd->root_cnode_in_pd = SEL4UTILS_CNODE_SLOT;
    copy_cap_to_pd(pd, pd->proc.thread.tcb.cptr, &pd->tcb_in_pd);
    // if (config_set(CONFIG_HAVE_TIMER)) {
    //     pd->timer_ntfn = sel4utils_copy_cap_to_process(&(pd->proc), pd->vka, env->timer_notify_test.cptr);
    // }

    /* NOTE:
       The return from sel4utils_copy_cap_to_process is the slot in the cnode where the cap was placed
       in the child process' cspace
    */
    copy_cap_to_pd(pd, simple_get_init_cap(simple, seL4_CapDomain), &pd->domain_in_pd);
    copy_cap_to_pd(pd, simple_get_init_cap(simple, seL4_CapInitThreadASIDPool), &pd->asid_pool_in_pd);
    copy_cap_to_pd(pd, simple_get_init_cap(simple, seL4_CapASIDControl), &pd->asid_ctrl_in_pd);
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
        copy_cap_to_pd(pd, sched_ctrl, &pd->sched_ctrl_in_pd);

        for (int i = 1; i < pd->cores; i++)
        {
            sched_ctrl = simple_get_sched_ctrl(simple, i);
            copy_cap_to_pd(pd, sched_ctrl, NULL);
        }
    }
    /* setup data about untypeds */
    // pd->untypeds = copy_untypeds_to_process(&(pd->proc),
    //                                                env->untypeds,
    //                                                env->num_untypeds,
    //                                                env);
    /* copy the fault endpoint - we wait on the endpoint for a message
     * or a fault to see when the test finishes */
    copy_cap_to_pd(pd, pd->proc.fault_endpoint.cptr, &pd->fault_endpoint_in_pd);

    /* Copy any RDEs that were added before loading
       need to do this before adding of the core resource servers below 
       since we retain references to those slots */
    seL4_Word slot;
    for (int i = 1; i < MAX_PD_OSM_RDE; i++) {
        if (pd->rde[i].type.type != GPICAP_TYPE_NONE) {
            OSDB_PRINTF("copying RDE server %d EP: %lx to target PD\n", pd->rde[i].pd_obj_id, pd->rde[i].server_ep);
            copy_cap_to_pd(pd, pd->rde[i].server_ep, &slot);
        }
    }

    /* These are RDE Entries. */
    seL4_CPtr child_ads_cap_in_parent;
    error = forge_ads_cap_from_vspace(&pd->proc.vspace, pd->vka, &child_ads_cap_in_parent);
    if (error)
    {
        ZF_LOGF("Failed to forge child's as cap");
    }
    copy_cap_to_pd(pd, child_ads_cap_in_parent, &pd->child_ads_cptr_in_child);
    assert(pd->child_ads_cptr_in_child != 0);

    OSDB_PRINTF("copied ads ep at %d\n", (int)pd->child_ads_cptr_in_child);
    rde_type_t ads_rde_type = { .type = GPICAP_TYPE_ADS };
    pd_add_rde(pd, ads_rde_type, pd->child_ads_cptr_in_child);
    pd->rde[GPICAP_TYPE_ADS].slot_in_PD_Debug = pd->child_ads_cptr_in_child;
    pd->rde[GPICAP_TYPE_ADS].slot_in_RT_Debug = child_ads_cap_in_parent;

    // For the GPI server, no need to forge
    seL4_CPtr gpi_endpoint_in_parent = get_gpi_server()->server_ep_obj.cptr;
    copy_cap_to_pd(pd, gpi_endpoint_in_parent, &pd->gpi_endpoint_in_child);
    assert(pd->gpi_endpoint_in_child != 0);
    // (XXX) linh: this shouldn't really be of type MO, but it is how PDs get their MOs
    rde_type_t gpi_rde_type = { .type = GPICAP_TYPE_MO };
    pd_add_rde(pd, gpi_rde_type, pd->gpi_endpoint_in_child); 
    pd->rde[GPICAP_TYPE_MO].slot_in_PD_Debug = pd->gpi_endpoint_in_child;
    pd->rde[GPICAP_TYPE_MO].slot_in_RT_Debug = gpi_endpoint_in_parent;
    OSDB_PRINTF("copied gpi ep at %d\n", (int)pd->gpi_endpoint_in_child);

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
    pd->pd_loaded = true;
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

    seL4_Word new_badge;
    int error = 0;
    cspacepath_t src, dest;
    seL4_CPtr dest_cptr;
    /*
        Find out if the cap is an OSmosis cap or not.
    */
    if (badge)
    {
        // Find the pd from where ths cap came (do we need this info??)

        gpi_cap_t cap_type = get_cap_type_from_badge(badge);
        switch (cap_type)
        {
        case GPICAP_TYPE_ADS:
            // ZF_LOGF("Sending ADS cap is not supported yet");
            new_badge = gpi_new_badge(cap_type,
                                      get_perms_from_badge(badge),
                                      to_pd->pd_obj_id, /* Client ID*/
                                      get_object_id_from_badge(badge));
            // Increment the counter in the mo_t object.
            ads_component_registry_entry_t *ads_reg = ads_component_registry_get_entry_by_badge(badge);
            assert(ads_reg != NULL);

            // Mint a new cap for the child.
            vka_cspace_make_path(get_ads_component()->server_vka,
                                 get_ads_component()->server_ep_obj.cptr, &src);
            vka_cspace_alloc(get_ads_component()->server_vka, &dest_cptr);
            vka_cspace_make_path(get_ads_component()->server_vka, dest_cptr, &dest);

            error = vka_cnode_mint(&dest,
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
            uint32_t idx = to_pd->has_access_to_count++;
            osmosis_pd_cap_t *res = pd_add_resource(to_pd, GPICAP_TYPE_ADS, ads_reg->ads.ads_obj_id);
            res->slot_in_PD_Debug = cap;
            res->slot_in_RT_Debug = src.capPtr;
            res->slot_in_ServerPD_Debug = src.capPtr;
            break;
        case GPICAP_TYPE_MO:
            new_badge = gpi_new_badge(cap_type,
                                      get_perms_from_badge(badge),
                                      to_pd->pd_obj_id, /* Client ID*/
                                      get_object_id_from_badge(badge));
            // Increment the counter in the mo_t object.
            mo_component_registry_entry_t *mo_reg = mo_component_registry_get_entry_by_badge(badge);
            assert(mo_reg != NULL);
            mo_reg->count++;

            // Mint a new cap for the child.
            vka_cspace_make_path(get_mo_component()->server_vka,
                                 get_mo_component()->server_ep_obj.cptr, &src);
            vka_cspace_alloc(get_mo_component()->server_vka, &dest_cptr);
            vka_cspace_make_path(get_mo_component()->server_vka, dest_cptr, &dest);

            error = vka_cnode_mint(&dest,
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
            // ZF_LOGF("Unknown cap type in %s", __FUNCTION__);
            //  (XXX) Arya: allowing unknown cap type for now to send parent ep
            ZF_LOGI("Unknown cap type %d in %s", cap_type, __FUNCTION__);
        }

        // Find the pd where the cap is going, and basd
        // Create a new badge and then badge the unbadged gpi-server cap with the new badge
        // new badge = (old badge & client id mask) | (new client id << client id offset)

        // forge a copy of the cap with the type, perms, and obj id, but different client id
        // Insert it in the appropirate list

        // do the same copy as above
    }
    else
    {
        // This is a cap from the kernel.
        // Just copy it to the child.
    }

    OSDB_PRINTF(PDSERVS "pd_send_cap: copying cap to child: %lu\n", *slot);
    error = copy_cap_to_pd(to_pd, cap, slot);
    if (error != 0)
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
    int error;

    OSDB_PRINTF(PDSERVS "pd_start: ARGS: pd_endpoint_in_root: %ld, arg0: %ld\n",
                pd_endpoint_in_root, arg0);
    // For the PD server, forge and copy
    /* No need to forge, you already have it */
    assert(&pd->proc != NULL);
    assert(&pd->proc.vspace != NULL);

    seL4_CPtr pd_cptr_in_child;
    error = copy_cap_to_pd(pd, pd_endpoint_in_root, &pd_cptr_in_child);
    if (error != 0)
    {
        ZF_LOGF("Failed to copy PD cap to process");
        return -1;
    }

    rde_type_t pd_rde_type = { .type = GPICAP_TYPE_PD };
    pd_add_rde(pd, pd_rde_type, pd_cptr_in_child);

    vka_object_t rde_frame_parent;
    error = vka_alloc_frame(vka, seL4_PageBits, &rde_frame_parent);
    if (error)
    {
        ZF_LOGE("Couldn't allocate frame to hold PD's resource directory");
    }
    
    seL4_CPtr rde_parent_cap = rde_frame_parent.cptr;
    void *rde_parent = vspace_map_pages(server_vspace, &rde_parent_cap, NULL, seL4_AllRights, 1, seL4_PageBits, 1);
    memcpy(rde_parent, pd->rde, sizeof(osmosis_rde_t) * MAX_PD_OSM_RDE);

    seL4_CPtr rde_mo_cap;
    mo_t *rde_mo_obj;
    error = forge_mo_cap_from_frames(&rde_parent_cap, 1, vka, &rde_mo_cap, &rde_mo_obj);
    if (error)
    {
        ZF_LOGE("Couldn't forge an MO for PD's resource directory");
    }
    copy_cap_to_pd(pd, rde_mo_cap, &pd->pd_rde_in_child);
    OSDB_PRINTF("copied PD's resource directory cap at %lx\n", pd->pd_rde_in_child);

    // Phase1: Start it.
    // Phase2: start the CPU thread.

    /* set up args for the test process */
    seL4_Word argc = 1;
    char string_args[argc][WORD_STRING_SIZE];
    char *argv[argc];
    sel4utils_create_word_args(string_args, argv, argc, arg0);

    // argc = 1;
    // snprintf(argv[0], WORD_STRING_SIZE, "%ld", arg0);

    /* spawn the process */
    seL4_CPtr osm_caps[] = {pd->child_ads_cptr_in_child,
                            pd->pd_rde_in_child};
    error = sel4utils_osm_spawn_process_v(&(pd->proc),
                                          osm_caps,
                                          pd->vka,
                                          server_vspace,
                                          argc,
                                          argv,
                                          1);
    ZF_LOGF_IF(error != 0, "Failed to start test process!");
    OSDB_PRINTF(PDSERVS "pd_start: starting PD\n");
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
        case GPICAP_TYPE_NONE:
            break;
        case GPICAP_TYPE_ADS:
            char res_id[CSV_MAX_STRING_SIZE];
            snprintf(res_id, 20, "ADS_%lu", pd->has_access_to[idx].res_id);
            add_has_access_to(ms,
                              pd_id,
                              res_id,
                              "true");
            ads_component_registry_entry_t *ads_data =
                ads_component_registry_get_entry_by_id(pd->has_access_to[idx].res_id);
            assert(ads_data != NULL);
            ads_dump_rr(&ads_data->ads, ms);
            add_has_access_to(ms,
                              pd_id,
                              res_id,
                              // (XXX): We need to find out which ads is active and print true only those ADSs
                              // When TRUE it shows that this ads is in use by some TCB.
                              // We specifically add this to handle the scenario where a PD can have mutliple ads, but only one of them is in use.
                              // Think LWC.
                              "true");
            break;
        case GPICAP_TYPE_MO:
            break;
        case GPICAP_TYPE_CPU:
            break;
        case GPICAP_TYPE_seL4:
            // Use some other method to get the cap details
            break;
        default:
            ZF_LOGF("Calling anothe PD to get the info %s", __FUNCTION__);
            break;
        }
    }

    print_model_state(ms);
    free(ms);
    /* Print RDE Info*/
    for (int idx = 0; idx < MAX_PD_OSM_RDE; idx++)
    {
        print_pd_osm_rde_info(&pd->rde[idx]);
    }

    return 0;
}

inline void print_pd_osm_cap_info(osmosis_pd_cap_t *o)
{
    printf("Slot_RT:%lx\t Slot_PD: %lx\t Slot_ServerPD: %lx\t T: %s\n",
           o->slot_in_RT_Debug,
           o->slot_in_PD_Debug,
           o->slot_in_ServerPD_Debug,
           cap_type_to_str(o->type));
}

inline void print_pd_osm_rde_info(osmosis_rde_t *o)
{
    printf("RDE: PD_ID: %u\t Slot_RT:%lu\t Slot_PD: %lu\t T: %s\n",
           o->pd_obj_id,
           o->slot_in_RT_Debug,
           o->slot_in_PD_Debug,
           cap_type_to_str(o->type.type));
}
