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
#include <cpio/cpio.h>
#include <sel4utils/helpers.h>
#include <sel4runtime/auxv.h>
#include <sel4runtime.h>

#include <sel4gpi/ads_obj.h>
#include <sel4gpi/ads_component.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/cap_tracking.h>
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/model_exporting.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/pd_component.h>
#include <sel4gpi/gpi_elf.h>

#define MAX_MO_RR 10000

// Defined for utility printing macros
#define DEBUG_ID ADS_DEBUG
#define SERVER_ID ADSSERVS

/* This is doesn't belong here but we need it */
extern char _cpio_archive[];
extern char _cpio_archive_end[];

typedef struct _pd pd_t;
typedef struct _mo mo_t;
typedef struct _cpu cpu_t;

/**
 * Callback when an attach node is deleted
 * Perform cleanup here
 */
static void on_attach_registry_delete(resource_registry_node_t *node_gen, void *ads_v)
{
    attach_node_t *node = (attach_node_t *)node_gen;
    ads_t *ads = (ads_t *)ads_v;

    OSDB_PRINTF("Deleting attach node from ADS (%d), vaddr %p, mo_attached %d, type %s\n",
                ads->id, node->vaddr, node->mo_attached, human_readable_va_res_type(node->type));

    // Remove the reservation
    sel4utils_free_reservation(ads->vspace, node->res);

    // Remove the attached MO
    if (node->mo_attached)
    {
        // Unmap the pages
        // (XXX) Arya: I believe we want VSPACE_PRESERVE here
        // Otherwise, sel4utils will attempt to free the frame caps and their corresponding untyped
        // Which we do not want, since the MO continues to exist
        sel4utils_unmap_pages(ads->vspace, node->vaddr + node->mo_offset,
                              node->n_frames, node->page_bits, VSPACE_PRESERVE);

        // Free the frame caps (duplicated for this attach)
        for (int i = 0; i < node->n_frames; i++)
        {
            cspacepath_t path;
            vka_cspace_make_path(get_ads_component()->server_vka, node->frame_caps[i], &path);
            vka_cnode_delete(&path);
            vka_cspace_free_path(get_ads_component()->server_vka, path);
        }

        // (XXX) Arya: IMPORTANT
        // Something is broken with morecore when I free this
        free(node->frame_caps);

        // Decrement the refcount of the MO
        // It is important to do this after freeing the caps, since if the MO is freed,
        // it will return the frames to the VKA, and the VKA expects that there are no copies
        resource_component_dec(get_mo_component(), node->mo_id);
    }
}

int ads_initialize(ads_t *ads)
{
    int error = 0;

    // Initialize VMR registry
    resource_registry_initialize(&ads->attach_registry, on_attach_registry_delete, (void *)ads);
    resource_registry_initialize(&ads->attach_id_to_vaddr_map, NULL, NULL);

    /* The root task holds the ADS by default */
    error = pd_add_resource_by_id(get_gpi_server()->rt_pd_id,
                                  make_res_id(GPICAP_TYPE_ADS, get_ads_component()->space_id, ads->id),
                                  seL4_CapNull, seL4_CapNull, seL4_CapNull);
    SERVER_GOTO_IF_ERR(error, "Failed to add new ADS to root task\n");

err_goto:
    return error;
}

