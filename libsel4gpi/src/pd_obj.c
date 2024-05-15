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
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <simple/simple_helpers.h>
#include <utils/uthash.h>
#include <cpio/cpio.h>

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
#include <sel4gpi/resource_server_clientapi.h>
#include <sel4gpi/pd_utils.h>

#define CSPACE_SIZE_BITS 17
#define ELF_LIB_DATA_SECTION ".lib_data"
#define ELF_APP_DATA_SECTION ".data"

/* This is doesn't belong here but we need it */
extern char _cpio_archive[];
extern char _cpio_archive_end[];

// Defined for utility printing macros
#define DEBUG_ID PD_DEBUG
#define SERVER_ID PDSERVS

static int pd_setup_cspace(pd_t *pd, vka_t *vka);
static int pd_dump_internal(pd_t *pd, model_state_t *ms, cspacepath_t rr_frame_path, void *rr_local_vaddr);

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
    vka_cspace_make_path(get_gpi_server()->server_vka, cap, &src);
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

int pd_add_resource(pd_t *pd, gpi_cap_t type, uint32_t res_id, uint32_t ns_id,
                    seL4_CPtr slot_in_RT, seL4_CPtr slot_in_PD, seL4_CPtr slot_in_serverPD)
{
    // Unique resource ID is the badge with the following fields: type, ns_id, res_id
    uint64_t res_node_id = gpi_new_badge(type, 0, 0, ns_id, res_id);
    pd_hold_node_t *node = (pd_hold_node_t *)resource_server_registry_get_by_id(&pd->hold_registry, res_node_id);

    if (node != NULL)
    {
        OSDB_PRINTF("Warning: adding resource with existing ID (%ld), do not insert again\n", res_node_id);
        badge_print(res_node_id);
    }
    else
    {
        node = calloc(1, sizeof(pd_hold_node_t));
        node->type = type;
        node->res_id = res_id;
        node->ns_id = ns_id;
        node->slot_in_RT_Debug = slot_in_RT;
        node->slot_in_PD_Debug = slot_in_PD;
        node->slot_in_ServerPD_Debug = slot_in_serverPD;
        node->gen.object_id = res_node_id;

        resource_server_registry_insert(&pd->hold_registry, (resource_server_registry_node_t *)node);
    }

    return 0;
}

static int pd_rde_find_idx(pd_t *pd,
                           gpi_cap_t type,
                           uint32_t ns_id)
{
    int idx = -1;

    for (int i = 0; i < MAX_NS_PER_RDE; i++)
    {
        if (pd->init_data->rde[type][i].type.type == GPICAP_TYPE_NONE)
        {
            break;
        }
        else if (pd->init_data->rde[type][i].ns_id == ns_id)
        {
            idx = i;
            break;
        }
    }

    return idx;
}

osmosis_rde_t *pd_rde_get(pd_t *pd,
                          gpi_cap_t type,
                          uint32_t ns_id)
{
    int idx = pd_rde_find_idx(pd, type, ns_id);

    if (idx == -1)
    {
        return NULL;
    }
    else
    {
        return &pd->init_data->rde[type][idx];
    }
}

int pd_add_rde(pd_t *pd,
               rde_type_t type,
               uint32_t manager_id,
               uint32_t ns_id,
               seL4_CPtr server_ep)
{
    int idx = -1;

    for (int i = 0; i < MAX_NS_PER_RDE; i++)
    {
        if (pd->init_data->rde[type.type][i].type.type == GPICAP_TYPE_NONE)
        {
            idx = i;
            break;
        }
    }

    if (idx == -1)
    {
        OSDB_PRINTF("No more RDE NS slots available for type %d\n", type.type);
        return 1;
    }

    pd->init_data->rde[type.type][idx].manager_id = manager_id;
    /* we don't really need to keep this if we index by type, but let's just keep it around for now */
    pd->init_data->rde[type.type][idx].type = type;
    pd->init_data->rde[type.type][idx].slot_in_RT = server_ep;
    pd->init_data->rde[type.type][idx].ns_id = ns_id;
    uint32_t client_id = pd->id;

    // Badge the raw endpoint for the client PD
    cspacepath_t src, dest;
    vka_cspace_make_path(get_gpi_server()->server_vka, server_ep, &src);

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

    pd->init_data->rde[type.type][idx].slot_in_PD = dest.capPtr;

    OSDB_PRINTF("Added new RDE of type %d to PD %d, in slot %d, with badge %lx\n", type.type, client_id, (int)dest.capPtr, badge_val);

    pd->init_data->rde_count++;
    return 0;
}

