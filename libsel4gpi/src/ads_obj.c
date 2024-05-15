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
#include <cpio/cpio.h>
#include <sel4utils/helpers.h>
#include <sel4runtime/auxv.h>

#define MAX_MO_RR 10000

// Defined for utility printing macros
#define DEBUG_ID ADS_DEBUG
#define SERVER_ID ADSSERVS

/* This is doesn't belong here but we need it */
extern char _cpio_archive[];
extern char _cpio_archive_end[];

int ads_new(ads_t *ads,
            vka_t *vka,
            vspace_t *loader,
            void *arg0)
{
    int error = 0;

    // Allocate a vspace
    ads->vspace = malloc(sizeof(vspace_t));
    if (ads->vspace == NULL)
    {
        ZF_LOGE("Failed to allocate vspace\n");
        goto error_exit;
    }

    vspace_t *new_vspace = ads->vspace;
    assert(new_vspace != NULL);

    // Allocate process structure for cookies
    ads->process_for_cookies = malloc(sizeof(sel4utils_process_t));
    if (ads->process_for_cookies == NULL)
    {
        ZF_LOGE("Failed to allocate process struct for cookies in ads_new\n");
        goto error_exit;
    }

    // Give vspace root
    vka_object_t *vspace_root_object = malloc(sizeof(vka_object_t));
    assert(vspace_root_object != NULL);

    error = vka_alloc_vspace_root(vka, vspace_root_object);
    if (error)
    {
        ZF_LOGE("Failed to allocate page directory for new process: %d\n", error);
        goto error_exit;
    }

    // Allocate alloc data
    sel4utils_alloc_data_t *alloc_data = malloc(sizeof(sel4utils_alloc_data_t));
    if (alloc_data == NULL)
    {
        ZF_LOGE("Failed to allocate memory for alloc data\n");
        goto error_exit;
    }

    // Assign an asid pool
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

        &(ads->process_for_cookies));
    if (error)
    {
        ZF_LOGE("Failed to get new vspace while making copy: %d\n in %s", error, __FUNCTION__);
        goto error_exit;
    }
    ads->root_page_dir = vspace_root_object;

    // Initialize VMR registry
    resource_server_initialize_registry(&ads->attach_registry, NULL);
    resource_server_initialize_registry(&ads->attach_id_to_vaddr_map, NULL);

    return 0;

error_exit:
    free(alloc_data);
    free(ads->vspace);
    return -1;
}

int ads_reserve(ads_t *ads,
                void *vaddr,
                uint32_t num_pages,
                size_t size_bits,
                sel4utils_reservation_type_t vmr_type,
                attach_node_t **ret_node)
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
                                              MO_PAGE_BITS,
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

    /* Set the reservation type */
    sel4utils_res_t *sel4utils_res = reservation_to_res(res);
    sel4utils_res->type = vmr_type;

    /* Track the VMR in registry */
    attach_node_t *attach_node = malloc(sizeof(attach_node_t));
    attach_node_map_t *attach_node_map_entry = malloc(sizeof(attach_node_map_t));

    if (attach_node == NULL || attach_node_map_entry == NULL)
    {
        OSDB_PRINTF("Failed to allocate registry entry for ADS reservation.\n");
        return 1;
    }

    // Map a shorter attach node ID to vaddr
    attach_node_map_entry->vaddr = vaddr;
    resource_server_registry_insert_new_id(&ads->attach_id_to_vaddr_map, (resource_server_registry_node_t *)attach_node_map_entry);

    // The attach node is keyed by vaddr
    memset((void *)attach_node, 0, sizeof(attach_node_t));
    attach_node->res = res;
    attach_node->vaddr = vaddr;
    attach_node->map_entry = attach_node_map_entry;
    attach_node->type = vmr_type;
    attach_node->n_pages = num_pages;
    attach_node->gen.object_id = (uint64_t)vaddr;
    resource_server_registry_insert(&ads->attach_registry, (resource_server_registry_node_t *)attach_node);

    *ret_node = attach_node;
    return 0;
}