int ads_new(ads_t *ads,
            vka_t *vka,
            vspace_t *loader,
            void *arg0)
{
    int error = 0;

    // Allocate a vspace
    ads->vspace = calloc(1, sizeof(vspace_t));
    SERVER_GOTO_IF_COND(ads->vspace == NULL, "Failed to allocate vspace\n");

    vspace_t *new_vspace = ads->vspace;
    assert(new_vspace != NULL);

    // Allocate process structure for cookies
    ads->process_for_cookies = calloc(1, sizeof(sel4utils_process_t));
    SERVER_GOTO_IF_COND(ads->process_for_cookies == NULL, "Failed to allocate process struct for cookies in ads_new\n");

    // Give vspace root
    vka_object_t *vspace_root_object = calloc(1, sizeof(vka_object_t));
    assert(vspace_root_object != NULL);

    error = vka_alloc_vspace_root(vka, vspace_root_object);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate page directory for new process\n");

    // Allocate alloc data
    sel4utils_alloc_data_t *alloc_data = calloc(1, sizeof(sel4utils_alloc_data_t));
    SERVER_GOTO_IF_COND(alloc_data == NULL, "Failed to allocate memory for alloc data\n");

    // Assign an asid pool
    SERVER_GOTO_IF_COND(!config_set(CONFIG_X86_64) &&
                            assign_asid_pool(seL4_CapInitThreadASIDPool, vspace_root_object->cptr) != seL4_NoError,
                        "Failed to allocate asid pool\n");

    // Create empty vspace
    error = sel4utils_get_vspace(
        loader,
        new_vspace,
        alloc_data,
        vka,
        vspace_root_object->cptr,
        sel4utils_allocated_object, // (XXX) Arya: could we just set this to null? Or our own fn

        /*
            sel4utils_allocated_object expects a process struct as a cookie
            Instead use a different function which suited are needs better.
        */

        ads->process_for_cookies);

    SERVER_GOTO_IF_ERR(error, "Failed to get new vspace while making copy\n");

    ads->root_page_dir = vspace_root_object;

    // Perform any initialization of metadata
    error = ads_initialize(ads);

    return error;

err_goto:
    free(alloc_data);
    free(ads->vspace);
    return error;
}

int ads_reserve(ads_t *ads,
                void *vaddr,
                uint32_t num_pages,
                size_t size_bits,
                sel4utils_reservation_type_t vmr_type,
                bool cacheable,
                seL4_CapRights_t rights,
                attach_node_t **ret_node)
{
    int error = 0;

    // (XXX) Arya: should shift this option to client api
    cacheable = vmr_type == SEL4UTILS_RES_TYPE_DEVICE ? false : cacheable;
    vspace_t *target = ads->vspace;

    /* Reserve the range in the vspace */
    reservation_t res;
    if (vaddr == NULL)
    {
        res = sel4utils_reserve_range_aligned(target,
                                              num_pages * SIZE_BITS_TO_BYTES(size_bits),
                                              size_bits,
                                              rights,
                                              cacheable,
                                              &vaddr);
    }
    else
    {
        res = sel4utils_reserve_range_at(target,
                                         vaddr,
                                         num_pages * SIZE_BITS_TO_BYTES(size_bits),
                                         rights, cacheable);
    }

    SERVER_GOTO_IF_COND(res.res == NULL, "Failed to reserve range\n");

    /* Set the reservation type */
    sel4utils_res_t *sel4utils_res = reservation_to_res(res);
    sel4utils_res->type = vmr_type;

    /* Track the VMR in registry */
    attach_node_t *attach_node = calloc(1, sizeof(attach_node_t));
    attach_node_map_t *attach_node_map_entry = calloc(1, sizeof(attach_node_map_t));

    SERVER_GOTO_IF_COND(attach_node == NULL || attach_node_map_entry == NULL,
                        "Failed to allocate registry entry for ADS reservation.\n");

    // Map a shorter attach node ID to vaddr
    attach_node_map_entry->vaddr = vaddr;
    resource_registry_insert_new_id(&ads->attach_id_to_vaddr_map, (resource_registry_node_t *)attach_node_map_entry);

    // The attach node is keyed by vaddr
    memset((void *)attach_node, 0, sizeof(attach_node_t));
    attach_node->res = res;
    attach_node->vaddr = vaddr;
    attach_node->map_entry = attach_node_map_entry;
    attach_node->type = vmr_type;
    attach_node->n_pages = num_pages;
    attach_node->gen.object_id = (uint64_t)vaddr;
    attach_node->page_bits = size_bits;
    attach_node->cacheable = cacheable;
    attach_node->rights = rights;
    resource_registry_insert(&ads->attach_registry, (resource_registry_node_t *)attach_node);

    // The root task holds the VMR by default
    uint64_t vmr_id = attach_node_map_entry->gen.object_id;
    error = pd_add_resource_by_id(get_gpi_server()->rt_pd_id,
                                  make_res_id(GPICAP_TYPE_VMR, ads->id, vmr_id),
                                  seL4_CapNull, seL4_CapNull, seL4_CapNull);
    SERVER_GOTO_IF_ERR(error, "Failed to add new VMR to root task\n");

    *ret_node = attach_node;

err_goto:
    return error;
}