static void pd_held_resource_on_delete(resource_server_registry_node_t *node_gen)
{
    int error = 0;
    pd_hold_node_t *node = (pd_hold_node_t *)node_gen;

    OSDB_PRINTF("Freeing resource %s_%d_%d\n", cap_type_to_str(node->type), node->ns_id, node->res_id);

    // If the resource is a core resource, free it directly
    // Decrement the registry entry's count, and if it reaches zero, the resource will be freed
    switch (node->type)
    {
    case GPICAP_TYPE_ADS:
        error = resource_component_dec(get_ads_component(), node->res_id);
        break;
    case GPICAP_TYPE_CPU:
        error = cpu_component_dec(node->res_id);
        break;
    case GPICAP_TYPE_MO:
        error = mo_component_dec(node->res_id);
        break;
    case GPICAP_TYPE_PD:
        // (XXX) Arya: I think we do not want to destroy a PD when the refcount reaches zero
        // If it dies on its own, then it will be destroyed
        break;
    case GPICAP_TYPE_VMR:
        // NS ID is the ADS, res ID is the VMR
        error = ads_component_rm_by_id(node->ns_id, node->res_id);
        break;
    default:
        // Otherwise, call the manager PD
        // (XXX) Arya: TODO implement
        // OSDB_PRINTERR("Not implemented: delete PD's held non-core resource %s-%d\n", cap_type_to_str(node->type), node->res_id);
        break;
    }

    if (error)
    {
        OSDB_PRINTERR("Warning: Could not free PD's held resource %s-%d\n", cap_type_to_str(node->type), node->res_id);
    }
}

int pd_new(pd_t *pd,
           vka_t *server_vka,
           vspace_t *server_vspace,
           void *arg0)
{
    int error;

    OSDB_PRINTF("new PD: \n");

    // Initialize the hold registry
    resource_server_initialize_registry(&pd->hold_registry, pd_held_resource_on_delete);

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
    error = forge_mo_cap_from_frames(&frame.cptr, 1, server_vka, pd->id,
                                     &pd->init_data_mo.badged_server_ep_cspath.capPtr, &rde_mo_obj);
    pd->init_data_mo_id = rde_mo_obj->id;

    if (error)
    {
        ZF_LOGE("Couldn't forge an MO for PD's init data\n");
    }

    // Track the init data MO in RT only
    pd_add_resource_by_id(get_gpi_server()->rt_pd_id, GPICAP_TYPE_MO, rde_mo_obj->id, NSID_DEFAULT,
                          pd->init_data_mo.badged_server_ep_cspath.capPtr, seL4_CapNull, pd->init_data_mo.badged_server_ep_cspath.capPtr);

    // Setup init data
    pd->init_data->rde_count = 0;
    memset(pd->init_data->rde, 0, sizeof(osmosis_rde_t) * MAX_PD_OSM_RDE);

    error = pd_setup_cspace(pd, get_pd_component()->server_vka);
    assert(error == 0);

    return error;
}