attach_node_t *ads_get_res_by_id(ads_t *ads, uint64_t res_id)
{
    attach_node_map_t *map_entry = (attach_node_map_t *)resource_server_registry_get_by_id(&ads->attach_id_to_vaddr_map, res_id);
    return ads_get_res_by_vaddr(ads, map_entry->vaddr);
}

attach_node_t *ads_get_res_by_vaddr(ads_t *ads, void *vaddr)
{
    return (attach_node_t *)resource_server_registry_get_by_id(&ads->attach_registry, (uint64_t)vaddr);
}

int ads_attach_to_res(ads_t *ads,
                      vka_t *vka,
                      attach_node_t *reservation,
                      size_t offset,
                      mo_t *mo)
{
    int error = 0;

    OSDB_PRINTF("attaching mo (id %lu, pages: %d) to reservation(vaddr: %p, type: %s, pages: %d) offset %ld\n",
                mo->id, mo->num_pages,
                reservation->vaddr, human_readable_va_res_type(reservation->type), reservation->n_pages,
                offset);

    /* Make a copy of the frame caps for this new mapping */
    seL4_CPtr frame_caps[mo->num_pages];
    for (int i = 0; i < mo->num_pages; i++)
    {
        cspacepath_t from_path, to_path;
        vka_cspace_make_path(vka, mo->frame_caps_in_root_task[i], &from_path);

        /* allocate a path for the copy*/
        int error = vka_cspace_alloc_path(vka, &to_path);
        if (error)
        {
            OSDB_PRINTF("main: Failed to allocate slot in root cspace, error: %d", error);
            return 1;
        }

        /* copy the frame cap */
        error = vka_cnode_copy(&to_path, &from_path, seL4_AllRights);
        if (error)
        {
            OSDB_PRINTF("main: Failed to copy cap, error: %d", error);
            return 1;
        }

        frame_caps[i] = to_path.capPtr;

        // void *frame_paddr = (void *)seL4_DebugCapPaddr(attach_node->frame_caps[i]);
        // OSDB_PRINTF("paddr of frame to map: %p\n", frame_paddr);
    }

    /* Map the frame caps into the vspace */
    error = sel4utils_map_pages_at_vaddr(ads->vspace,
                                         frame_caps,
                                         NULL,
                                         reservation->vaddr + offset,
                                         mo->num_pages,
                                         MO_PAGE_BITS,
                                         reservation->res);

    if (error)
    {
        ZF_LOGE("Failed to map pages\n");
        return 1;
    }

    /* Track the attachment */
    reservation->mo_attached = true;
    reservation->mo_offset = offset;
    reservation->mo_id = mo->id;
    reservation->n_frames = mo->num_pages;
    reservation->frame_caps = malloc(sizeof(seL4_CPtr) * mo->num_pages);
    memcpy(reservation->frame_caps, frame_caps, sizeof(seL4_CPtr) * mo->num_pages);

    return 0;
}

int ads_attach(ads_t *ads,
               vka_t *vka,
               void *vaddr,
               mo_t *mo,
               void **ret_vaddr,
               sel4utils_reservation_type_t vmr_type)
{
    int error = 0;

    /* Reserve the VMR */
    attach_node_t *attach_node;
    error = ads_reserve(ads, vaddr, mo->num_pages, MO_PAGE_BITS, vmr_type, &attach_node);

    if (error)
    {
        ZF_LOGE("Failed to reserve region\n");
        return 1;
    }

    /* Attach the MO */
    error = ads_attach_to_res(ads, vka, attach_node, 0, mo);

    if (error)
    {
        ZF_LOGE("Failed to attach pages to region\n");
        return 1;
    }

    *ret_vaddr = attach_node->vaddr;
    return error;
}