attach_node_t *ads_get_res_by_id(ads_t *ads, uint64_t res_id)
{
    attach_node_map_t *map_entry = (attach_node_map_t *)resource_registry_get_by_id(&ads->attach_id_to_vaddr_map, res_id);
    if (map_entry == NULL)
    {
        return NULL;
    }
    return ads_get_res_by_vaddr(ads, map_entry->vaddr);
}

attach_node_t *ads_get_res_by_vaddr(ads_t *ads, void *vaddr)
{
    return (attach_node_t *)resource_registry_get_by_id(&ads->attach_registry, (uint64_t)vaddr);
}

linked_list_t *ads_get_res_by_type(ads_t *src_ads, sel4utils_reservation_type_t vmr_type)
{
    linked_list_t *found_nodes = linked_list_new();
    resource_registry_node_t *current, *tmp;
    HASH_ITER(hh, src_ads->attach_registry.head, current, tmp)
    {
        attach_node_t *node = (attach_node_t *)current;
        if (node->type == vmr_type)
        {
            linked_list_insert(found_nodes, node);
        }
    }

    return found_nodes;
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
    SERVER_GOTO_IF_COND(mo->page_bits != reservation->page_bits,
                        "Trying to attach MO of page size %zu to reservation of page size %zu\n",
                        SIZE_BITS_TO_BYTES(mo->page_bits),
                        SIZE_BITS_TO_BYTES(reservation->page_bits));

    OSDB_PRINTF("attaching mo (id %u, pages: %u, page size: %zu)"
                "to reservation(vaddr: %p, type: %s, pages: %u) offset %zu\n",
                mo->id, mo->num_pages,
                SIZE_BITS_TO_BYTES(mo->page_bits),
                reservation->vaddr,
                human_readable_va_res_type(reservation->type),
                reservation->n_pages,
                offset);

    reservation->frame_caps = calloc(mo->num_pages, sizeof(seL4_CPtr));

    error = copy_frame_caps_for_mapping(mo->frame_caps_in_root_task, reservation->frame_caps, mo->num_pages);
    SERVER_GOTO_IF_ERR(error, "Failed to copy frame caps for attachment\n");

    /* Map the frame caps into the vspace */
    error = sel4utils_map_pages_at_vaddr(ads->vspace,
                                         reservation->frame_caps,
                                         NULL,
                                         reservation->vaddr + offset,
                                         mo->num_pages,
                                         mo->page_bits,
                                         reservation->res);
    SERVER_GOTO_IF_ERR(error, "Failed to map pages\n");

    /* Track the attachment */
    reservation->mo_attached = true;
    reservation->mo_offset = offset;
    reservation->mo_id = mo->id;
    reservation->n_frames = mo->num_pages;
    // memcpy(reservation->frame_caps, frame_caps, sizeof(seL4_CPtr) * mo->num_pages);

#if TRACK_MAP_RELATIONS
    /* Map the VMR to the MO */
    uint64_t vmr_universal_id = compact_res_id(GPICAP_TYPE_VMR, ads->id, reservation->map_entry->gen.object_id);
    uint64_t mo_id = compact_res_id(GPICAP_TYPE_MO, get_mo_component()->space_id, mo->id);
    error = pd_component_map_resources(get_gpi_server()->rt_pd_id, vmr_universal_id, mo_id);
    SERVER_GOTO_IF_ERR(error, "Failed to map VMR to MO\n");
#endif

    /* Track this attachment as a refcount to the MO */
    error = resource_component_inc(get_mo_component(), mo->id);
    SERVER_GOTO_IF_ERR(error, "Failed to increment refcount of MO\n");

err_goto:
    return error;
}