void pd_destroy(pd_t *pd, vka_t *server_vka, vspace_t *server_vspace)
{
    /* below is copied from sel4utils_destroy_process */
    /* (XXX) Arya: eventually should be repartitioned to other components */
    sel4utils_process_t *process = &pd->proc;
    vka_t *vka = server_vka;

    /* destroy the cnode */
    if (process->own_cspace)
    {
        cspacepath_t path;
        vka_cspace_make_path(vka, process->cspace.cptr, &path);
        /* need to revoke the cnode to remove any self references that would keep the object
         * alive when we try to delete it */
        vka_cnode_revoke(&path);
        vka_free_object(vka, &process->cspace);
    }

    /* destroy the thread */
    sel4utils_clean_up_thread(vka, &process->vspace, &process->thread);

    /* ADS component destroys the vspace */

    /* destroy the endpoint */
    if (process->own_ep && process->fault_endpoint.cptr != 0)
    {
        vka_free_object(vka, &process->fault_endpoint);
    }

    /* destroy the page directory */
    if (process->own_vspace)
    {
        vka_free_object(vka, &process->pd);
    }

    /* Free elf information */
    if (process->elf_regions)
    {
        free(process->elf_regions);
    }

    if (process->elf_phdrs)
    {
        free(process->elf_phdrs);
    }
    /* end copied from sel4utils_destroy_process */

    /* Clean up metadata */
    if (pd->image_name)
    {
        free(pd->image_name);
    }

    // Hash table of holding resources
    // (XXX) Arya: This can trigger sys_munmap which is not supported
    // This also triggers resource deletion, if this PD held the last copy
    resource_server_registry_node_t *current, *tmp;
    HASH_ITER(hh, pd->hold_registry.head, current, tmp)
    {
        resource_server_registry_delete(&pd->hold_registry, current);
    }

    // Frame for init data
    // (XXX) Arya: TODO destroy the init data MO
    vspace_unmap_pages(server_vspace, pd->init_data, 1, seL4_PageBits, server_vka);

    // The PD's VKA/allocator are destroyed with allocator_mem_pool

    // Other caps in RT
    cspacepath_t path;
    vka_cspace_make_path(server_vka, pd->pd_cap_in_RT, &path);
    vka_cnode_delete(&path);
    vka_cspace_free_path(server_vka, path);
    // The CPtrs like "ads_cap_in_RT" should be destroyed when the ADS is destroyed
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
    cspace_single_level_t *cspace = calloc(1, sizeof(cspace_single_level_t));
    allocman_t *allocator = bootstrap_create_allocman(PD_ALLOCATOR_STATIC_POOL_SIZE,
                                                      pd->allocator_mem_pool);

    error = cspace_single_level_create(allocator, cspace, (struct cspace_single_level_config){.cnode = root, .cnode_size_bits = size_bits,
                                                                                              //.cnode_guard_bits = seL4_WordBits - pd->cspace_size_bits,
                                                                                              .cnode_guard_bits = guard_bits,
                                                                                              .first_slot = start_slot,
                                                                                              .end_slot = end_slot});
    if (error != seL4_NoError)
    {
        OSDB_PRINTF("%s: Failed to initialize single-level cspace for PD id %d.\n",
                    __FUNCTION__, pd->id);
        return -1;
    }

    error = allocman_attach_cspace(allocator, cspace_single_level_make_interface(cspace));
    if (error != seL4_NoError)
    {
        OSDB_PRINTF("%s: Failed to attach cspace to allocman for PD id %d.\n",
                    __FUNCTION__, pd->id);
        return -1;
    }

    allocman_make_vka(&pd->pd_vka, allocator);
    return 0;
}