int ads_forge_attach(ads_t *ads, sel4utils_res_t *res, mo_t *mo)
{
    // Add the attach node for this region
    attach_node_t *attach_node = malloc(sizeof(attach_node_t));
    attach_node_map_t *attach_node_map_entry = malloc(sizeof(attach_node_map_t));

    if (attach_node == NULL || attach_node_map_entry == NULL)
    {
        ZF_LOGE("Failed to allocate attach node for forged attach\n");
        return 1;
    }

    // Map a shorter attach node ID to vaddr
    attach_node_map_entry->vaddr = (void *)res->start;
    resource_server_registry_insert_new_id(&ads->attach_id_to_vaddr_map, (resource_server_registry_node_t *)attach_node_map_entry);

    // The attach node is keyed by vaddr
    memset((void *)attach_node, 0, sizeof(attach_node_t));
    attach_node->res.res = res;
    attach_node->vaddr = (void *)res->start;
    attach_node->map_entry = attach_node_map_entry;
    attach_node->type = res->type;
    attach_node->n_pages = mo->num_pages;
    attach_node->gen.object_id = res->start;
    attach_node->mo_attached = true;
    attach_node->mo_id = mo->id;
    attach_node->mo_offset = 0;
    resource_server_registry_insert(&ads->attach_registry, (resource_server_registry_node_t *)attach_node);

    return 0;
}

