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
#include <sel4utils/process.h>
#include <stdio.h>

#include <vka/capops.h>

#include <sel4gpi/ads_obj.h>
#include <sel4gpi/ads_component.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/cap_tracking.h>
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/model_exporting.h>

#define MAX_MO_RR 10000

int ads_new(vspace_t *loader,
            vka_t *vka,
            ads_t *ret_ads)
{

    ret_ads->vspace = malloc(sizeof(vspace_t));
    if (ret_ads->vspace == NULL)
    {
        ZF_LOGE("Failed to allocate vspace\n");
        goto error_exit;
    }
    ret_ads->process_for_cookies = malloc(sizeof(sel4utils_process_t));
    if (ret_ads->process_for_cookies == NULL)
    {
        ZF_LOGE("Failed to allocate process struct for coolies in ads_new\n");
        goto error_exit;
    }
    vspace_t *new_vspace = ret_ads->vspace;
    assert(new_vspace != NULL);

    // Give vspace root
    // assign asid pool
    vka_object_t *vspace_root_object = malloc(sizeof(vka_object_t));
    assert(vspace_root_object != NULL);

    sel4utils_alloc_data_t *alloc_data = malloc(sizeof(sel4utils_alloc_data_t));
    if (alloc_data == NULL)
    {
        ZF_LOGE("Failed to allocate memory for alloc data\n");
        goto error_exit;
    }

    int error = vka_alloc_vspace_root(vka, vspace_root_object);
    if (error)
    {
        ZF_LOGE("Failed to allocate page directory for new process: %d\n", error);
        goto error_exit;
    }

    /* assign an asid pool */
    if (!config_set(CONFIG_X86_64) &&
        assign_asid_pool(seL4_CapInitThreadASIDPool, vspace_root_object->cptr) != seL4_NoError)
    {
        goto error_exit;
    }
    // Create empty vspace
    error = sel4utils_get_vspace(
        loader,
        new_vspace,
        alloc_data,
        vka,
        vspace_root_object->cptr,
        sel4utils_allocated_object,

        /*
            sel4utils_allocated_object expects a process struct as a cookie
            Instead use a different function which suited are needs better.
        */

        &(ret_ads->process_for_cookies));
    if (error)
    {
        ZF_LOGE("Failed to get new vspace while making copy: %d\n in %s", error, __FUNCTION__);
        goto error_exit;
    }
    ret_ads->root_page_dir = vspace_root_object;

    return 0;

error_exit:
    free(alloc_data);
    free(ret_ads->vspace);
    return -1;
}
int ads_attach(ads_t *ads,
               vka_t *vka,
               void *vaddr,
               uint32_t num_pages,
               seL4_CPtr *frame_caps,
               void **ret_vaddr,
               /*sel4utils_process_t*/ vspace_t *process_cookie)
{
    int cacheable = 1;
    vspace_t *target = ads->vspace;

    /* Reserver the range in the vspace */
    seL4_CapRights_t rights = seL4_AllRights;
    reservation_t res;
    if (vaddr == NULL)
    {
        res = sel4utils_reserve_range_aligned(target,
                                              num_pages * PAGE_SIZE_4K,
                                              seL4_PageBits,
                                              rights,
                                              cacheable,
                                              &vaddr);
    }
    else
    {
        res = sel4utils_reserve_range_at(target,
                                         vaddr,
                                         num_pages * PAGE_SIZE_4K,
                                         rights, cacheable);
    }
    if (res.res == NULL)
    {
        ZF_LOGE("Failed to reserve range\n");
        return 1;
    }

    /* Map the frame cap into the vspace */

    size_t size_bits = seL4_PageBits;
    int error = sel4utils_map_pages_at_vaddr(target,
                                             frame_caps,
                                             (uintptr_t *)process_cookie, // TODO: this is a hack
                                             vaddr,
                                             num_pages,
                                             size_bits,
                                             res);
    if (error)
    {
        ZF_LOGE("Failed to map pages\n");
        return 1;
    }
    *ret_vaddr = vaddr;
    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "attached %u pages at %p\n", num_pages, *ret_vaddr);

    return 0;
}

int ads_rm(ads_t *ads, vka_t *vka, void *vaddr, size_t size)
{
    return 0;
}

int ads_bind(ads_t *ads, vka_t *vka, seL4_CPtr *cpu_cap)
{
    return 0;
}