static int pd_setup_cspace(pd_t *pd, vka_t *vka)
{
    int error;
    int size_bits = CSPACE_SIZE_BITS;
    pd->cnode_guard = api_make_guard_skip_word(seL4_WordBits - size_bits);

    error = vka_alloc_endpoint(vka, &pd->proc.fault_endpoint);
    ZF_LOGE_IFERR(error, "Failed to create PD's fault endpoint");

    error = vka_alloc_cnode_object(vka, size_bits, &pd->proc.cspace);
    ZF_LOGE_IFERR(error, "Failed to create cspace");
    if (error)
    {
        goto error;
    }
    pd->proc.cspace_size = size_bits;
    /* first slot is always 1, never allocate 0 as a cslot */
    pd->proc.cspace_next_free = 1;
    pd->init_data->cspace_root = PD_CAP_ROOT;

    /*  mint the cnode cap into the process cspace */
    cspacepath_t src;
    vka_cspace_make_path(vka, pd->proc.cspace.cptr, &src);
    cspacepath_t dest = {.capPtr = pd->proc.cspace_next_free, .root = src.capPtr, .capDepth = pd->proc.cspace_size};
    error = vka_cnode_mint(&dest, &src, seL4_AllRights, pd->cnode_guard);
    ZF_LOGE_IFERR(error, "Failed to mint PD's cnode into its cspace");
    pd->proc.cspace_next_free++;

    /* copy over initial caps to PD (XXX) Linh: disabling for now bc unclear if we need this */
#if 0
    /* copy fault endpoint cap into process cspace */
    vka_cspace_make_path(vka, fault_ep.cptr, &src);
    error = vka_cnode_copy(&dest, &src, seL4_AllRights);
    ZF_LOGE_IFERR("Failed to copy PD's fault EP to its cspace");
    cspace_next_free++;

    /* copy page directory cap into process cspace */
    vka_cspace_make_path(vka, process->pd.cptr, &src);
    error = vka_cnode_copy(&dest, &src, seL4_AllRights);
    ZF_LOGE_IFERR(error, "Failed to copy PD's page directory cap to its cspace");
    cspace_next_free++;

    if (!config_set(CONFIG_X86_64))
    {
        vka_cspace_make_path(vka, seL4_CapInitThreadASIDPool, &src);
        error = vka_cnode_copy(&dest, &src, seL4_AllRights);
        ZF_LOGE_IFERR(error, "Failed to copy ASID pool cap to PD");
    }
    cspace_next_free++;
#endif
    return 0;

error:
    /* try to clean up */
    if (pd->proc.fault_endpoint.cptr != 0)
    {
        vka_free_object(vka, &pd->proc.fault_endpoint);
    }

    if (pd->proc.cspace.cptr != 0)
    {
        vka_free_object(vka, &pd->proc.cspace);
    }

    if (pd->proc.pd.cptr != 0)
    {
        vka_free_object(vka, &pd->proc.pd);
        if (pd->proc.vspace.data != 0)
        {
            ZF_LOGE("Could not clean up vspace\n");
        }
    }

    return -1;
}

int pd_configure(pd_t *pd,
                 const char *image_path,
                 ads_t *target_ads,
                 cpu_t *target_cpu)
{
    int error = 0;
    pd->image_name = (char *)image_path;
    memcpy(&pd->proc.pd, target_ads->root_page_dir, sizeof(vka_object_t));
    pd->proc.thread = target_cpu->thread;

    /* The RT manages this ADS */
    // (XXX) Arya: is this necessary?
    pd_add_resource_by_id(get_gpi_server()->rt_pd_id, GPICAP_TYPE_ADS, target_ads->id, NSID_DEFAULT, seL4_CapNull, seL4_CapNull, seL4_CapNull);

    /* Initialize a vka for the PD's cspace */
    error = pd_bootstrap_allocator(pd, pd->proc.cspace.cptr, pd->proc.cspace_next_free,
                                   BIT(CSPACE_SIZE_BITS), CSPACE_SIZE_BITS, 0);
    assert(error == 0);

    // the ADS cap is both a resource manager and a resource
    seL4_Word badge = gpi_new_badge(GPICAP_TYPE_ADS, 0x00, pd->id, target_ads->id, target_ads->id);
    error = pd_send_cap(pd, get_ads_component()->server_ep, badge, &pd->init_data->ads_cap, true);
    ZF_LOGF_IFERR(error, "Failed to send ADS resource cap to PD");

    // the ADS cap also acts an as RDE, however since its object ID is set, a PD can never
    // make a new ADS from this EP
    rde_type_t ads_rde_type = {.type = GPICAP_TYPE_ADS};
    error = pd_add_rde(pd, ads_rde_type, get_gpi_server()->ads_manager_id, target_ads->id, get_ads_component()->server_ep);
    ZF_LOGE_IFERR(error, "Failed to add ADS RDE to PD");
    pd->init_data->binded_ads_ns_id = target_ads->id;
    target_cpu->binded_ads_id = target_ads->id;

    badge = gpi_new_badge(GPICAP_TYPE_CPU, 0x00, pd->id, NSID_DEFAULT, target_cpu->id);
    error = pd_send_cap(pd, get_cpu_component()->server_ep, badge, &pd->init_data->cpu_cap, true);
    ZF_LOGF_IFERR(error, "Failed to send CPU cap to PD");

    memcpy(&pd->proc.vspace, target_ads->vspace, sizeof(vspace_t));

    // Send the PD's PD resource
    badge = gpi_new_badge(GPICAP_TYPE_PD, 0x00, pd->id, NSID_DEFAULT, pd->id);
    error = pd_send_cap(pd, pd->pd_cap_in_RT, badge, &pd->init_data->pd_cap, true);
    ZF_LOGF_IFERR(error, "Failed to send PD cap to PD");

    // Map init data to the PD
    error = ads_component_attach(pd->init_data->binded_ads_ns_id, pd->init_data_mo_id, SEL4UTILS_RES_TYPE_OTHER, NULL, (void **)&pd->init_data_in_PD);
    if (error)
    {
        ZF_LOGF("Failed to attach init data to child PD");
    }
    OSDB_PRINTF("Mapped PD's init data at %p\n", (void *)pd->init_data_in_PD);

    return 0;
}

