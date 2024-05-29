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
#include <sel4gpi/error_handle.h>
#include <cpio/cpio.h>
#include <sel4utils/helpers.h>
#include <sel4runtime/auxv.h>
#include <sel4runtime.h>

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
    if (map_entry == NULL)
    {
        return NULL;
    }
    return ads_get_res_by_vaddr(ads, map_entry->vaddr);
}

attach_node_t *ads_get_res_by_vaddr(ads_t *ads, void *vaddr)
{
    return (attach_node_t *)resource_server_registry_get_by_id(&ads->attach_registry, (uint64_t)vaddr);
}

/**
 * @brief finds a reservation for a VMR by the type
 *
 * @param src_ads ADS to find the reservation in
 * @param vmr_type the type of VMR to look for
 * @return attach_node_t* returns the attach node data, or NULL if no such VMR exists
 */
static attach_node_t *ads_get_res_by_type(ads_t *src_ads, sel4utils_reservation_type_t vmr_type)
{
    // sel4utils_alloc_data_t *vmr_data = get_alloc_data(src_ads->vspace);
    // sel4utils_res_t *curr_res = vmr_data->reservation_head;

    // while (curr_res != NULL)
    // {
    //     if (curr_res->type == vmr_type)
    //     {
    //         return curr_res;
    //     }

    //     curr_res = curr_res->next;
    // }

    for (attach_node_t *curr = (attach_node_t *)src_ads->attach_registry.head; curr != NULL; curr = curr->gen.hh.next)
    {
        if (curr->type == vmr_type)
        {
            return curr;
        }
    }

    return NULL;
}

static int copy_frame_caps_for_mapping(seL4_CPtr *src_caps, seL4_CPtr *dest_caps, size_t num_pages)
{
    int error = 0;

    cspacepath_t from_path, to_path;
    for (size_t i = 0; i < num_pages; i++)
    {
        vka_cspace_make_path(get_ads_component()->server_vka, src_caps[i], &from_path);
        error = vka_cspace_alloc_path(get_ads_component()->server_vka, &to_path);
        SERVER_GOTO_IF_ERR(error, "Failed to allocate slot\n");

        error = vka_cnode_copy(&to_path, &from_path, seL4_AllRights);
        SERVER_GOTO_IF_ERR(error, "Failed to copy cap\n");

        dest_caps[i] = to_path.capPtr;
    }

err_goto:
    return error;
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
    // seL4_CPtr frame_caps[mo->num_pages];
    // for (int i = 0; i < mo->num_pages; i++)
    // {
    //     cspacepath_t from_path, to_path;
    //     vka_cspace_make_path(vka, mo->frame_caps_in_root_task[i], &from_path);

    //     /* allocate a path for the copy*/
    //     int error = vka_cspace_alloc_path(vka, &to_path);
    //     if (error)
    //     {
    //         OSDB_PRINTF("main: Failed to allocate slot in root cspace, error: %d", error);
    //         return 1;
    //     }

    //     /* copy the frame cap */
    //     error = vka_cnode_copy(&to_path, &from_path, seL4_AllRights);
    //     if (error)
    //     {
    //         OSDB_PRINTF("main: Failed to copy cap, error: %d", error);
    //         return 1;
    //     }

    //     frame_caps[i] = to_path.capPtr;

    //     // void *frame_paddr = (void *)seL4_DebugCapPaddr(attach_node->frame_caps[i]);
    //     // OSDB_PRINTF("paddr of frame to map: %p\n", frame_paddr);
    // }

    reservation->frame_caps = malloc(sizeof(seL4_CPtr) * mo->num_pages);

    error = copy_frame_caps_for_mapping(mo->frame_caps_in_root_task, reservation->frame_caps, mo->num_pages);
    SERVER_GOTO_IF_ERR(error, "Failed to copy frame caps for attachment\n");

    /* Map the frame caps into the vspace */
    error = sel4utils_map_pages_at_vaddr(ads->vspace,
                                         reservation->frame_caps,
                                         NULL,
                                         reservation->vaddr + offset,
                                         mo->num_pages,
                                         MO_PAGE_BITS,
                                         reservation->res);

    SERVER_GOTO_IF_ERR(error, "Failed to map pages\n");

    /* Track the attachment */
    reservation->mo_attached = true;
    reservation->mo_offset = offset;
    reservation->mo_id = mo->id;
    reservation->n_frames = mo->num_pages;
    // memcpy(reservation->frame_caps, frame_caps, sizeof(seL4_CPtr) * mo->num_pages);

err_goto:
    return error;
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
    // Add the ADS resource space
    gpi_model_node_t *ads_space_node = add_resource_space_node(ms, GPICAP_TYPE_ADS, get_ads_component()->space_id);

    // Add the ADS node
    gpi_model_node_t *ads_node = add_resource_node(ms, GPICAP_TYPE_ADS, 1, ads->id);
    add_edge(ms, GPI_EDGE_TYPE_SUBSET, ads_node, ads_space_node);

    // (XXX) Arya: Do we want to only include the currently active ADS? Reintroduce the 'mapped' property?
    add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, ads_node);

    for (attach_node_t *res = (attach_node_t *)ads->attach_registry.head; res != NULL; res = (attach_node_t *)res->gen.hh.next)
    {
        /* Add the VMR node */
        // VMR is sometimes an implicit resource (eg. MO attached without reservation)
        gpi_model_node_t *vmr_node = add_resource_node(ms, GPICAP_TYPE_VMR, ads->id, (uint64_t)res->vaddr);
        add_edge(ms, GPI_EDGE_TYPE_SUBSET, vmr_node, ads_node);
        add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, vmr_node);

        /* Add the relation from VMR to MO node, if there is one */
        if (res->mo_attached)
        {
            gpi_model_node_t *mo_node = add_resource_node(ms, GPICAP_TYPE_MO, 1, res->mo_id);
            add_edge(ms, GPI_EDGE_TYPE_MAP, vmr_node, mo_node);
        }
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