int ads_attach(ads_t *ads,
               vka_t *vka,
               void *vaddr,
               mo_t *mo,
               bool cacheable,
               seL4_CapRights_t rights,
               void **ret_vaddr,
               sel4utils_reservation_type_t vmr_type)
{
    int error = 0;

    /* Reserve the VMR */
    attach_node_t *attach_node;
    error = ads_reserve(ads, vaddr, mo->num_pages, mo->page_bits, vmr_type, cacheable, rights, &attach_node);

    if (error)
    {
        ZF_LOGE("Failed to reserve region\n");
        return 1;
    }

    if (error)
    {
        ZF_LOGE("Failed to map VMR resource to MO resource\n");
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
    int error = 0;

    // Add the attach node for this region
    attach_node_t *attach_node = calloc(1, sizeof(attach_node_t));
    attach_node_map_t *attach_node_map_entry = calloc(1, sizeof(attach_node_map_t));
    SERVER_GOTO_IF_COND(attach_node == NULL || attach_node_map_entry == NULL,
                        "Failed to allocate attach node for forged attach\n");

    // Map a shorter attach node ID to vaddr
    attach_node_map_entry->vaddr = (void *)res->start;
    resource_registry_insert_new_id(&ads->attach_id_to_vaddr_map, (resource_registry_node_t *)attach_node_map_entry);

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
    attach_node->page_bits = mo->page_bits;

    resource_registry_insert(&ads->attach_registry, (resource_registry_node_t *)attach_node);

    // Track this attachment as a refcount to the MO
    error = resource_component_inc(get_mo_component(), mo->id);
    SERVER_GOTO_IF_ERR(error, "Failed to increment refcount of MO\n");

err_goto:
    return error;
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
        ZF_LOGE("Failed to find VMR (%p) to remove\n", vaddr);
        return 1;
    }

    // Remove the attach node
    resource_registry_delete(&ads->attach_id_to_vaddr_map, (resource_registry_node_t *)node->map_entry);
    resource_registry_delete(&ads->attach_registry, (resource_registry_node_t *)node);

err_goto:
    return error;
}

int ads_bind(ads_t *ads, vka_t *vka, seL4_CPtr *cpu_cap)
{
    return 0;
}

gpi_model_node_t *ads_dump_rr(ads_t *ads, model_state_t *ms, gpi_model_node_t *pd_node)
{
    // Add the ADS resource space
    gpi_model_node_t *ads_space_node = get_resource_space_node(ms, GPICAP_TYPE_ADS, ads->id);

    if (!ads_space_node)
    {
        ads_space_node = add_resource_space_node(ms, GPICAP_TYPE_ADS, ads->id, false);
    }

    if (!ads_space_node->extracted)
    {
        add_edge(ms, GPI_EDGE_TYPE_HOLD, get_root_node(ms), ads_space_node); // the RT holds this resource space

        for (attach_node_t *res = (attach_node_t *)ads->attach_registry.head; res != NULL; res = (attach_node_t *)res->gen.hh.next)
        {
            /* Add the VMR node */
            // VMR is sometimes an implicit resource (eg. MO attached without reservation)
            // (XXX) Linh: we are casting the vaddr to a 4-byte int, which may not be enough bytes to display it
            //             we could increase the CSV string size to fit 8-byte object IDs
            // (XXX) Arya: I am using attach ID instead of vaddr as the object ID now to avoid this issue
            gpi_model_node_t *vmr_node = add_resource_node(
                ms,
                make_res_id(GPICAP_TYPE_VMR, ads->id, (uint32_t)res->map_entry->gen.object_id),
                true);
            add_edge(ms, GPI_EDGE_TYPE_SUBSET, vmr_node, ads_space_node);
            add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, vmr_node);
            // set the VMR type, number of pages, and page size as extra data on the node
            char extra[CSV_MAX_STRING_SIZE] = {0};
            snprintf(extra, CSV_MAX_STRING_SIZE, "%s_%d_%zu",
                     human_readable_va_res_type(res->type),
                     res->n_pages, res->page_bits);
            set_node_extra(vmr_node, extra);

            /* Add the relation from VMR to MO node, if there is one */
            if (res->mo_attached)
            {
                gpi_model_node_t *mo_node = get_resource_node(ms, make_res_id(GPICAP_TYPE_MO,
                                                                              get_mo_component()->space_id, res->mo_id));

                if (!mo_node)
                {
                    mo_node = add_resource_node(ms,
                                                make_res_id(GPICAP_TYPE_MO, get_mo_component()->space_id, res->mo_id),
                                                true);
                    // mark the node to be dumped later on, since we've only added it here for the MAP edge
                }

                add_edge(ms, GPI_EDGE_TYPE_MAP, vmr_node, mo_node);
            }
        }

        ads_space_node->extracted = true;
    }

    return ads_space_node;
}