int pd_send_cap(pd_t *to_pd,
                seL4_CPtr cap,
                seL4_Word badge,
                seL4_Word *slot,
                bool inc_refcount)
{
    /*
        (XXX): Need to handle how sending OSM caps would leand to additional data tracking.
    */

    if (cap == 0) {
        OSDB_PRINTERR("pd_send_cap got a null cap to send\n");
        return 1;
    }

    OSDB_PRINTF("pd_send_cap: Sending cap %ld(badge:%lx) to pd %p\n", cap, badge, to_pd);

    seL4_Word new_badge;
    int error = 0;
    cspacepath_t src, dest;
    seL4_CPtr dest_cptr;
    pd_hold_node_t *res;
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
            server_src_cap = get_ads_component()->server_ep;

            // Copying the resource, so increase the reference count
            if (inc_refcount)
            {
                resource_component_inc(get_ads_component(), get_object_id_from_badge(badge));
            }

            res_id = get_object_id_from_badge(badge);
            break;
        case GPICAP_TYPE_MO:
            server_vka = get_mo_component()->server_vka;
            server_src_cap = get_mo_component()->server_ep;
            // Increment the counter in the mo_t object.
            mo_component_registry_entry_t *mo_reg = mo_component_registry_get_entry_by_badge(badge);
            assert(mo_reg != NULL);

            // Copying the resource, so increase the reference count
            if (inc_refcount)
            {
                resource_server_registry_inc(&get_mo_component()->registry, (resource_server_registry_node_t *)mo_reg);
            }

            res_id = mo_reg->mo.id;
            break;
        case GPICAP_TYPE_CPU:
            server_vka = get_cpu_component()->server_vka;
            server_src_cap = get_cpu_component()->server_ep;

            cpu_component_registry_entry_t *cpu_reg = (cpu_component_registry_entry_t *)
                resource_component_registry_get_by_badge(get_cpu_component(), badge);
            assert(cpu_reg != NULL);

            // Copying the resource, so increase the reference count
            if (inc_refcount)
            {
                resource_component_inc(get_cpu_component(), get_object_id_from_badge(badge));
            }

            res_id = get_object_id_from_badge(badge);
            break;
        case GPICAP_TYPE_PD:
            server_vka = get_pd_component()->server_vka;
            server_src_cap = get_pd_component()->server_ep;

            pd_component_registry_entry_t *pd_reg = pd_component_registry_get_entry_by_badge(badge);
            assert(pd_reg != NULL);

            // Copying the resource, so increase the reference count
            if (inc_refcount)
            {
                resource_server_registry_inc(&get_pd_component()->registry, (resource_server_registry_node_t *)pd_reg);
            }

            res_id = pd_reg->pd.id;
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
                                      to_pd->id, /* Client ID */
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
                OSDB_PRINTF("%s: Failed to mint new_badge %lx.\n",
                            __FUNCTION__, new_badge);
                return 1;
            }

            cap = dest_cptr;
            pd_add_resource(to_pd, cap_type, res_id, NSID_DEFAULT, src.capPtr, cap, src.capPtr);
        }
        // do the same copy as above
    }
    else
    {
        // This is a cap from the kernel.
        // Just copy it to the child.
    }

    error = copy_cap_to_pd(to_pd, cap, slot);
    if (error != 0)
    {
        ZF_LOGF("Failed to copy cap to process");
        return -1;
    }
    OSDB_PRINTF("pd_send_cap: copied cap at %ld to child\n", *slot);

    /* Add to our caps data struct */

    return 0;
}

