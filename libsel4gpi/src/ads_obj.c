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

/* This is doesn't belong here but we need it */
extern char _cpio_archive[];
extern char _cpio_archive_end[];

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
               void *vaddr,
               uint32_t num_pages,
               size_t size_bits,
               seL4_CPtr *frame_caps,
               uint32_t mo_id,
               void **ret_vaddr)
{
    int cacheable = 1;
    vspace_t *target = ads->vspace;

    /* Reserve the range in the vspace */
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

    /* Map the frame caps into the vspace */

// (XXX) Arya: Not sure what the intention was with the process cookie
// But setting it causes the processes to page fault once exiting
// Due to vspace tear down, which expects the process cookie to be set
// only if the vspace is responsible for freeing the pages
#if 0
    int error = sel4utils_map_pages_at_vaddr(target,
                                             frame_caps,
                                             (uintptr_t *)process_cookie, // TODO: this is a hack
                                             vaddr,
                                             num_pages,
                                             size_bits,
                                             res);
#else
    int error = sel4utils_map_pages_at_vaddr(target,
                                             frame_caps,
                                             NULL,
                                             vaddr,
                                             num_pages,
                                             size_bits,
                                             res);
#endif

    if (error)
    {
        ZF_LOGE("Failed to map pages\n");
        return 1;
    }
    *ret_vaddr = vaddr;

    /* Track the attachment */
    attach_node_t *attach_node = malloc(sizeof(attach_node_t));
    attach_node->mo_id = mo_id;
    attach_node->frame_caps = malloc(sizeof(seL4_CPtr) * num_pages);
    memcpy(attach_node->frame_caps, frame_caps, sizeof(seL4_CPtr) * num_pages);
    attach_node->type = SEL4UTILS_RES_TYPE_OTHER;
    attach_node->n_pages = num_pages;
    attach_node->vaddr = *ret_vaddr;
    attach_node->next = ads->attach_nodes;
    ads->attach_nodes = attach_node;

    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "attached %u pages at %p\n", num_pages, *ret_vaddr);

    return 0;
}

int ads_attach_mo(ads_t *ads,
                  vka_t *vka,
                  void *vaddr,
                  mo_t *mo,
                  void **ret_vaddr)
{
    int error;

    uint32_t num_pages = mo->num_pages;
    mo_frame_t *root_frame_caps = mo->frame_caps_in_root_task;

    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "attaching mo with id %lu, num pages: %d\n", mo->mo_obj_id, num_pages);

    /* Make a copy of the frame caps for this new mapping */
    seL4_CPtr frame_caps[num_pages];
    for (int i = 0; i < num_pages; i++)
    {
        cspacepath_t from_path, to_path;
        vka_cspace_make_path(vka, root_frame_caps[i].cap, &from_path);

        /* allocate a path for the copy*/
        int error = vka_cspace_alloc_path(vka, &to_path);
        if (error)
        {
            OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to allocate slot in root cspace, error: %d", error);
            return -1;
        }

        /* copy the frame cap */
        error = vka_cnode_copy(&to_path, &from_path, seL4_AllRights);
        if (error)
        {
            OSDB_PRINTF(ADS_DEBUG, ADSSERVS "main: Failed to copy cap, error: %d", error);
            return -1;
        }

        frame_caps[i] = to_path.capPtr;

        // void *frame_paddr = (void *)seL4_DebugCapPaddr(attach_node->frame_caps[i]);
        // OSDB_PRINTF(ADS_DEBUG, ADSSERVS "paddr of frame to map: %p\n", frame_paddr);
    }

    /* Perform the attachment of the new frame caps */
    error = ads_attach(ads,
                       vaddr,
                       num_pages,
                       seL4_PageBits,
                       frame_caps,
                       mo->mo_obj_id,
                       ret_vaddr);

    return error;
}