/**
 * @brief deep copies the contents of src_mo to dst_ads, a reservation for the VMR in dst_ads must already exist
 * (XXX) Linh: this is a legacy function that can be deprecated, do we need to keep it around for any purpose?
 *
 * @param dst_ads ADS to copy MO contents into
 * @param src_mo MO of data to be copied
 * @param new_attach_node the attach node in dst_ads for the reservation
 * @param old_attach_node the original attach node for src_mo
 * @return int 0 on success, 1 on failure
 */
static int ads_deep_copy(ads_t *dst_ads, mo_t *src_mo, attach_node_t *new_attach_node, attach_node_t *old_attach_node)
{
    int error = 0;
    int num_pages = old_attach_node->n_pages;

    // Make a new MO
    // The "client" to hold this MO is the root task
    mo_t *new_mo;
    error = mo_component_allocate_rt(num_pages, &new_mo);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate a new MO for deep copy\n");

    // Attach the new MO in the new ADS
    error = ads_attach_to_res(dst_ads,
                              get_ads_component()->server_vka,
                              new_attach_node,
                              old_attach_node->mo_offset, new_mo);
    SERVER_GOTO_IF_ERR(error, "Failed to attach pages to region\n");

    // Reduce the refcount of the MO since only the root task is holding it
    resource_component_dec(get_mo_component(), new_mo->id);

    // Attach the MOs
    void *old_mo_va;
    error = ads_component_attach_to_rt(src_mo->id, &old_mo_va);
    SERVER_GOTO_IF_ERR(error, "Failed to map old MO for deep copy\n");

    void *new_mo_va;
    error = ads_component_attach_to_rt(new_mo->id, &new_mo_va);
    SERVER_GOTO_IF_ERR(error, "Failed to map new MO for deep copy\n");

    // Copy data
    memcpy(new_mo_va, old_mo_va, num_pages * SIZE_BITS_TO_BYTES(MO_PAGE_BITS));

    // Remove the MOs
    error = ads_component_remove_from_rt(old_mo_va);
    SERVER_GOTO_IF_ERR(error, "Failed to unmap old MO for deep copy\n");

    error = ads_component_remove_from_rt(new_mo_va);
    SERVER_GOTO_IF_ERR(error, "Failed to unmap new MO for deep copy\n");

err_goto:

    return error;
}