int ads_rm(ads_t *ads, vka_t *vka, void *vaddr)
{
    assert(vaddr != NULL);

    int error = 0;
    vspace_t *target = ads->vspace;

    /* Find the attach node corresponding to this vaddr */
    attach_node_t *node = ads_get_res_by_vaddr(ads, vaddr);

    if (node == 0)
    {
        ZF_LOGE("Failed to find VMR to remove\n");
        return 1;
    }

    // Remove the reservation
    sel4utils_free_reservation(target, node->res);

    // Unmap the pages
    // (XXX) Arya: I believe we want VSPACE_PRESERVE here
    // Otherwise, sel4utils will attempt to free the frame caps and their corresponding untyped
    // Which we do not want, since the MO continues to exist
    sel4utils_unmap_pages(target, vaddr, node->n_pages, MO_PAGE_BITS, VSPACE_PRESERVE);

    // Free the frame caps (duplicated for this attach)
    for (int i = 0; i < node->n_frames; i++)
    {
        cspacepath_t path;
        vka_cspace_make_path(vka, node->frame_caps[i], &path);
        vka_cnode_delete(&path);
        vka_cspace_free_path(vka, path);
    }

    free(node->frame_caps);

    // Remove the attach node
    resource_server_registry_delete(&ads->attach_id_to_vaddr_map, (resource_server_registry_node_t *)node->map_entry);
    resource_server_registry_delete(&ads->attach_registry, (resource_server_registry_node_t *)node);

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
    gpi_model_node_t *ads_node = add_resource_node(ms, GPICAP_TYPE_ADS, 1, ads->id);

    // (XXX) Arya: Do we want to only include the currently active ADS? Reintroduce the 'mapped' property?
    add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, ads_node);

    uint32_t added_mo_rrs[MAX_MO_RR];
    int num_added_mo_rrs = 0;
    bool skip = false;
    for (attach_node_t *res = (attach_node_t *)ads->attach_registry.head; res != NULL; res = (attach_node_t *)res->gen.hh.next)
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
        gpi_model_node_t *vmr_node = add_resource_node(ms, GPICAP_TYPE_VMR, ads->id, (uint64_t)res->vaddr);
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
    OSDB_PRINTF("vka address: %p\n", vka);

    while (from_sel4_res != NULL)
    {
        char res_type[CSV_MAX_STRING_SIZE];
        char res_id[CSV_MAX_STRING_SIZE];
        snprintf(res_type, CSV_MAX_STRING_SIZE, "%s", human_readable_va_res_type(from_sel4_res->type));
        snprintf(res_id, CSV_MAX_STRING_SIZE, "%u_%lx_%lx",
                 ads->id, from_sel4_res->start,
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
                OSDB_PRINTF("No cap for %p\n", start);
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
                     vka_t *vka,
                     ads_t *src_ads,
                     ads_t *dst_ads,
                     void *omit_vaddr,
                     void *pd_osm_data,
                     bool shallow_copy)
{
    OSDB_PRINTF("Shallow copying ADS(%d)\n", src_ads->id);

    int error = 0;
    vspace_t *from = src_ads->vspace;
    vspace_t *to = dst_ads->vspace;

    assert(from != NULL);
    assert(to != NULL);

    sel4utils_alloc_data_t *from_data = get_alloc_data(from);
    sel4utils_res_t *from_sel4_res = from_data->reservation_head;

    OSDB_PRINTF("=========== Start of ADS copy (%d -> %d) ================\n", src_ads->id, dst_ads->id);

    /* walk all the reservations */
    // (XXX) Arya: We may be able to just walk attach nodes instead of reservations (eventually?)
    int num_pages;
    while (from_sel4_res != NULL)
    {
        OSDB_PRINTF("Reservation: %p\n", (void *)from_sel4_res->start);

        if (from_sel4_res->start == (uintptr_t)omit_vaddr)
        {
            OSDB_PRINTF("Skipping the region, with start vaddr of %p\n", omit_vaddr);
            from_sel4_res = from_sel4_res->next;
            continue;
        }

        // Reserve the region
        num_pages = (from_sel4_res->end - from_sel4_res->start) / (SIZE_BITS_TO_BYTES(MO_PAGE_BITS));

        attach_node_t *new_attach_node;
        error = ads_reserve(dst_ads, (void *)from_sel4_res->start, num_pages, MO_PAGE_BITS, from_sel4_res->type, &new_attach_node);

        if (error)
        {
            ZF_LOGE("Failed to reserve region\n");
            return 1;
        }

        // Find the original attach node
        attach_node_t *old_attach_node = ads_get_res_by_vaddr(src_ads, (void *)from_sel4_res->start);

        if (old_attach_node == NULL)
        {
            ZF_LOGE("Failed to find the attach node for vaddr: %p\n", (void *)from_sel4_res->start);
            goto error_exit;
        }

        // Find the original MO
        mo_t *old_mo;
        if (old_attach_node->mo_attached)
        {
            old_mo = &mo_component_registry_get_entry_by_id(old_attach_node->mo_id)->mo;

            if (error)
            {
                ZF_LOGE("Failed to find the MO (%ld) for vaddr: %p\n", old_attach_node->mo_id, (void *)from_sel4_res->start);
                goto error_exit;
            }
        }
        else
        {
            // If no MO attached, skip to next reservation
            ZF_LOGE("Skipping reservation at vaddr: %p, no MO attached\n", (void *)from_sel4_res->start);
            from_sel4_res = from_sel4_res->next;
            continue;
        }

        // Shallow copy IPC buffer, stack, init data, elf
        if (from_sel4_res->type == SEL4UTILS_RES_TYPE_IPC_BUF ||
            from_sel4_res->type == SEL4UTILS_RES_TYPE_STACK ||
            from_sel4_res->start == (uintptr_t)pd_osm_data ||
            from_sel4_res->type == SEL4UTILS_RES_TYPE_ELF)
        {
            OSDB_PRINTF("======================Shallow copying [%s] %p to %p [%s]\n",
                        human_readable_va_res_type(from_sel4_res->type),
                        (void *)from_sel4_res->start, (void *)from_sel4_res->end,
                        human_readable_size(from_sel4_res->end - from_sel4_res->start));

            // Attach the same MO in the new ADS
            error = ads_attach_to_res(dst_ads, vka, new_attach_node, old_attach_node->mo_offset, old_mo);

            if (error)
            {
                ZF_LOGE("Failed to attach pages to region\n");
                return 1;
            }
        }
        else if (from_sel4_res->type == SEL4UTILS_RES_TYPE_HEAP)
        {
            OSDB_PRINTF("======================Deep copying [%s] %p to %p [%s]\n",
                        human_readable_va_res_type(from_sel4_res->type),
                        (void *)from_sel4_res->start, (void *)from_sel4_res->end,
                        human_readable_size(from_sel4_res->end - from_sel4_res->start));

            // Make a new MO
            // The "client" to hold this MO is the root task
            mo_component_registry_entry_t *mo_entry;
            seL4_CPtr mo_cap; // Not used since we are not giving this MO away
            error = resource_component_allocate(get_mo_component(), get_gpi_server()->rt_pd_id, true, (void *)num_pages,
                                                (resource_server_registry_node_t **)&mo_entry, &mo_cap);

            if (error)
            {
                ZF_LOGE("Failed to allocate a new MO for deep copy\n");
                goto error_exit;
            }

            // Attach the new MO in the new ADS
            mo_t *new_mo = &mo_entry->mo;
            error = ads_attach_to_res(dst_ads, vka, new_attach_node, old_attach_node->mo_offset, new_mo);

            if (error)
            {
                ZF_LOGE("Failed to attach pages to region\n");
                return 1;
            }

            // Temporarily map the pages of both MO to current vspace and copy data
            // (XXX) Arya: no ADS for RT so just manually map the pages
            void *old_mo_va = vspace_map_pages(loader, old_mo->frame_caps_in_root_task, NULL, seL4_AllRights, num_pages, MO_PAGE_BITS, 1);

            if (old_mo_va == NULL)
            {
                ZF_LOGE("Failed to map old MO for deep copy\n");
                goto error_exit;
            }

            void *new_mo_va = vspace_map_pages(loader, new_mo->frame_caps_in_root_task, NULL, seL4_AllRights, num_pages, MO_PAGE_BITS, 1);

            if (new_mo_va == NULL)
            {
                ZF_LOGE("Failed to map new MO for deep copy\n");
                goto error_exit;
            }

            memcpy(new_mo_va, old_mo_va, num_pages * SIZE_BITS_TO_BYTES(MO_PAGE_BITS));

            vspace_unmap_pages(loader, old_mo_va, num_pages, MO_PAGE_BITS, VSPACE_FREE);
            vspace_unmap_pages(loader, new_mo_va, num_pages, MO_PAGE_BITS, VSPACE_FREE);
        }

        // Move to next node.
        from_sel4_res = from_sel4_res->next;
    }
    OSDB_PRINTF("New vspace details:\n");
    // sel4utils_walk_vspace(to, NULL);
    // For each reservation: call share_mem_at_vaddr
    return 0;

error_exit:
    return 1;
}

void ads_destroy(ads_t *ads)
{
    /* Destroy the hash tables of attach nodes */
    // (XXX) Arya: This can trigger sys_munmap which is not supported
    resource_server_registry_node_t *current, *tmp;
    HASH_ITER(hh, ads->attach_registry.head, current, tmp)
    {
        attach_node_t *node = (attach_node_t *)current;
        resource_server_registry_delete(&ads->attach_id_to_vaddr_map, (resource_server_registry_node_t *)node->map_entry);
        resource_server_registry_delete(&ads->attach_registry, current);
    }

    /* tear down the vspace */
    vspace_tear_down(ads->vspace, VSPACE_FREE);

    /**
     * we should not need to free any objects created by the vspace,
     * as they should be all MOs
     * (XXX) Arya: Make sure this is the case
     */

    /* free the object */
}

/* ======================================= CONVENIENCE FUNCTIONS (NOT PART OF FRAMEWORK) ================================================= */

int ads_load_elf(vspace_t *loadee_vspace,
                 sel4utils_process_t *proc,
                 char *image_name,
                 void **ret_entry_point)
{
    int error;
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

    *ret_entry_point = proc->entry_point;

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

int ads_proc_setup(sel4utils_process_t *process,
                   void *osm_init_data,
                   vka_t *vka,
                   vspace_t *vspace,
                   int argc,
                   char *argv[],
                   void **ret_init_stack)
{
    assert(vspace != NULL);
    assert(&process->vspace != NULL);
    /* define an envp and auxp */
    int error;
    int envc = 0;
    char *envp[] = {};

    uintptr_t initial_stack_pointer = (uintptr_t)process->thread.stack_top - sizeof(seL4_Word);

    /* Copy the elf headers */
    uintptr_t at_phdr;
    error = sel4utils_stack_write(vspace, &process->vspace, vka, process->elf_phdrs,
                                  process->num_elf_phdrs * sizeof(Elf_Phdr), &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    at_phdr = initial_stack_pointer;

    /* initialize of aux vectors */
    int auxc = 7;
    Elf_auxv_t auxv[8];
    auxv[0].a_type = AT_PAGESZ;
    auxv[0].a_un.a_val = process->pagesz;
    auxv[1].a_type = AT_PHDR;
    auxv[1].a_un.a_val = at_phdr;
    auxv[2].a_type = AT_PHNUM;
    auxv[2].a_un.a_val = process->num_elf_phdrs;
    auxv[3].a_type = AT_PHENT;
    auxv[3].a_un.a_val = sizeof(Elf_Phdr);
    auxv[4].a_type = AT_SEL4_IPC_BUFFER_PTR;
    auxv[4].a_un.a_val = process->thread.ipc_buffer_addr;
    auxv[5].a_type = AT_SEL4_TCB;
    auxv[5].a_un.a_val = process->dest_tcb_cptr;

    auxv[6].a_type = AT_OSM_INIT_DATA;
    auxv[6].a_un.a_val = (uint64_t)osm_init_data;

    if (process->sysinfo)
    {
        auxv[7].a_type = AT_SYSINFO;
        auxv[7].a_un.a_val = process->sysinfo;
        auxc++;
    }

    seL4_UserContext context = {0};

    uintptr_t dest_argv[argc];
    uintptr_t dest_envp[envc];

    /* write all the strings into the stack */
    /* Copy over the user arguments */
    error = sel4utils_stack_copy_args(vspace, &process->vspace, vka, argc, argv, dest_argv, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
    /* copy the environment */
    error = sel4utils_stack_copy_args(vspace, &process->vspace, vka, envc, envp, dest_envp, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
#pragma GCC diagnostic pop

    /* we need to make sure the stack is aligned to a double word boundary after we push on everything else
     * below this point. First, work out how much we are going to push */
    size_t to_push = 5 * sizeof(seL4_Word) +  /* constants */
                     sizeof(auxv[0]) * auxc + /* aux */
                     sizeof(dest_argv) +      /* args */
                     sizeof(dest_envp);       /* env */
    uintptr_t hypothetical_stack_pointer = initial_stack_pointer - to_push;
    uintptr_t rounded_stack_pointer = ALIGN_DOWN(hypothetical_stack_pointer, STACK_CALL_ALIGNMENT);
    ptrdiff_t stack_rounding = hypothetical_stack_pointer - rounded_stack_pointer;
    initial_stack_pointer -= stack_rounding;

    /* construct initial stack frame */
    /* Null terminate aux */
    error = sel4utils_stack_write_constant(vspace, &process->vspace, vka, 0, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    error = sel4utils_stack_write_constant(vspace, &process->vspace, vka, 0, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    /* write aux */
    error = sel4utils_stack_write(vspace, &process->vspace, vka, auxv, sizeof(auxv[0]) * auxc, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    /* Null terminate environment */
    error = sel4utils_stack_write_constant(vspace, &process->vspace, vka, 0, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    /* write environment */
    error = sel4utils_stack_write(vspace, &process->vspace, vka, dest_envp, sizeof(dest_envp), &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    /* Null terminate arguments */
    error = sel4utils_stack_write_constant(vspace, &process->vspace, vka, 0, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    /* write arguments */
    error = sel4utils_stack_write(vspace, &process->vspace, vka, dest_argv, sizeof(dest_argv), &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    /* Push argument count */
    error = sel4utils_stack_write_constant(vspace, &process->vspace, vka, argc, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }

    assert(initial_stack_pointer % (2 * sizeof(seL4_Word)) == 0);
    error = sel4utils_arch_init_context(process->entry_point, (void *)initial_stack_pointer, &context);
    if (error)
    {
        printf("sel4utils_arch_init_context error\n");
        return error;
    }

    process->thread.initial_stack_pointer = (void *)initial_stack_pointer;
    *ret_init_stack = (void *)initial_stack_pointer;
    return 0;
}

// int ads_thread_setup(void *stack_top, void **ret_init_stack)
// {
// }