static int request_remote_rr(pd_t *pd, model_state_t *ms, uint64_t server_id, uint32_t obj_id,
                             cspacepath_t rr_frame_path, void *rr_local_vaddr)
{
    int error;

    pd_component_resource_manager_entry_t *server_entry = pd_component_resource_manager_get_entry_by_id(server_id);

    if (server_entry == NULL)
    {
        OSDB_PRINTF("Failed to find resource server with ID 0x%lx\n", server_id);
        return -1;
    }
    seL4_CPtr server_cap = server_entry->server_ep;
    model_state_t *ms2;

    OSDB_PRINTF("Resource ID 0x%x, server ID 0x%lx, server EP at %d\n", obj_id, server_id, (int)server_cap);

    // Pre-map the memory so resource server does not need to call root task
    cspacepath_t rr_frame_copy_path;
    error = vka_cspace_alloc_path(get_gpi_server()->server_vka, &rr_frame_copy_path);
    if (error != seL4_NoError)
    {
        OSDB_PRINTF("Failed to allocate path for RR frame copy %d", error);
        return -1;
    }

    error = vka_cnode_copy(&rr_frame_copy_path, &rr_frame_path, seL4_AllRights);
    if (error != seL4_NoError)
    {
        OSDB_PRINTF("Failed to copy RR frame cap cap, error: %d", error);
        return -1;
    }

    void *rr_remote_vaddr = vspace_map_pages(&server_entry->pd->proc.vspace, &rr_frame_copy_path.capPtr, NULL,
                                             seL4_AllRights, 1, seL4_LargePageBits, 1);
    if (rr_remote_vaddr == NULL)
    {
        OSDB_PRINTF("Failed to map RR frame to resource server, %d", error);
        return -1;
    }

    // Get RR from remote resource server
    error = resource_server_client_get_rr(server_cap, obj_id, pd->id,
                                          server_entry->pd->id,
                                          rr_remote_vaddr, rr_local_vaddr,
                                          SIZE_BITS_TO_BYTES(seL4_LargePageBits), &ms2);

    if (error == RS_ERROR_DNE)
    {
        // The resource was deleted and the PD component didn't know
        // (XXX) Arya: Eventually, the PD component should be told
        // For now, just omit deleted resources from the model state
        error = 0;
    }
    if (error == RS_ERROR_RR_SIZE)
    {
        // (XXX) Arya: Need to allocate a bigger shared memory if this fails
        printf("RR needs larger memory\n");
        error = -1;
    }
    else if (error != seL4_NoError)
    {
        error = -1;
    }

    combine_model_states(ms, ms2);

    // Remove remotely-mapped memory
    vspace_unmap_pages(&server_entry->pd->proc.vspace, rr_remote_vaddr, 1, seL4_LargePageBits, NULL);
    vka_cnode_delete(&rr_frame_copy_path);
}