int ads_shallow_copy(vspace_t *loader,
                     vka_t *vka,
                     ads_t *src_ads,
                     ads_t *dst_ads,
                     vmr_config_t *cfg)
{
    int error = 0;
    linked_list_t *src_attaches = NULL;

    SERVER_GOTO_IF_COND(cfg->type <= SEL4UTILS_RES_TYPE_NONE || cfg->type >= SEL4UTILS_RES_TYPE_MAX,
                        "Invalid reservation type given: %d\n", cfg->type);

    if (cfg->start == NULL &&
        cfg->type != SEL4UTILS_RES_TYPE_SHARED_FRAMES &&
        cfg->type != SEL4UTILS_RES_TYPE_GENERIC)
    {
        src_attaches = ads_get_res_by_type(src_ads, cfg->type);
        SERVER_GOTO_IF_COND(src_attaches->count == 0,
                            "Given %s VMR config with no start address and no existing reservation(s)\n",
                            human_readable_va_res_type(cfg->type));
    }
    else
    {
        src_attaches = linked_list_new();
        attach_node_t *attach_node = ads_get_res_by_vaddr(src_ads, cfg->start);
        SERVER_GOTO_IF_COND(attach_node == NULL, "Failed to find the attach node for vaddr: %p\n", cfg->start);
        linked_list_insert(src_attaches, attach_node);
    }

    /* if we wanted to map a certain VMR at a different address in the destination ADS, there can only be
     * one source reservation, as the requestor can only provide one destination vaddr.
     */
    SERVER_GOTO_IF_COND(src_attaches->count > 1 && cfg->dest_start != NULL,
                        "Specified a destination vaddr (%p) for a source VMR(%s, %p) with non-contiguous regions\n",
                        cfg->dest_start, human_readable_va_res_type(cfg->type), cfg->start);

    for (linked_list_node_t *curr = src_attaches->head; curr != NULL; curr = curr->next)
    {
        attach_node_t *src_attach_node = (attach_node_t *)curr->data;
        OSDB_PRINTF("%s VMR %p (type: %s, pages: %u) from ADS%d -> ADS%d\n",
                    sel4gpi_share_degree_to_str(cfg->share_mode),
                    src_attach_node->vaddr, human_readable_va_res_type(cfg->type),
                    src_attach_node->n_pages, src_ads->id, dst_ads->id);

        attach_node_t *new_attach_node;
        void *attach_vaddr = cfg->dest_start != NULL && src_attaches->count == 1
                                 ? cfg->dest_start
                                 : (void *)src_attach_node->vaddr;
        error = ads_reserve(dst_ads, attach_vaddr, src_attach_node->n_pages,
                            src_attach_node->page_bits, src_attach_node->type,
                            src_attach_node->cacheable, src_attach_node->rights,
                            &new_attach_node);
        SERVER_GOTO_IF_ERR(error, "Failed to reserve region\n");

        // Find the original MO
        mo_t *old_mo;
        SERVER_GOTO_IF_COND(!src_attach_node->mo_attached, "No MO attached to source VMR, cannot copy\n");
        mo_component_registry_entry_t *old_mo_reg_entry =
            (mo_component_registry_entry_t *)resource_component_registry_get_by_id(get_mo_component(),
                                                                                   src_attach_node->mo_id);
        SERVER_GOTO_IF_COND(old_mo_reg_entry == NULL,
                            "Failed to find the MO (%ld) for vaddr: %p\n",
                            src_attach_node->mo_id, (void *)src_attach_node->vaddr);
        old_mo = &old_mo_reg_entry->mo;

        error = ads_attach_to_res(dst_ads, vka, new_attach_node, src_attach_node->mo_offset, old_mo);
        SERVER_GOTO_IF_ERR(error, "Failed to attach source MO (%d) to dst ADS (%d)\n", old_mo->id, dst_ads->id);
    }

err_goto:
    // TODO if error: cleanup reservation
    linked_list_destroy(src_attaches, false);
    return error;
}

void ads_destroy(ads_t *ads)
{
    /* Destroy the hash tables of attach nodes */
    // (XXX) Arya: This can trigger sys_munmap which is not supported
    resource_registry_node_t *current, *tmp;
    HASH_ITER(hh, ads->attach_registry.head, current, tmp)
    {
        attach_node_t *node = (attach_node_t *)current;
        resource_registry_delete(&ads->attach_id_to_vaddr_map, (resource_registry_node_t *)node->map_entry);
        resource_registry_delete(&ads->attach_registry, current);
    }

    /* tear down the vspace */

    /**
     * (XXX) Arya:
     * If VKA is VSPACE_FREE instead of VSPACE_PRESERVE
     * this will also free the forged MO regions like ELF region
     *
     * VSPACE_FREE does not currently work, it seems that some freed frame
     * has another cap to it and causes the VKA to break
     */
    vspace_tear_down(ads->vspace, VSPACE_PRESERVE);

    /**
     * we should not need to free any objects created by the vspace,
     * as they should be all MOs
     * (XXX) Arya: Make sure this is the case
     */
}

/* ======================================= CONVENIENCE FUNCTIONS (NOT PART OF FRAMEWORK) ================================================= */

int ads_load_elf(ads_t *loadee,
                 ads_t *loader,
                 pd_t *pd,
                 const char *image_name,
                 void **ret_entry_point)
{
    int error;
    seL4_CPtr slot;
    vka_t *server_vka = get_ads_component()->server_vka;

    unsigned long size;
    unsigned long cpio_len = _cpio_archive_end - _cpio_archive;
    char const *file = cpio_get_file(_cpio_archive, cpio_len, image_name, &size);
    elf_t elf;
    elf_newFile(file, size, &elf);

    *ret_entry_point = sel4gpi_elf_load(loadee, loader, server_vka, server_vka, &elf);
    SERVER_GOTO_IF_COND(*ret_entry_point == NULL, "Failed to load elf file\n");

    pd->sysinfo = sel4gpi_elf_get_vsyscall(&elf);

    /* Retrieve the ELF phdrs */
    pd->num_elf_phdrs = sel4gpi_elf_num_phdrs(&elf);
    pd->elf_phdrs = calloc(pd->num_elf_phdrs, sizeof(Elf_Phdr));
    SERVER_GOTO_IF_COND(!pd->elf_phdrs, "Failed to allocate memory for elf phdr information\n");

    sel4gpi_elf_read_phdrs(&elf, pd->num_elf_phdrs, pd->elf_phdrs);
    pd->pagesz = PAGE_SIZE_4K;

    return 0;

err_goto:
    if (pd->elf_phdrs)
    {
        free(pd->elf_phdrs);
    }

    return 1;
}