/**
 * @brief copies a VMR reservation from src_ads to dst_ads
 *
 * @param src_ads the source ADS
 * @param dst_ads the destination ADS
 * @param start address of the start of the VMR
 * @param end address of the end of the VMR
 * @param vmr_type VMR type (e.g. stack, heap, etc.)
 * @param src_attach_node the attach node to copy the reservation from
 * @param ret_new_attach_node returns the created attach node for dst_ads
 * @param ret_mo if MO was attached in src_ads's reservation, returns the MO, otherwise NULL
 * @param ret_n_pages returns the number of pages in the reservation
 * @return 0 on success, 1 on failure
 */
static int ads_copy_reservation(ads_t *src_ads,
                                ads_t *dst_ads,
                                uintptr_t start,
                                uintptr_t end,
                                sel4utils_reservation_type_t vmr_type,
                                attach_node_t *src_attach_node,
                                attach_node_t **ret_new_attach_node,
                                mo_t **ret_mo,
                                int *ret_n_pages)
{
    int error = 0;
    int num_pages = (end - start) / (SIZE_BITS_TO_BYTES(MO_PAGE_BITS));

    attach_node_t *new_attach_node;
    error = ads_reserve(dst_ads, (void *)start, num_pages, MO_PAGE_BITS, vmr_type, &new_attach_node);
    SERVER_GOTO_IF_ERR(error, "Failed to reserve region\n");

    // Find the original MO
    mo_t *old_mo;
    if (src_attach_node->mo_attached)
    {
        mo_component_registry_entry_t *old_mo_reg_entry = (mo_component_registry_entry_t *)resource_component_registry_get_by_id(get_mo_component(), src_attach_node->mo_id);
        SERVER_GOTO_IF_COND(old_mo_reg_entry == NULL, "Failed to find the MO (%ld) for vaddr: %p\n", src_attach_node->mo_id, (void *)start);
        old_mo = &old_mo_reg_entry->mo;
    }

    *ret_new_attach_node = new_attach_node;
    *ret_mo = old_mo;
    *ret_n_pages = num_pages;

err_goto:
    return error;
}