int ads_rm(ads_t *ads, vka_t *vka, void *vaddr)
{
    assert(vaddr != NULL);

    int error = 0;
    size_t size_bits = seL4_PageBits; // (XXX) Arya: Need to change if we ever have other page size
    vspace_t *target = ads->vspace;

    /* Find the attach node corresponding to this vaddr */

    error = 1; // Default error if we do not find the corresponding memory region

    attach_node_t *prev = NULL;
    for (attach_node_t *node = ads->attach_nodes; node != NULL; node = node->next)
    {
        if (node->vaddr == vaddr)
        {
            error = 0;

            // Remove the reservation
            sel4utils_free_reservation_by_vaddr(target, vaddr);

            // Unmap the pages
            // (XXX) Arya: I believe we want VSPACE_PRESERVE here
            // Otherwise, sel4utils will attempt to free the frame caps and their corresponding untyped
            // Which we do not want, since the MO continues to exist
            sel4utils_unmap_pages(target, vaddr, node->n_pages, size_bits, VSPACE_PRESERVE);

            // Remove the attach node
            if (prev == NULL)
            {
                ads->attach_nodes = node->next;
            }
            else
            {
                prev->next = node->next;
            }

            free(node->frame_caps);
            free(node);

            break;
        }

        prev = node;
    }

    return error;
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

void ads_dump_rr(ads_t *ads, model_state_t *ms, gpi_model_node_t *pd_node)
{
    // (XXX) Arya: ADS does not belong to a resource space! To fix
    gpi_model_node_t *ads_node = add_resource_node(ms, GPICAP_TYPE_ADS, 1, ads->ads_obj_id);

    // (XXX) Arya: Do we want to only include the currently active ADS? Reintroduce the 'mapped' property?
    add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, ads_node);

    uint32_t added_mo_rrs[MAX_MO_RR];
    int num_added_mo_rrs = 0;
    bool skip = false;
    for (attach_node_t *res = ads->attach_nodes; res != NULL; res = res->next)
    {
        // Skip extra attaches of the same MO
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

        assert(num_added_mo_rrs < MAX_MO_RR);
        added_mo_rrs[num_added_mo_rrs] = res->mo_id;
        num_added_mo_rrs++;

        /* Add the VMR node */
        // (XXX) Arya: VMR is an implicit resource
        gpi_model_node_t *vmr_node = add_resource_node(ms, GPICAP_TYPE_VMR, ads->ads_obj_id, (uint64_t)res->vaddr);
        add_edge(ms, GPI_EDGE_TYPE_SUBSET, vmr_node, ads_node);
        add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, vmr_node);

        /* Add the relation from VMR to MO node */
        gpi_model_node_t *mo_node = add_resource_node(ms, GPICAP_TYPE_MO, 1, res->mo_id);
        add_edge(ms, GPI_EDGE_TYPE_MAP, vmr_node, mo_node);
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

int ads_load_elf(vspace_t *loadee_vspace, sel4utils_process_t *proc, char *image_name)
{
    int error;
    OSDB_PRINTF(ADS_DEBUG, ADSSERVS "Loading %s's ELF\n", image_name);
    seL4_CPtr slot;
    vspace_t *server_vspace = get_ads_component()->server_vspace;
    vka_t *server_vka = get_ads_component()->server_vka;

    unsigned long size;
    unsigned long cpio_len = _cpio_archive_end - _cpio_archive;
    char const *file = cpio_get_file(_cpio_archive, cpio_len, image_name, &size);
    elf_t elf;
    elf_newFile(file, size, &elf);

    proc->entry_point = sel4utils_elf_load(loadee_vspace, server_vspace, server_vka, server_vka, &elf);
    if (proc->entry_point == NULL)
    {
        ZF_LOGE("Failed to load elf file\n");
        goto error;
    }

    proc->sysinfo = sel4utils_elf_get_vsyscall(&elf);

    /* Retrieve the ELF phdrs */
    proc->num_elf_phdrs = sel4utils_elf_num_phdrs(&elf);
    proc->elf_phdrs = calloc(proc->num_elf_phdrs, sizeof(Elf_Phdr));
    if (!proc->elf_phdrs)
    {
        ZF_LOGE("Failed to allocate memory for elf phdr information");
        goto error;
    }
    sel4utils_elf_read_phdrs(&elf, proc->num_elf_phdrs, proc->elf_phdrs);
    proc->pagesz = PAGE_SIZE_4K;

    return 0;
error:
    if (proc->elf_regions)
    {
        free(proc->elf_regions);
    }

    if (proc->elf_phdrs)
    {
        free(proc->elf_phdrs);
    }
}