int ads_write_arguments(pd_t *pd,
                        vspace_t *loadee_vspace,
                        void *ipc_buf_addr,
                        void *stack_top,
                        int argc,
                        char *argv[],
                        void **ret_init_stack)
{
    /* define an envp and auxp */
    int error;
    int envc = 0;
    char *envp[] = {};
    vspace_t *vspace = get_ads_component()->server_vspace;
    vka_t *vka = get_ads_component()->server_vka;

    uintptr_t initial_stack_pointer = (uintptr_t)stack_top - sizeof(seL4_Word);

    /* Copy the elf headers */
    uintptr_t at_phdr;
    error = sel4utils_stack_write(vspace, loadee_vspace, vka, pd->elf_phdrs,
                                  pd->num_elf_phdrs * sizeof(Elf_Phdr), &initial_stack_pointer);
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
    auxv[0].a_un.a_val = pd->pagesz;
    auxv[1].a_type = AT_PHDR;
    auxv[1].a_un.a_val = at_phdr;
    auxv[2].a_type = AT_PHNUM;
    auxv[2].a_un.a_val = pd->num_elf_phdrs;
    auxv[3].a_type = AT_PHENT;
    auxv[3].a_un.a_val = sizeof(Elf_Phdr);
    auxv[4].a_type = AT_SEL4_IPC_BUFFER_PTR;
    auxv[4].a_un.a_val = (uint64_t)ipc_buf_addr;
    auxv[5].a_type = AT_SEL4_TCB;
    auxv[5].a_un.a_val = seL4_CapNull; // Is it ok that we don't give the process access to its TCB?

    auxv[6].a_type = AT_OSM_SHARED_DATA;
    auxv[6].a_un.a_val = (uint64_t)pd->shared_data_in_PD;

    if (pd->sysinfo)
    {
        auxv[7].a_type = AT_SYSINFO;
        auxv[7].a_un.a_val = pd->sysinfo;
        auxc++;
    }

    uintptr_t dest_argv[argc];
    uintptr_t dest_envp[envc];

    /* write all the strings into the stack */
    /* Copy over the user arguments */
    error = sel4utils_stack_copy_args(vspace, loadee_vspace, vka, argc, argv, dest_argv, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
    /* copy the environment */
    error = sel4utils_stack_copy_args(vspace, loadee_vspace, vka, envc, envp, dest_envp, &initial_stack_pointer);
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
    error = sel4utils_stack_write_constant(vspace, loadee_vspace, vka, 0, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    error = sel4utils_stack_write_constant(vspace, loadee_vspace, vka, 0, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    /* write aux */
    error = sel4utils_stack_write(vspace, loadee_vspace, vka, auxv, sizeof(auxv[0]) * auxc, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    /* Null terminate environment */
    error = sel4utils_stack_write_constant(vspace, loadee_vspace, vka, 0, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    /* write environment */
    error = sel4utils_stack_write(vspace, loadee_vspace, vka, dest_envp, sizeof(dest_envp), &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    /* Null terminate arguments */
    error = sel4utils_stack_write_constant(vspace, loadee_vspace, vka, 0, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    /* write arguments */
    error = sel4utils_stack_write(vspace, loadee_vspace, vka, dest_argv, sizeof(dest_argv), &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }
    /* Push argument count */
    error = sel4utils_stack_write_constant(vspace, loadee_vspace, vka, argc, &initial_stack_pointer);
    if (error)
    {
        ZF_LOGE("%s: Failed to write stack\n", __func__);
        return -1;
    }

    assert(initial_stack_pointer % (2 * sizeof(seL4_Word)) == 0);

    *ret_init_stack = (void *)initial_stack_pointer;
    return 0;
}