/**
 * @brief deep copies the contents of src_mo to dst_ads, a reservation for the VMR in dst_ads must already exist
 *
 * @param dst_ads ADS to copy MO contents into
 * @param src_mo MO of data to be copied
 * @param num_pages number of pages in the source reservation
 * @param new_attach_node the attach node in dst_ads for the reservation
 * @param old_attach_node the original attach node for src_mo
 * @return int 0 on success, 1 on failure
 */
static int ads_deep_copy(ads_t *dst_ads, mo_t *src_mo, int num_pages, attach_node_t *new_attach_node, attach_node_t *old_attach_node)
{
    int error = 0;

    // Make a new MO
    // The "client" to hold this MO is the root task
    mo_component_registry_entry_t *mo_entry;
    seL4_CPtr mo_cap; // Not used since we are not giving this MO away
    error = resource_component_allocate(get_mo_component(), get_gpi_server()->rt_pd_id, BADGE_OBJ_ID_NULL, false, (void *)num_pages, (resource_server_registry_node_t **)&mo_entry, &mo_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate a new MO for deep copy\n");

    // Attach the new MO in the new ADS
    mo_t *new_mo = &mo_entry->mo;
    error = ads_attach_to_res(dst_ads, get_ads_component()->server_vka, new_attach_node, old_attach_node->mo_offset, new_mo);
    SERVER_GOTO_IF_ERR(error, "Failed to attach pages to region\n");

    // Temporarily map the pages of both MO to current vspace and copy data
    // (XXX) Arya: no ADS for RT so just manually map the pages
    vspace_t *loader = get_ads_component()->server_vspace;
    void *old_mo_va = vspace_map_pages(loader, src_mo->frame_caps_in_root_task, NULL, seL4_AllRights, num_pages, MO_PAGE_BITS, 1);
    SERVER_GOTO_IF_COND(old_mo_va == NULL, "Failed to map old MO for deep copy\n");

    void *new_mo_va = vspace_map_pages(loader, new_mo->frame_caps_in_root_task, NULL, seL4_AllRights, num_pages, MO_PAGE_BITS, 1);
    SERVER_GOTO_IF_COND(new_mo_va == NULL, "Failed to map new MO for deep copy\n");

    memcpy(new_mo_va, old_mo_va, num_pages * SIZE_BITS_TO_BYTES(MO_PAGE_BITS));

err_goto:
    vspace_unmap_pages(loader, old_mo_va, num_pages, MO_PAGE_BITS, VSPACE_FREE);
    vspace_unmap_pages(loader, new_mo_va, num_pages, MO_PAGE_BITS, VSPACE_FREE);

    return error;
}

int ads_copy(vspace_t *loader,
             vka_t *vka,
             ads_t *src_ads,
             ads_t *dst_ads,
             vmr_config_t *cfg)
{
    int error = 0;

    OSDB_PRINTF("%s VMR %p (type: %s, pages: %u) from ADS%d -> ADS%d\n",
                sel4gpi_share_degree_to_str(cfg->share_mode),
                cfg->start, human_readable_va_res_type(cfg->type),
                cfg->region_pages, src_ads->id, dst_ads->id);

    attach_node_t *src_attach_node;

    if (cfg->start == NULL &&
        cfg->type != SEL4UTILS_RES_TYPE_SHARED_FRAMES &&
        cfg->type != SEL4UTILS_RES_TYPE_OTHER &&
        cfg->type != SEL4UTILS_RES_TYPE_GENERIC)
    {
        src_attach_node = ads_get_res_by_type(src_ads, cfg->type);
        SERVER_GOTO_IF_COND(src_attach_node == NULL, "Given %s VMR config with no start address and no existing reservation\n",
                            human_readable_va_res_type(cfg->type));
    }
    else
    {
        src_attach_node = ads_get_res_by_vaddr(src_ads, cfg->start);
        SERVER_GOTO_IF_COND(src_attach_node == NULL, "Failed to find the attach node for vaddr: %p\n", cfg->start);
    }

    attach_node_t *new_attach_node;
    mo_t *old_mo;                   // original MO
    int num_pages;

    uintptr_t region_end = (uintptr_t)cfg->start + (cfg->region_pages * SIZE_BITS_TO_BYTES(MO_PAGE_BITS));

    error = ads_copy_reservation(src_ads, dst_ads, (uintptr_t)cfg->start, region_end, cfg->type,
                                 src_attach_node, &new_attach_node, &old_mo, &num_pages);
    SERVER_GOTO_IF_ERR(error, "Copying reservation failed\n");
    SERVER_GOTO_IF_COND(!src_attach_node->mo_attached, "No MO attached for source VMR reservation\n");

    switch (cfg->share_mode)
    {
    case GPI_SHARED:
        error = ads_attach_to_res(dst_ads, vka, new_attach_node, src_attach_node->mo_offset, old_mo);
        break;
    case GPI_COPY:
        error = ads_deep_copy(dst_ads, old_mo, num_pages, new_attach_node, src_attach_node);
        break;
    default:
        SERVER_GOTO_IF_COND(1, "Invalid sharing mode specified: %s\n", sel4gpi_share_degree_to_str(cfg->share_mode));
        break;
    }

    SERVER_GOTO_IF_ERR(error, "Failed to attach source MO (%d) to dst ADS (%d)\n", old_mo->id, dst_ads->id);

#if 0
    /* walk all the reservations */
    // (XXX) Arya: We may be able to just walk attach nodes instead of reservations (eventually?)
    while (from_sel4_res != NULL)
    {
        OSDB_PRINTF("Reservation: %p\n", (void *)from_sel4_res->start);

        if (from_sel4_res->start == (uintptr_t)omit_vaddr)
        {
            OSDB_PRINTF("Skipping the region, with start vaddr of %p\n", omit_vaddr);
            from_sel4_res = from_sel4_res->next;
            continue;
        }

        attach_node_t *new_attach_node;
        attach_node_t *old_attach_node; // original attach node
        mo_t *old_mo;                   // original MO
        error = ads_copy_reservation(src_ads, dst_ads, from_sel4_res->start, from_sel4_res->end, from_sel4_res->type, &old_attach_node, &new_attach_node, &old_mo, &num_pages);
        SERVER_GOTO_IF_ERR(error, "Copying reservation failed\n");

        if (!old_attach_node->mo_attached)
        {
            // If no MO attached, skip to next reservation
            OSDB_PRINTF("Skipping reservation at vaddr: %p, no MO attached\n", (void *)from_sel4_res->start);
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

            error = ads_deep_copy(dst_ads, old_mo, num_pages, new_attach_node, old_attach_node);
            SERVER_GOTO_IF_ERR(error, "Failed to deep copy reservation\n");
        }

        // Move to next node.
        from_sel4_res = from_sel4_res->next;
    }
    OSDB_PRINTF("New vspace details:\n");
    // sel4utils_walk_vspace(to, NULL);
    // For each reservation: call share_mem_at_vaddr
    return 0;
#endif
err_goto:
    // TODO if error: cleanup reservation
    return error;
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
                 const char *image_name,
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

int ads_write_arguments(sel4utils_process_t *process,
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

    process->thread.initial_stack_pointer = (void *)initial_stack_pointer;
    *ret_init_stack = (void *)initial_stack_pointer;
    return 0;
}