static char *ads_res_type_to_str(sel4utils_reservation_type_t type)
{
    switch (type)
    {
    case SEL4UTILS_RES_TYPE_ELF:
        return "ELF";
    case SEL4UTILS_RES_TYPE_STACK:
        return "STACK";
    case SEL4UTILS_RES_TYPE_IPC_BUF:
        return "IPC_BUFFER";
    case SEL4UTILS_RES_TYPE_HEAP:
        return "HEAP";
    case SEL4UTILS_RES_TYPE_SHARED_FRAMES:
        return "SHARED_FRAMES";
    case SEL4UTILS_RES_TYPE_OTHER:
        return "OTHER";
    default:
        return "UNKNOWN";
    }
}

void ads_dump_rr(ads_t *ads, model_state_t *ms)
{
    char ads_res_id[CSV_MAX_STRING_SIZE];
    make_res_id(ads_res_id, GPICAP_TYPE_ADS, ads->ads_obj_id);
    add_resource(ms, cap_type_to_str(GPICAP_TYPE_ADS), ads_res_id);

    uint32_t added_mo_rrs[MAX_MO_RR];
    int num_added_mo_rrs = 0;
    bool skip = false;
    for (attach_node_t *res = ads->attach_nodes; res != NULL; res = res->next)
    {
        // Enable this to skip extra PMRs on same MO
        #if 0
        for (int i = 0; i < num_added_mo_rrs; i++)
        {
            if (added_mo_rrs[i] == res->mo_id)
            {
                skip = true;
                break;
            }
        }

        if (skip)
        {
            continue;
        }
        #endif

        added_mo_rrs[num_added_mo_rrs] = res->mo_id;
        num_added_mo_rrs++;
        char vmr_res_id[CSV_MAX_STRING_SIZE];
        make_virtual_res_id(vmr_res_id, ads->ads_obj_id, (uint64_t)res->vaddr, "VMR");
        add_resource_2(ms, "VMR", vmr_res_id, ads_res_type_to_str(res->type));
        add_resource_depends_on(ms, vmr_res_id, ads_res_id, REL_TYPE_SUBSET);

        char mo_res_id[CSV_MAX_STRING_SIZE];
        make_res_id(mo_res_id, GPICAP_TYPE_MO, res->mo_id);
        add_resource_depends_on(ms, vmr_res_id, mo_res_id, REL_TYPE_MAP);
    }
#if 0
    vspace_t *ads_vspace = ads->vspace;

    /* Dump the info */
    sel4utils_res_t *from_sel4_res = get_alloc_data(ads_vspace)->reservation_head;
    assert(from_sel4_res != NULL);

    vka_t *vka = get_alloc_data(ads_vspace)->vka;

    assert(vka != NULL);
    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "vka address: %p\n", vka);

    while (from_sel4_res != NULL)
    {
        char res_type[CSV_MAX_STRING_SIZE];
        char res_id[CSV_MAX_STRING_SIZE];
        snprintf(res_type, CSV_MAX_STRING_SIZE, "%s", human_readable_va_res_type(from_sel4_res->type));
        snprintf(res_id, CSV_MAX_STRING_SIZE, "%u_%lx_%lx",
                 ads->ads_obj_id, from_sel4_res->start,
                 from_sel4_res->end);
        add_resource(ms, res_type, res_id);
        add_resource_depends_on(ms, ads_res_id, res_id);

        /* Print all the caps of this reservation */
        void *va = (void *)from_sel4_res->start;
        for (void *start = (void *)from_sel4_res->start;
             start < (void *)from_sel4_res->end;
             start += PAGE_SIZE_4K)
        {
            seL4_CPtr cap = vspace_get_cap(ads_vspace, start);
            if (cap == 0)
            {
                OSDB_PRINTF(ADS_DEBUG, ADSSERVS "No cap for %p\n", start);
            }
            else
            {
                /*
                    From the cap we want
                    1. Type
                    2. PA (if frame cap)
                    3. Size (if frame cap)
                    4. Rights
                    5. Endpoint badge (if endpoint cap)
                    6. Endpoint badge (PD responsible for this EP)
                    */

                void *paddr = (void *)0;
                seL4_CPtr page_frame_cap = vspace_get_cap(ads_vspace, start);
                assert(page_frame_cap != RESERVED && page_frame_cap != EMPTY);
                osmosis_cap_t cap_info;
                if (gpi_retrieve_cap_data(page_frame_cap, &cap_info) == 0)
                {
                    paddr = (void *)cap_info.paddr;
                }

                while (cap_info.isMinted)
                {
                    if (gpi_retrieve_cap_data(cap_info.minted_from, &cap_info) == 0)
                    {
                        paddr = (void *)cap_info.paddr;
                    }
                    else
                    {
                        break;
                    }
                }

                char page_res_type[CSV_MAX_STRING_SIZE];
                char page_res_id[CSV_MAX_STRING_SIZE];
                snprintf(page_res_type, CSV_MAX_STRING_SIZE, "PhysicalPage");
                snprintf(page_res_id, CSV_MAX_STRING_SIZE, "%p", paddr);
                add_resource(ms, page_res_type, page_res_id);

                add_resource_depends_on(ms, res_id, page_res_id);
            }
        }

        from_sel4_res = from_sel4_res->next;
    }
