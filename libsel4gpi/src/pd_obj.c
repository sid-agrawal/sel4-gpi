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
#include <sel4gpi/cpu_component.h>
#include <sel4gpi/cap_tracking.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/ads_obj.h>
#include <sel4gpi/cpu_obj.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/resource_server_utils.h>
// #include <sel4gpi/gpi_rde.h>

#include <vka/capops.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <simple/simple_helpers.h>
#include <utils/uthash.h>

#define CSPACE_SIZE_BITS 17

int copy_cap_to_pd(pd_t *to_pd,
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

osmosis_pd_cap_t *pd_add_resource(pd_t *pd, gpi_cap_t type, uint32_t res_id,
                                  seL4_CPtr slot_in_RT, seL4_CPtr slot_in_PD, seL4_CPtr slot_in_serverPD)
{
    osmosis_pd_cap_t *new = calloc(1, sizeof(osmosis_pd_cap_t));
    new->type = type;
    new->res_id = res_id;
    new->slot_in_RT_Debug = slot_in_RT;
    new->slot_in_PD_Debug = slot_in_PD;
    new->slot_in_ServerPD_Debug = slot_in_serverPD;
    pd->has_access_to_count++;
    HASH_ADD(hh, pd->has_access_to, res_id, sizeof(uint32_t), new);
    return new;
}

int pd_add_rde(pd_t *pd,
               rde_type_t type,
               uint32_t manager_id,
               uint32_t ns_id,
               seL4_CPtr server_ep)
{
    int idx;

    if (ns_id == 0)
    {
        idx = type.type;
    }
    else
    {
        int start = GPICAP_TYPE_MAX + (MAX_NS_PER_RDE * (type.type - 1)); // -1 since we don't have ns's for the none type

        assert(start > 0 && start < MAX_PD_OSM_RDE);
        int i;
        for (i = start; i < start + MAX_NS_PER_RDE; i++)
        {
            if (pd->init_data->rde[i].type.type == GPICAP_TYPE_NONE)
            {
                idx = i;
                break;
            }
        }

        if (i >= start + MAX_NS_PER_RDE)
        {
            OSDB_PRINTF("No more RDE NS slots available for type %d\n", type.type);
            return 1;
        }
    }

    assert(idx > 0 && idx < MAX_PD_OSM_RDE);

    pd->init_data->rde[idx].manager_id = manager_id;
    /* we don't really need to keep this if we index by type, but let's just keep it around for now */
    pd->init_data->rde[idx].type = type;
    pd->init_data->rde[idx].slot_in_RT = server_ep;
    pd->init_data->rde[idx].ns_id = ns_id;
    uint32_t client_id = pd->pd_obj_id;

    // Badge the raw endpoint for the client PD
    cspacepath_t src, dest;
    vka_cspace_make_path(pd->vka, server_ep, &src);

    int error = vka_cspace_alloc_path(&pd->pd_vka, &dest);
    if (error)
    {
        return error;
    }

    seL4_Word badge_val = gpi_new_badge(type.type,
                                        0x00,
                                        client_id,
                                        ns_id,
                                        BADGE_OBJ_ID_NULL);

    error = vka_cnode_mint(&dest,
                           &src,
                           seL4_AllRights,
                           badge_val);
    if (error)
    {
        return error;
    }

    pd->init_data->rde[idx].slot_in_PD = dest.capPtr;

    OSDB_PRINTF("Added new RDE of type %d to PD %d, in slot %d\n", type.type, client_id, (int)dest.capPtr);

    pd->init_data->rde_count++;
    return 0;
}

int pd_new(pd_t *pd,
           vka_t *server_vka,
           vspace_t *server_vspace)
{
    int error;

    OSDB_PRINTF(PDSERVS "new PD: \n");

    pd->has_access_to_count = 0;
    pd->has_access_to = NULL; // required for uthash initialization
    pd->pd_started = false;
    pd->vka = server_vka;
    pd->vspace = server_vspace;

    // Create the MO for the PD's init data
    vka_object_t frame;
    error = vka_alloc_frame(server_vka, seL4_PageBits, &frame);
    if (error)
    {
        ZF_LOGE("Couldn't allocate frame to hold PD's init data\n");
    }
    pd->init_data_frame = frame.cptr;
    pd->init_data = (osm_pd_init_data_t *)vspace_map_pages(server_vspace, &frame.cptr, NULL, seL4_AllRights, 1, seL4_PageBits, 1);

    mo_t *rde_mo_obj;
    error = forge_mo_cap_from_frames(&frame.cptr, 1, server_vka, pd->pd_obj_id,
                                     &pd->init_data_mo.badged_server_ep_cspath.capPtr, &rde_mo_obj);
    pd->init_data_mo_id = rde_mo_obj->mo_obj_id;

    if (error)
    {
        ZF_LOGE("Couldn't forge an MO for PD's init data\n");
    }

    // Setup init data
    pd->init_data->rde_count = 0;
    memset(pd->init_data->rde, 0, sizeof(osmosis_rde_t) * MAX_PD_OSM_RDE);
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

int pd_mint(pd_t *pd,
            cspacepath_t *src,
            seL4_Word badge,
            seL4_CPtr *ret)
{
    cspacepath_t dest;

    int error = vka_cspace_alloc_path(&pd->pd_vka, &dest);
    // int error = vka_cspace_alloc_path(get_pd_component()->server_vka, &dest);
    if (error)
    {
        return error;
    }

    error = vka_cnode_mint(&dest,
                           src,
                           seL4_AllRights,
                           badge);

    *ret = error == seL4_NoError ? dest.capPtr : seL4_CapNull;
    return error;
}

int pd_badge_ep(pd_t *pd,
                seL4_CPtr src_ep,
                seL4_Word badge,
                seL4_CPtr *ret_ep)
{
    cspacepath_t src;
    vka_cspace_make_path(&pd->pd_vka, src_ep, &src);

    return pd_mint(pd, &src, badge, ret_ep);
}

int pd_bootstrap_allocator(pd_t *pd,
                           seL4_CPtr root,
                           size_t start_slot,
                           size_t end_slot,
                           size_t size_bits,
                           size_t guard_bits)
{
    int error;
    allocman_t *allocator = bootstrap_create_allocman(PD_ALLOCATOR_STATIC_POOL_SIZE,
                                                      pd->allocator_mem_pool);

    cspace_single_level_t *cspace = malloc(sizeof(cspace_single_level_t));

    error = cspace_single_level_create(allocator, cspace, (struct cspace_single_level_config){.cnode = root, .cnode_size_bits = size_bits,
                                                                                              //.cnode_guard_bits = seL4_WordBits - pd->cspace_size_bits,
                                                                                              .cnode_guard_bits = guard_bits,
                                                                                              .first_slot = start_slot,
                                                                                              .end_slot = end_slot});
    if (error != seL4_NoError)
    {
        OSDB_PRINTF(PDSERVS "%s: Failed to initialize single-level cspace for PD id %d.\n",
                    __FUNCTION__, pd->pd_obj_id);
        return -1;
    }

    error = allocman_attach_cspace(allocator, cspace_single_level_make_interface(cspace));
    if (error != seL4_NoError)
    {
        OSDB_PRINTF(PDSERVS "%s: Failed to attach cspace to allocman for PD id %d.\n",
                    __FUNCTION__, pd->pd_obj_id);
        return -1;
    }

    allocman_make_vka(&pd->pd_vka, allocator);
    return 0;
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

#if CONFIG_MAX_NUM_NODES > 1
    seL4_Error syserr = seL4_TCB_SetAffinity(pd->proc.thread.tcb.cptr, 1);
    ZF_LOGE_IFERR(syserr, "Failed to set TCB Affinity");
#endif // CONFIG_MAX_NUM_NODES > 1

    /* Initialize a vka for the PD's cspace */
    error = pd_bootstrap_allocator(pd, pd->proc.cspace.cptr, pd->proc.cspace_next_free,
                                   BIT(CSPACE_SIZE_BITS), CSPACE_SIZE_BITS, 0);
    assert(error == 0);

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

    // the ADS cap is both a resource manager and a resource
    // (XXX) Arya: Pending re-design, the ADS component serves virtual mem to an ADS
    error = forge_ads_cap_from_vspace(&pd->proc.vspace, pd->vka, pd->pd_obj_id, &pd->ads_cap_in_RT, &pd->ads_obj_id);
    ZF_LOGF_IFERR(error, "Failed to forge child's as cap");

    // Send the ADS cap as a resource
    seL4_Word badge = gpi_new_badge(GPICAP_TYPE_ADS, 0x00, pd->pd_obj_id, pd->ads_obj_id, pd->ads_obj_id);
    error = pd_send_cap(pd, pd->ads_cap_in_RT, badge, &pd->init_data->ads_cap);
    ZF_LOGF_IFERR(error, "Failed to send ADS resource cap to PD");

    // the ADS cap also acts an as RDE, however since its object ID is set, a PD can never
    // make a new ADS from this EP
    rde_type_t ads_rde_type = {.type = GPICAP_TYPE_ADS};
    error = pd_add_rde(pd, ads_rde_type, get_gpi_server()->ads_manager_id, pd->ads_obj_id, get_ads_component()->server_ep_obj.cptr);
    ZF_LOGE_IFERR(error, "Failed to add ADS RDE to PD");
    pd->init_data->binded_ads_ns_id = pd->ads_obj_id;

    // Send CPU cap as a resource
    uint32_t cpu_obj_id;
    error = forge_cpu_cap_from_tcb(&pd->proc, pd->vka, pd->pd_obj_id, &pd->cpu_cap_in_RT, &cpu_obj_id);
    badge = gpi_new_badge(GPICAP_TYPE_CPU, 0x00, pd->pd_obj_id, 0x00, cpu_obj_id);
    pd_send_cap(pd, pd->cpu_cap_in_RT, badge, &pd->init_data->cpu_cap);
    ZF_LOGF_IFERR(error, "Failed to send CPU cap to PD");

    /* set up free slot range */
    pd->cspace_size_bits = pd->proc.cspace_size;

    printf("%s: %d\n", __FUNCTION__, __LINE__);
    uint32_t num_mo_caps = 0;
    seL4_CPtr mo_caps[MAX_MO_CHILD];
    printf("%s: %d\n", __FUNCTION__, __LINE__);
    error = forge_mo_caps_from_vspace(target_vspace,
                                      pd->vka,
                                      pd->pd_obj_id,
                                      &num_mo_caps,
                                      mo_caps);
    assert(error == 0);

    memcpy(&pd->proc.vspace, target_vspace, sizeof(vspace_t));

    pd->free_slots.start = pd->proc.cspace_next_free;
    OSDB_PRINTF("%s:%d: free_slot.start %ld\n", __FUNCTION__, __LINE__, pd->free_slots.start);

    pd->free_slots.end = (1u << pd->cspace_size_bits);
    assert(pd->free_slots.start < pd->free_slots.end);
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
    osmosis_pd_cap_t *res;
    bool should_mint = true;
    /*
        Find out if the cap is an OSmosis cap or not.
    */
    if (badge)
    {
        gpi_cap_t cap_type = get_cap_type_from_badge(badge);
        uint32_t res_id;
        vka_t *server_vka;
        seL4_CPtr server_src_cap;

        switch (cap_type)
        {
        case GPICAP_TYPE_ADS:
            server_vka = get_ads_component()->server_vka;
            server_src_cap = get_ads_component()->server_ep_obj.cptr;

            ads_component_registry_entry_t *ads_reg = ads_component_registry_get_entry_by_badge(badge);
            assert(ads_reg != NULL);

            res_id = ads_reg->ads.ads_obj_id;
            break;
        case GPICAP_TYPE_MO:
            server_vka = get_mo_component()->server_vka;
            server_src_cap = get_mo_component()->server_ep_obj.cptr;
            // Increment the counter in the mo_t object.
            mo_component_registry_entry_t *mo_reg = mo_component_registry_get_entry_by_badge(badge);
            assert(mo_reg != NULL);
            mo_reg->count++;

            res_id = mo_reg->mo.mo_obj_id;
            break;
        case GPICAP_TYPE_CPU:
            server_vka = get_cpu_component()->server_vka;
            server_src_cap = get_cpu_component()->server_ep_obj.cptr;

            cpu_component_registry_entry_t *cpu_reg = cpu_component_registry_get_entry_by_badge(badge);
            assert(cpu_reg != NULL);

            res_id = cpu_reg->cpu.cpu_obj_id;
            break;
        case GPICAP_TYPE_PD:
            ZF_LOGF("Sending PD cap is not supported yet");
            break;
        default:
            // ZF_LOGF("Unknown cap type in %s", __FUNCTION__);
            //  (XXX) Arya: allowing unknown cap type for now to send parent ep
            ZF_LOGE("Unknown cap type %d in %s", cap_type, __FUNCTION__);
            should_mint = false;
            break;
        }

        // Find the pd where the cap is going, and basd
        // Create a new badge and then badge the unbadged gpi-server cap with the new badge
        // new badge = (old badge & client id mask) | (new client id << client id offset)

        // forge a copy of the cap with the type, perms, and obj id, but different client id
        // Insert it in the appropirate list
        if (should_mint) // (XXX) remove this once we stop sending non-osmosis caps through here
        {
            new_badge = gpi_new_badge(cap_type,
                                      get_perms_from_badge(badge),
                                      to_pd->pd_obj_id, /* Client ID */
                                      get_ns_id_from_badge(badge),
                                      get_object_id_from_badge(badge));

            vka_cspace_make_path(server_vka, server_src_cap, &src);
            vka_cspace_alloc(server_vka, &dest_cptr);
            vka_cspace_make_path(server_vka, dest_cptr, &dest);

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
            pd_add_resource(to_pd, cap_type, res_id, src.capPtr, cap, src.capPtr);
        }
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
             int argc,
             seL4_Word *args)
{
    int error;

    OSDB_PRINTF(PDSERVS "pd_start: ARGS: pd_endpoint_in_root: %ld, argc: %d\n",
                pd_endpoint_in_root, argc);
    assert(&pd->proc != NULL);
    assert(&pd->proc.vspace != NULL);

    // Send the PD's PD resource (XXX) Arya: replace with pd_send_cap
    error = copy_cap_to_pd(pd, pd_endpoint_in_root, &pd->init_data->pd_cap);
    if (error)
    {
        ZF_LOGF("Failed to send PD cap to child");
    }

    // Map init data to the PD
    error = ads_component_attach(pd->ads_obj_id, pd->init_data_mo_id, NULL, (void **)&pd->init_data_in_PD);
    if (error)
    {
        ZF_LOGF("Failed to attach init data to child PD");
    }
    OSDB_PRINTF("Mapped PD's init data at %p\n", (void *)pd->init_data_in_PD);

    // Phase1: Start it.
    // Phase2: start the CPU thread.

    /* set up string args for the process */
    char string_args[argc][WORD_STRING_SIZE];
    char *argv[argc];

    for (int i = 0; i < argc; i++)
    {
        argv[i] = string_args[i];
        snprintf(argv[i], WORD_STRING_SIZE, "%" PRIuPTR "", args[i]);
    }

    OSDB_PRINTF("Starting PD with string args: [", argc);
    for (int i = 0; i < argc; i++)
    {
        OSDB_PRINTF("%s, ", string_args[i]);
    }
    OSDB_PRINTF("]\n");

    /* spawn the process */
    OSDB_PRINTF(PDSERVS "pd_start: starting PD\n");
    error = sel4utils_osm_spawn_process_v(&(pd->proc),
                                          (void *)pd->init_data_in_PD,
                                          pd->vka,
                                          server_vspace,
                                          argc,
                                          argv,
                                          1);
    ZF_LOGF_IF(error != 0, "Failed to start test process!");

    pd->pd_started = true;

    return 0;
}

int pd_dump(pd_t *pd)
{
    int error;

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
    make_res_id(pd_id, GPICAP_TYPE_PD, pd->pd_obj_id);
    add_pd(ms, pd_name, pd_id);

    /* Allocate memory for remote rr requests */
    vka_object_t rr_frame_obj;
    error = vka_alloc_frame(pd->vka, seL4_PageBits, &rr_frame_obj);
    if (error != seL4_NoError)
    {
        return error;
    }

    cspacepath_t rr_frame_path;
    vka_cspace_make_path(pd->vka, rr_frame_obj.cptr, &rr_frame_path);

    void *rr_local_vaddr = vspace_map_pages(get_pd_component()->server_vspace, &rr_frame_obj.cptr, NULL,
                                            seL4_AllRights, 1, seL4_PageBits, 1);
    if (rr_local_vaddr == NULL)
    {
        return -1;
    }

    /*
    (XXX) Arya: not able to use MO for remote rr request
    mo_client_context_t mo_conn;
    mo_t *mo;
    error = forge_mo_cap_from_frames(&rr_frame_obj.cptr, 1, pd->vka,
                                     &mo_conn.badged_server_ep_cspath.capPtr, &mo);
    if (error != seL4_NoError)
    {
        return error;
    }
    */

    // char rde_id[CSV_MAX_STRING_SIZE];
    char rde_name[CSV_MAX_STRING_SIZE];
    char rm_id[CSV_MAX_STRING_SIZE];
    // uint32_t added_pd_rr[MAX_PD_OSM_RDE] = {0};
    // memset(added_pd_rr, -1, sizeof(uint32_t) * MAX_PD_OSM_RDE);

    for (int i = 0; i < MAX_PD_OSM_RDE; i++)
    {
        osmosis_rde_t rde = pd->init_data->rde[i];

        if (rde.type.type != GPICAP_TYPE_NONE)
        {
            pd_component_resource_manager_entry_t *rm = pd_component_resource_manager_get_entry_by_id(rde.manager_id);

            if (rm == NULL)
            {
                ZF_LOGF("Couldn't find resource manager with ID %d\n", rde.manager_id);
            }

            snprintf(rm_id, CSV_MAX_STRING_SIZE, "RM_%d", rm->manager_id);
            snprintf(rde_name, CSV_MAX_STRING_SIZE, "%s_Server", cap_type_to_str(rde.type.type));
            add_pd(ms, rde_name, rm_id);       // (XXX) Linh: placeholder before we implement dumping of RDE PDs
            add_pd_requests(ms, pd_id, rm_id); // rm_id should always be unique
        }
    }

    /* add a special resource for the RT */
    char root_task_id[CSV_MAX_STRING_SIZE];
    make_res_id(root_task_id, GPICAP_TYPE_PD, 0);
    add_resource(ms, "All", "RT_ALL");
    add_has_access_to(ms, root_task_id, "RT_ALL", true);
    add_pd(ms, "ROOT TASK", root_task_id);

    char res_id[CSV_MAX_STRING_SIZE];
    for (osmosis_pd_cap_t *current_cap = pd->has_access_to; current_cap != NULL; current_cap = current_cap->hh.next)
    {
        print_pd_osm_cap_info(current_cap);
        // if type seL4 cap
        //  print_pd_osm_cap_info(&current_cap);
        //  else if type osmosis cap
        //  get the RR for that cap
        make_res_id(res_id, current_cap->type, current_cap->res_id);
        switch (current_cap->type)
        {
        case GPICAP_TYPE_NONE:
            break;
        case GPICAP_TYPE_ADS:
            ads_component_registry_entry_t *ads_data =
                ads_component_registry_get_entry_by_id(current_cap->res_id);
            assert(ads_data != NULL);
            ads_dump_rr(&ads_data->ads, ms);
            add_has_access_to(ms,
                              pd_id,
                              res_id,
                              // (XXX): We need to find out which ads is active and print true only those ADSs
                              // When TRUE it shows that this ads is in use by some TCB.
                              // We specifically add this to handle the scenario where a PD can have mutliple ads, but only one of them is in use.
                              // Think LWC.
                              true);
            break;
        case GPICAP_TYPE_MO:
            mo_component_registry_entry_t *mo_data = mo_component_registry_get_entry_by_id(current_cap->res_id);
            assert(mo_data != NULL);
            mo_dump_rr(&mo_data->mo, ms);
            add_has_access_to(ms, pd_id, res_id, false);
            break;
        case GPICAP_TYPE_CPU:
            cpu_component_registry_entry_t *cpu_data = cpu_component_registry_get_entry_by_id(current_cap->res_id);
            assert(cpu_data != NULL);
            cpu_dump_rr(&cpu_data->cpu, ms);
            add_has_access_to(ms, pd_id, res_id, false);
            break;
        case GPICAP_TYPE_seL4:
            // Use some other method to get the cap details
            break;
        case GPICAP_TYPE_FILE:
        case GPICAP_TYPE_BLOCK:
            OSDB_PRINTF(PDSERVS "Calling another PD to get the info for resource with ID 0x%x\n", current_cap->res_id);

            // Find the server that created this resource based on the resource id
            uint64_t obj_id = current_cap->res_id;
            uint64_t server_id = get_server_id_from_badge(obj_id);
            pd_component_resource_manager_entry_t *server_entry = pd_component_resource_manager_get_entry_by_id(server_id);

            if (server_entry == NULL)
            {
                OSDB_PRINTF(PDSERVS "Failed to find resource server with ID 0x%lx\n", server_id);
                return -1;
            }
            seL4_CPtr server_cap = server_entry->server_ep;
            rr_state_t *rs;

            OSDB_PRINTF(PDSERVS "Resource ID 0x%lx, server ID 0x%lx, server EP at %d\n", obj_id, server_id, (int)server_cap);

            // Pre-map the memory so resource server does not need to call root task
            cspacepath_t rr_frame_copy_path;
            int error = vka_cspace_alloc_path(pd->vka, &rr_frame_copy_path);
            if (error != seL4_NoError)
            {
                OSDB_PRINTF(PDSERVS "Failed to allocate path for RR frame copy %d", error);
                return -1;
            }

            error = vka_cnode_copy(&rr_frame_copy_path, &rr_frame_path, seL4_AllRights);
            if (error != seL4_NoError)
            {
                OSDB_PRINTF(ADSSERVS "Failed to copy RR frame cap cap, error: %d", error);
                return -1;
            }

            void *rr_remote_vaddr = vspace_map_pages(&server_entry->pd->proc.vspace, &rr_frame_copy_path.capPtr, NULL,
                                                     seL4_AllRights, 1, seL4_PageBits, 1);
            if (rr_remote_vaddr == NULL)
            {
                OSDB_PRINTF(PDSERVS "Failed to map RR frame to resource server, %d", error);
                return -1;
            }

            // Get RR from remote resource server
            error = resource_server_get_rr(server_cap, obj_id,
                                           rr_remote_vaddr, rr_local_vaddr,
                                           SIZE_BITS_TO_BYTES(seL4_PageBits), &rs);
            if (error == RS_ERROR_DNE)
            {
                // The resource was deleted and the PD component didn't know
                // (XXX) Arya: Eventually, the PD component should be told
                // For now, just omit deleted resources from the model state

                // Remove remotely-mapped memory
                vspace_unmap_pages(&server_entry->pd->proc.vspace, rr_remote_vaddr, 1, seL4_PageBits, NULL);
                vka_cnode_delete(&rr_frame_copy_path);

                continue;
            }
            if (error == RS_ERROR_RR_SIZE)
            {
                // (XXX) Arya: Need to allocate a bigger shared memory if this fails
                return error;
            }
            else if (error != seL4_NoError)
            {
                return error;
            }

            combine_model_states(ms, rs);

            // Add the has_access_to row
            add_has_access_to(ms,
                              pd_id,
                              res_id,
                              false); // (XXX) Arya: how to determine is_mapped

            // Remove remotely-mapped memory
            vspace_unmap_pages(&server_entry->pd->proc.vspace, rr_remote_vaddr, 1, seL4_PageBits, NULL);
            vka_cnode_delete(&rr_frame_copy_path);

            break;
        case GPICAP_TYPE_PD:
            if (current_cap->res_id != pd->pd_obj_id)
            {
                pd_component_registry_entry_t *pd_data = pd_component_registry_get_entry_by_id(current_cap->res_id);
                assert(pd_data != NULL);
                pd_dump(&pd_data->pd);
            }
            // add_has_access_to(ms, pd_id, res_id, false);
            break;
        default:
            ZF_LOGE("Invalid has_access_to cap type 0x%x", current_cap->type);
            break;
        }
    }

    /* Free the frame used for rr requests */
    // (XXX) Arya: Again, unmapping this will cause future failures
    // vspace_unmap_pages(get_pd_component()->server_vspace, rr_local_vaddr, 1, seL4_PageBits, pd->vka);

    print_model_state(ms);
    free(ms);
    /* Print RDE Info*/
    for (int idx = 0; idx < MAX_PD_OSM_RDE; idx++)
    {
        print_pd_osm_rde_info(&pd->init_data->rde[idx]);
    }

    return 0;
}

inline void print_pd_osm_cap_info(osmosis_pd_cap_t *o)
{
    printf("Resource_ID: %d Slot_RT:%lx\t Slot_PD: %lx\t Slot_ServerPD: %lx\t T: %s\n",
           o->res_id,
           o->slot_in_RT_Debug,
           o->slot_in_PD_Debug,
           o->slot_in_ServerPD_Debug,
           cap_type_to_str(o->type));
}

inline void print_pd_osm_rde_info(osmosis_rde_t *o)
{
    printf("RDE: PD_ID: %u\t Slot_RT:%lu\t Slot_PD: %lu\t T: %s\n",
           o->pd_obj_id,
           o->slot_in_RT,
           o->slot_in_PD,
           cap_type_to_str(o->type.type));
}