// Add rows to model state for one resource
static int res_dump(pd_t *pd, model_state_t *ms, pd_hold_node_t *current_cap,
                    gpi_model_node_t *pd_node, cspacepath_t rr_frame_path, void *rr_local_vaddr)
{
    int error;

    switch (current_cap->type)
    {
    case GPICAP_TYPE_NONE:
        break;
    case GPICAP_TYPE_ADS:
        ads_component_registry_entry_t *ads_data = (ads_component_registry_entry_t *)
            resource_component_registry_get_by_id(get_ads_component(), current_cap->res_id);
        assert(ads_data != NULL);

        ads_dump_rr(&ads_data->ads, ms, pd_node);
        break;
    case GPICAP_TYPE_MO:
        mo_component_registry_entry_t *mo_data = mo_component_registry_get_entry_by_id(current_cap->res_id);
        assert(mo_data != NULL);
        mo_dump_rr(&mo_data->mo, ms, pd_node);
        break;
    case GPICAP_TYPE_CPU:
        cpu_component_registry_entry_t *cpu_data = cpu_component_registry_get_entry_by_id(current_cap->res_id);
        assert(cpu_data != NULL);
        cpu_dump_rr(&cpu_data->cpu, ms, pd_node);
        break;
    case GPICAP_TYPE_seL4:
        // Use some other method to get the cap details
        break;
    case GPICAP_TYPE_BLOCK:
        OSDB_PRINTF("Calling another PD to get the info for resource with ID 0x%x\n", current_cap->res_id);

        // Find the server that created this resource based on the resource id
        uint64_t obj_id = current_cap->res_id;
        uint64_t server_id = get_server_id_from_badge(obj_id);

        error = request_remote_rr(pd, ms, server_id, obj_id, rr_frame_path, rr_local_vaddr);
        assert(error == 0);

        break;
    case GPICAP_TYPE_PD:
        if (current_cap->res_id != pd->id)
        {
            pd_component_registry_entry_t *pd_data = pd_component_registry_get_entry_by_id(current_cap->res_id);
            assert(pd_data != NULL);
            // pd_dump(&pd_data->pd);
            pd_dump_internal(&pd_data->pd, ms, rr_frame_path, rr_local_vaddr);
        }
        break;
    case GPICAP_TYPE_FILE:
        // (XXX) Arya: dump files by NS instead
        break;
    default:
        ZF_LOGE("Invalid holding cap type 0x%x", current_cap->type);
        break;
    }

    return 0;
}

/**
 * Dump the given PD to the given model state
 */
static int pd_dump_internal(pd_t *pd, model_state_t *ms, cspacepath_t rr_frame_path, void *rr_local_vaddr)
{
    int error;

    /* Add the PD node */
    gpi_model_node_t *pd_node = add_pd_node(ms, pd->image_name, pd->id);

    /* Add request edges for all RDEs from this PD */
    for (int i = 0; i < GPICAP_TYPE_MAX; i++)
    {
        for (int j = 0; j < MAX_NS_PER_RDE; j++)
        {
            osmosis_rde_t rde = pd->init_data->rde[i][j];

            if (rde.type.type != GPICAP_TYPE_NONE)
            {
                pd_component_resource_manager_entry_t *rm = pd_component_resource_manager_get_entry_by_id(rde.manager_id);

                if (rm == NULL)
                {
                    ZF_LOGF("Couldn't find resource manager with ID %d\n", rde.manager_id);
                }

/* (XXX) Arya: Not capturing NS data temporarily, will be captured again with resource spaces */
#if 0
                if (rde.ns_id != NSID_DEFAULT)
                {
                    snprintf(ns_id, CSV_MAX_STRING_SIZE, "NS%d", rde.ns_id);
                }
                else
                {
                    snprintf(ns_id, CSV_MAX_STRING_SIZE, "GLOBAL");
                }
#endif

                /* Add the resource server PD node */
                int server_pd_id = rm->pd ? rm->pd->id : 0;
                gpi_model_node_t *resource_manager_pd = add_pd_node(ms, NULL, server_pd_id);
                add_request_edge(ms, pd_node, resource_manager_pd, rde.type.type);

                /**
                 *  For files, walk the file system now
                 *  (XXX) Arya: should there be a general pre-fetch stage for all resource managers?
                 **/
                if (rde.type.type == GPICAP_TYPE_FILE)
                {
                    // (XXX) Arya: Workaround to get all files in the file system
                    // Find the server that created this resource based on the resource id
                    uint64_t obj_id = rde.ns_id;
                    uint64_t server_id = rde.manager_id;

                    error = request_remote_rr(pd, ms, server_id, obj_id, rr_frame_path, rr_local_vaddr);
                    assert(error == 0);
                }
            }
        }
    }

    /* add caps that this PD has access to */
    for (pd_hold_node_t *current_cap = (pd_hold_node_t *)pd->hold_registry.head; current_cap != NULL; current_cap = (pd_hold_node_t *)current_cap->gen.hh.next)
    {
        // print_pd_osm_cap_info(current_cap);
        if (res_dump(pd, ms, current_cap, pd_node, rr_frame_path, rr_local_vaddr) != 0)
        {
            return 1;
        }
    }

    return error;
}