#endif
}

int ads_shallow_copy(vspace_t *loader,
                     ads_t *ads,
                     vka_t *vka,
                     void *omit_vaddr,
                     void *pd_osm_data,
                     bool shallow_copy,
                     ads_t *ret_ads)
{

    ret_ads->vspace = malloc(sizeof(vspace_t));
    if (ret_ads->vspace == NULL)
    {
        ZF_LOGE("Failed to allocate vspace\n");
        goto error_exit;
    }
    ret_ads->process_for_cookies = malloc(sizeof(sel4utils_process_t));
    if (ret_ads->process_for_cookies == NULL)
    {
        ZF_LOGE("Failed to allocate process struct for coolies in ads_shallow_copy\n");
        goto error_exit;
    }
    vspace_t *from = ads->vspace;
    assert(from != NULL);
    vspace_t *to = ret_ads->vspace;

    // OSDB_PRINTF(ADS_DEBUG, "Cloning vspace\n");
    // printf("Old vspace details:\n");
    // sel4utils_walk_vspace(from, NULL);

    // Give vspace root
    // assign asid pool
    static vka_object_t vspace_root_object;
    sel4utils_alloc_data_t *alloc_data = malloc(sizeof(sel4utils_alloc_data_t));
    if (alloc_data == NULL)
    {
        ZF_LOGE("Failed to allocate memory for alloc data\n");
        goto error_exit;
    }

    int error = vka_alloc_vspace_root(vka, &vspace_root_object);
    if (error)
    {
        ZF_LOGE("Failed to allocate page directory for new process: %d\n", error);
        goto error_exit;
    }

    /* assign an asid pool */
    if (!config_set(CONFIG_X86_64) &&
        assign_asid_pool(seL4_CapInitThreadASIDPool, vspace_root_object.cptr) != seL4_NoError)
    {
        goto error_exit;
    }
    // Create empty vspace
    error = sel4utils_get_vspace(
        loader,
        to,
        alloc_data,
        vka,
        vspace_root_object.cptr,
        sel4utils_allocated_object,

        /*
            sel4utils_allocated_object expects a process struct as a cookie
            Instead use a different function which suited are needs better.
        */

        &(ret_ads->process_for_cookies));
    if (error)
    {
        ZF_LOGE("Failed to get new vspace while making copy: %d\n in %s", error, __FUNCTION__);
        goto error_exit;
    }

    sel4utils_alloc_data_t *from_data = get_alloc_data(from);
    sel4utils_res_t *from_sel4_res = from_data->reservation_head;

    ZF_LOGE("===========Start of interesting output================\n");

    /* walk all the reservations */
    int num_pages;
    while (from_sel4_res != NULL)
    {
        OSDB_PRINTF(ADS_DEBUG, "Reservation: %p\n", (void *)from_sel4_res->start);
        // Reserver
        reservation_t new_res = sel4utils_reserve_range_at(to,
                                                           (void *)from_sel4_res->start,
                                                           from_sel4_res->end - from_sel4_res->start,
                                                           from_sel4_res->rights, from_sel4_res->cacheable);

        sel4utils_res_t *to_sel4_res = reservation_to_res(new_res);
        assert(to_sel4_res != NULL);
        to_sel4_res->type = from_sel4_res->type;

        num_pages = (from_sel4_res->end - from_sel4_res->start) / PAGE_SIZE_4K;
        if (from_sel4_res->start == (uintptr_t)omit_vaddr)
        {
            OSDB_PRINTF(ADS_DEBUG, "Skipping the region, with start vaddr of %p\n", omit_vaddr);
            from_sel4_res = from_sel4_res->next;
            continue;
        }

        // Shallow copy IPC buffer, stack, init data
        if (from_sel4_res->type == SEL4UTILS_RES_TYPE_IPC_BUF ||
            from_sel4_res->type == SEL4UTILS_RES_TYPE_STACK ||
            from_sel4_res->start == (uintptr_t)pd_osm_data ||
            (1 && from_sel4_res->type == SEL4UTILS_RES_TYPE_ELF))
        {
            OSDB_PRINTF(ADS_DEBUG, "======================Shallow copying [%s] %p to %p [%s]\n",
                        human_readable_va_res_type(from_sel4_res->type),
                        (void *)from_sel4_res->start, (void *)from_sel4_res->end,
                        human_readable_size(from_sel4_res->end - from_sel4_res->start));
            error = sel4utils_share_mem_at_vaddr(from, to,
                                                 (void *)from_sel4_res->start,
                                                 num_pages,
                                                 PAGE_BITS_4K,
                                                 (void *)from_sel4_res->start,
                                                 new_res);
            if (error)
            {
                ZF_LOGE("Failed to map memory while sharing copy: %d\n", error);
                goto error_exit;
            }

            for (attach_node_t *res = ads->attach_nodes; res != NULL; res = res->next)
            {
                if (res->vaddr == (void *)from_sel4_res->start)
                {
                    // (XXX) Linh: We don't store the MOs frame caps here... do we even need to?
                    attach_node_t *new_attach_node = malloc(sizeof(attach_node_t));
                    new_attach_node->vaddr = (void *)from_sel4_res->start;
                    new_attach_node->mo_id = res->mo_id;
                    new_attach_node->type = from_sel4_res->type;
                    new_attach_node->next = ret_ads->attach_nodes;
                    ret_ads->attach_nodes = new_attach_node;
                    break;
                }
            }
        }
        else if (from_sel4_res->type == SEL4UTILS_RES_TYPE_HEAP || from_sel4_res->type == SEL4UTILS_RES_TYPE_ELF)
        {
            // Deep copy ELF and HEAP
            OSDB_PRINTF(ADS_DEBUG, "======================Deep copying [%s] %p to %p [%s]\n",
                        human_readable_va_res_type(from_sel4_res->type),
                        (void *)from_sel4_res->start, (void *)from_sel4_res->end,
                        human_readable_size(from_sel4_res->end - from_sel4_res->start));

            error = sel4utils_copy_mem_at_vaddr(
                loader,
                from, to,
                (void *)from_sel4_res->start,
                num_pages,
                PAGE_BITS_4K,
                (void *)from_sel4_res->start,
                new_res);

            if (error)
            {
                ZF_LOGE("Failed to map memory while making copy: %d\n", error);
                goto error_exit;
            }

            // Forge new MO for copied region
            seL4_CPtr *caps = calloc(num_pages, sizeof(seL4_CPtr));

            for (int i = 0; i < num_pages; i++)
            {
                void *p = (void *)from_sel4_res->start + BIT(seL4_PageBits) * i;
                caps[i] = sel4utils_get_cap(to, p);
            }

            seL4_CPtr mo_cap;
            mo_t *mo_obj;
            error = forge_mo_cap_from_frames(caps, num_pages, vka, 0, &mo_cap, &mo_obj);
            ZF_LOGE_IF(error, "Failed to forge MO cap for PD's heap");

            pd_add_resource(&get_pd_component()->rt_pd, GPICAP_TYPE_MO, NSID_DEFAULT, mo_obj->mo_obj_id, mo_cap, 0, 0);

            attach_node_t *new_attach_node = malloc(sizeof(attach_node_t));
            new_attach_node->vaddr = (void *)from_sel4_res->start;
            new_attach_node->mo_id = mo_obj->mo_obj_id;
            new_attach_node->type = from_sel4_res->type;
            new_attach_node->next = ret_ads->attach_nodes;
            ret_ads->attach_nodes = new_attach_node;
        }

        // Move to next node.
        from_sel4_res = from_sel4_res->next;
    }
    OSDB_PRINTF(ADS_DEBUG, "New vspace details:\n");
    // sel4utils_walk_vspace(to, NULL);
    // For each reservation: call share_mem_at_vaddr
    return 0;

error_exit:
    free(alloc_data);
    free(ret_ads->vspace);
    return -1;
}