int pd_dump(pd_t *pd)
{
    int error;

    OSDB_PRINTF("pd_dump_cap: Dumping all details of PD:%u\n", pd->id);

    /*
        For all caps that belong to this PD
            switch {
                case: seL4:
                    Print Debug Info
                case: OSmosis:
                    Get the RR for that cap
            }
    */

    /* Initialize the model state */
    model_state_t *ms = (model_state_t *)malloc(sizeof(model_state_t));
    init_model_state(ms, NULL, 0);

    /* Allocate memory for remote rr requests */
    // (XXX) Arya: not able to use MO for remote rr request, since it would require calls back to pd component
    vka_object_t rr_frame_obj;
    error = vka_alloc_frame(get_gpi_server()->server_vka, seL4_LargePageBits, &rr_frame_obj);
    if (error != seL4_NoError)
    {
        return error;
    }

    cspacepath_t rr_frame_path;
    vka_cspace_make_path(get_gpi_server()->server_vka, rr_frame_obj.cptr, &rr_frame_path);

    void *rr_local_vaddr = vspace_map_pages(get_pd_component()->server_vspace, &rr_frame_obj.cptr, NULL,
                                            seL4_AllRights, 1, seL4_LargePageBits, 1);
    if (rr_local_vaddr == NULL)
    {
        return 1;
    }

    /* Add a special node for the RT */
    gpi_model_node_t *rt_node = get_root_node(ms);

    /* Add caps from RT (not all caps, just specially tracked ones) */
    pd_component_registry_entry_t *rt_entry = pd_component_registry_get_entry_by_id(get_gpi_server()->rt_pd_id);
    assert(rt_entry != NULL);
    pd_t *rt_pd = &rt_entry->pd;

    for (pd_hold_node_t *current_cap = (pd_hold_node_t *)rt_pd->hold_registry.head; current_cap != NULL; current_cap = (pd_hold_node_t *)current_cap->gen.hh.next)
    {
        // print_pd_osm_cap_info(current_cap);
        if (res_dump(rt_pd, ms, current_cap, rt_node, rr_frame_path, rr_local_vaddr) != 0)
        {
            return 1;
        }
    }

    /* Add the PD's data */
    error = pd_dump_internal(pd, ms, rr_frame_path, rr_local_vaddr);
    if (error != 0)
    {
        return error;
    }

    /* Free the frame used for rr requests */
    vspace_unmap_pages(get_pd_component()->server_vspace, rr_local_vaddr, 1, seL4_LargePageBits, get_pd_component()->server_vka);

    print_model_state(ms);
    destroy_model_state(ms);

    /* Print RDE Info*/
    for (int i = 0; i < GPICAP_TYPE_MAX; i++)
    {
        for (int j = 0; j < MAX_NS_PER_RDE; j++)
        {
            // print_pd_osm_rde_info(&pd->init_data->rde[i][j]);
        }
    }

    return 0;
}

inline void print_pd_osm_cap_info(pd_hold_node_t *o)
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
    if (o)
    {
        printf("RDE: PD_ID: %u\t Slot_RT:%lu\t Slot_PD: %lu\t T: %s\n",
               o->id,
               o->slot_in_RT,
               o->slot_in_PD,
               cap_type_to_str(o->type.type));
    }
}

// WIP thread resource sharing
// int pd_clone(pd_t *src, pd_t *dest)
// {
//     for (osmosis_pd_cap_t *current_cap = src->has_access_to; current_cap != NULL; current_cap = current_cap->hh.next)
//     {
//         // copy resources from src to dest pd
//     }
// }
