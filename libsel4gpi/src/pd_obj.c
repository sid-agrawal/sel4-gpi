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
#include <sel4gpi/pd_utils.h>
// #include <sel4gpi/gpi_rde.h>

#include <vka/capops.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <simple/simple_helpers.h>
#include <utils/uthash.h>
#include <cpio/cpio.h>

#define CSPACE_SIZE_BITS 17
#define ELF_LIB_DATA_SECTION ".lib_data"
#define ELF_APP_DATA_SECTION ".data"

/* This is doesn't belong here but we need it */
extern char _cpio_archive[];
extern char _cpio_archive_end[];
static int pd_setup_cspace(pd_t *pd, vka_t *vka);

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
        OSDB_PRINTF(PD_DEBUG, "No more RDE NS slots available for type %d\n", type.type);
        return 1;
    }

    pd->init_data->rde[type.type][idx].manager_id = manager_id;
    /* we don't really need to keep this if we index by type, but let's just keep it around for now */
    pd->init_data->rde[type.type][idx].type = type;
    pd->init_data->rde[type.type][idx].slot_in_RT = server_ep;
    pd->init_data->rde[type.type][idx].ns_id = ns_id;
    uint32_t client_id = pd->pd_obj_id;

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

    OSDB_PRINTF(PD_DEBUG, "Added new RDE of type %d to PD %d, in slot %d, with badge %lx\n", type.type, client_id, (int)dest.capPtr, badge_val);

    pd->init_data->rde_count++;
    return 0;
}

int pd_new(pd_t *pd,
           vka_t *server_vka,
           vspace_t *server_vspace)
{
    int error;

    OSDB_PRINTF(PD_DEBUG, PDSERVS "new PD: \n");

    pd->has_access_to_count = 0;
    pd->has_access_to = NULL; // required for uthash initialization
    pd->pd_started = false;

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

    // Track the init data MO in RT only
    pd_add_resource(&get_pd_component()->rt_pd, GPICAP_TYPE_MO, rde_mo_obj->mo_obj_id, pd->init_data_mo.badged_server_ep_cspath.capPtr, 0, 0);
    // pd_add_resource(pd, GPICAP_TYPE_MO, rde_mo_obj->mo_obj_id, pd->init_data_mo.badged_server_ep_cspath.capPtr, seL4_CapNull, pd->init_data_mo.badged_server_ep_cspath.capPtr);

    // Setup init data
    pd->init_data->rde_count = 0;
    memset(pd->init_data->rde, 0, sizeof(osmosis_rde_t) * MAX_PD_OSM_RDE);

    error = pd_setup_cspace(pd, get_pd_component()->server_vka);
    assert(error == 0);

    return 0;
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
        OSDB_PRINTF(PD_DEBUG, PDSERVS "%s: Failed to initialize single-level cspace for PD id %d.\n",
                    __FUNCTION__, pd->pd_obj_id);
        return -1;
    }

    error = allocman_attach_cspace(allocator, cspace_single_level_make_interface(cspace));
    if (error != seL4_NoError)
    {
        OSDB_PRINTF(PD_DEBUG, PDSERVS "%s: Failed to attach cspace to allocman for PD id %d.\n",
                    __FUNCTION__, pd->pd_obj_id);
        return -1;
    }

    allocman_make_vka(&pd->pd_vka, allocator);
    return 0;
}

static int pd_setup_proc(pd_t *pd, vka_t *server_vka, vspace_t *server_vspace, ads_t *target_ads, const char *image_name, uint64_t heap_size)
{
    int error;

    seL4_CPtr slot;
    // copy_cap_to_pd(pd, pd->proc.thread.tcb.cptr, &slot);

    unsigned long size;
    unsigned long cpio_len = _cpio_archive_end - _cpio_archive;
    char const *file = cpio_get_file(_cpio_archive, cpio_len, image_name, &size);
    elf_t elf;
    elf_newFile(file, size, &elf);

    // (XXX) Linh: Do we need to add the ELF regions to the PD as resources?
    pd->proc.entry_point = sel4utils_elf_load(target_ads->vspace, server_vspace, server_vka, server_vka, &elf);
    if (pd->proc.entry_point == NULL)
    {
        ZF_LOGE("Failed to load elf file\n");
        goto error;
    }

    pd->proc.sysinfo = sel4utils_elf_get_vsyscall(&elf);

    /* Retrieve the ELF phdrs */
    pd->proc.num_elf_phdrs = sel4utils_elf_num_phdrs(&elf);
    pd->proc.elf_phdrs = calloc(pd->proc.num_elf_phdrs, sizeof(Elf_Phdr));
    if (!pd->proc.elf_phdrs)
    {
        ZF_LOGE("Failed to allocate memory for elf phdr information");
        goto error;
    }
    sel4utils_elf_read_phdrs(&elf, pd->proc.num_elf_phdrs, pd->proc.elf_phdrs);
    
    /**
     * By default, all ELF sections will be shared during ads_shallow_copy
     * If we want to separate data sections, will need to identify them here and check for 
     * their vaddr when copying
    */
    //uint64_t section_size;
    //uintptr_t lib_data_section = sel4utils_elf_get_section(&elf, ELF_LIB_DATA_SECTION, &section_size);

    /* select the default page size of machine this process is running on */
    pd->proc.pagesz = PAGE_SIZE_4K;

    /* set up IPC buffer */
    pd->proc.thread.ipc_buffer_addr = (seL4_Word)vspace_new_ipc_buffer(target_ads->vspace, &pd->proc.thread.ipc_buffer);
    if (pd->proc.thread.ipc_buffer_addr == 0)
    {
        ZF_LOGE("Failed to allocate PD's IPC buffer");
        goto error;
    }

    /* set up stack */
    pd->proc.thread.stack_size = BYTES_TO_4K_PAGES(CONFIG_SEL4UTILS_STACK_SIZE);
    pd->proc.thread.stack_top = vspace_new_sized_stack(target_ads->vspace, pd->proc.thread.stack_size);
    if (pd->proc.thread.stack_top == NULL)
    {
        ZF_LOGE("Failed to allocate PD's stack");
        goto error;
    }

    /* set up heap */
    // (XXX) Arya: Use predefined location, and predefined size per image
    // Workaround so we can still use the static malloc
    if (heap_size > 0)
    {
        int n_pages = DIV_ROUND_UP(heap_size, BIT(seL4_PageBits));

        reservation_t heap_res = vspace_reserve_range_at(target_ads->vspace, (void *)PD_HEAP_LOC, heap_size, seL4_AllRights, 0);
        sel4utils_res_t *sel4utils_res = reservation_to_res(heap_res);
        sel4utils_res->type = SEL4UTILS_RES_TYPE_HEAP;

        error = vspace_new_pages_at_vaddr(target_ads->vspace, (void *)PD_HEAP_LOC, n_pages, seL4_PageBits, heap_res);
        ZF_LOGF_IF(error, "Failed to allocate PD's heap");
    }

    /* Forge MOs for these regions */
    int max_mo_caps = 10;
    int n_mo_caps;
    seL4_CPtr *mo_caps = calloc(max_mo_caps, sizeof(seL4_CPtr));
    uint64_t *mo_cap_ids = calloc(max_mo_caps, sizeof(uint64_t));
    error = forge_mo_caps_from_vspace(target_ads->vspace, target_ads, server_vka, 0, &n_mo_caps, mo_caps, mo_cap_ids);
    ZF_LOGE_IF(error, "Failed to forge MO caps for PD's vspace");

    /* Add MOs to root task since nobody else will have them */
    for (int i = 0; i < n_mo_caps; i++)
    {
        pd_add_resource(&get_pd_component()->rt_pd, GPICAP_TYPE_MO, mo_cap_ids[i], mo_caps[i], 0, 0);
    }
    free(mo_caps);
    free(mo_cap_ids);

    return 0;

error:
    if (pd->proc.elf_regions)
    {
        free(pd->proc.elf_regions);
    }

    if (pd->proc.elf_phdrs)
    {
        free(pd->proc.elf_phdrs);
    }

    if (pd->proc.thread.ipc_buffer_addr)
    {
        vspace_free_ipc_buffer(target_ads->vspace, (void *)pd->proc.thread.ipc_buffer_addr);
    }

    if (pd->proc.thread.stack_top)
    {
        vspace_free_sized_stack(target_ads->vspace, pd->proc.thread.stack_top, pd->proc.thread.stack_size);
    }

    return -1;
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

int pd_load_image(pd_t *pd,
                  vka_t *vka,
                  simple_t *simple,
                  const char *image_path,
                  vspace_t *server_vspace,
                  ads_t *target_ads,
                  cpu_t *target_cpu,
                  uint64_t heap_size)
{
    int error = 0;
    pd->image_name = (char *)image_path;
    OSDB_PRINTF(PD_DEBUG, PDSERVS "load_image: loading image %s for pd %p\n", image_path, pd);
    memcpy(&pd->proc.pd, target_ads->root_page_dir, sizeof(vka_object_t));

    // (XXX) Linh: we may not always setup the PD as a proc
    error = pd_setup_proc(pd, vka, server_vspace, target_ads, image_path, heap_size);
    assert(error == 0);

    error = cpu_config_vspace(target_cpu, vka, target_ads->vspace,
                              pd->proc.cspace.cptr,
                              pd->cnode_guard,
                              pd->proc.fault_endpoint.cptr,
                              pd->proc.thread.ipc_buffer,
                              pd->proc.thread.ipc_buffer_addr,
                              pd->proc.thread.stack_top);

    pd->proc.thread.tcb = *(target_cpu->tcb);

    /* Initialize a vka for the PD's cspace */
    error = pd_bootstrap_allocator(pd, pd->proc.cspace.cptr, pd->proc.cspace_next_free,
                                   BIT(CSPACE_SIZE_BITS), CSPACE_SIZE_BITS, 0);
    assert(error == 0);

    // the ADS cap is both a resource manager and a resource
    pd->ads_obj_id = target_ads->ads_obj_id;
    seL4_Word badge = gpi_new_badge(GPICAP_TYPE_ADS, 0x00, pd->pd_obj_id, target_ads->ads_obj_id, target_ads->ads_obj_id);
    error = pd_send_cap(pd, get_ads_component()->server_ep_obj.cptr, badge, &pd->init_data->ads_cap);
    ZF_LOGF_IFERR(error, "Failed to send ADS resource cap to PD");

    // the ADS cap also acts an as RDE, however since its object ID is set, a PD can never
    // make a new ADS from this EP
    rde_type_t ads_rde_type = {.type = GPICAP_TYPE_ADS};
    error = pd_add_rde(pd, ads_rde_type, get_gpi_server()->ads_manager_id, target_ads->ads_obj_id, get_ads_component()->server_ep_obj.cptr);
    ZF_LOGE_IFERR(error, "Failed to add ADS RDE to PD");
    pd->init_data->binded_ads_ns_id = target_ads->ads_obj_id;
    target_cpu->binded_ads_id = target_ads->ads_obj_id;

    // Send CPU cap as a resource
    badge = gpi_new_badge(GPICAP_TYPE_CPU, 0x00, pd->pd_obj_id, NSID_DEFAULT, target_cpu->cpu_obj_id);
    pd_send_cap(pd, get_cpu_component()->server_ep_obj.cptr, badge, &pd->init_data->cpu_cap);
    ZF_LOGF_IFERR(error, "Failed to send CPU cap to PD");

    memcpy(&pd->proc.vspace, target_ads->vspace, sizeof(vspace_t));

    OSDB_PRINTF(PD_DEBUG, PDSERVS "PD%d free_slot.start %ld\n", pd->pd_obj_id, pd->proc.cspace_next_free);
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
    OSDB_PRINTF(PD_DEBUG, "pd_send_cap: Sending cap %ld(badge:%lx) to pd %p\n", cap, badge, to_pd);

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
            server_vka = get_pd_component()->server_vka;
            server_src_cap = get_pd_component()->server_ep_obj.cptr;

            pd_component_registry_entry_t *pd_reg = pd_component_registry_get_entry_by_badge(badge);
            assert(pd_reg != NULL);

            res_id = pd_reg->pd.pd_obj_id;
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
                OSDB_PRINTF(PD_DEBUG, PDSERVS "%s: Failed to mint new_badge %lx.\n",
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

    error = copy_cap_to_pd(to_pd, cap, slot);
    if (error != 0)
    {
        ZF_LOGF("Failed to copy cap to process");
        return -1;
    }
    OSDB_PRINTF(PD_DEBUG, PDSERVS "pd_send_cap: copied cap at %ld to child\n", *slot);

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

    OSDB_PRINTF(PD_DEBUG, PDSERVS "pd_start: ARGS: pd_endpoint_in_root: %ld, argc: %d\n",
                pd_endpoint_in_root, argc);
    assert(&pd->proc != NULL);
    assert(&pd->proc.vspace != NULL);

    // Send the PD's PD resource
    seL4_Word badge = gpi_new_badge(GPICAP_TYPE_PD, 0x00, pd->pd_obj_id, NSID_DEFAULT, pd->pd_obj_id);
    pd_send_cap(pd, pd_endpoint_in_root, badge, &pd->init_data->pd_cap);

    // Map init data to the PD
    error = ads_component_attach(pd->ads_obj_id, pd->init_data_mo_id, NULL, (void **)&pd->init_data_in_PD);
    if (error)
    {
        ZF_LOGF("Failed to attach init data to child PD");
    }
    OSDB_PRINTF(PD_DEBUG, "Mapped PD's init data at %p\n", (void *)pd->init_data_in_PD);

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

    OSDB_PRINTF(PD_DEBUG, "Starting PD with string args: [", argc);
    for (int i = 0; i < argc; i++)
    {
        OSDB_PRINTF(PD_DEBUG, "%s, ", string_args[i]);
    }
    OSDB_PRINTF(PD_DEBUG, "]\n");

    /* spawn the process */
    OSDB_PRINTF(PD_DEBUG, PDSERVS "pd_start: starting PD\n");
    error = sel4utils_osm_spawn_process_v(&(pd->proc),
                                          (void *)pd->init_data_in_PD,
                                          get_gpi_server()->server_vka,
                                          server_vspace,
                                          argc,
                                          argv,
                                          1);
    ZF_LOGF_IF(error != 0, "Failed to start test process!");

    pd->pd_started = true;

    return 0;
}

// Add rows to model state for one resource
static int res_dump(pd_t *pd, model_state_t *ms, osmosis_pd_cap_t *current_cap,
                    char *pd_id, cspacepath_t rr_frame_path, void *rr_local_vaddr)
{
    char res_id[CSV_MAX_STRING_SIZE];
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
        OSDB_PRINTF(PD_DEBUG, PDSERVS "Calling another PD to get the info for resource with ID 0x%x\n", current_cap->res_id);

        // Find the server that created this resource based on the resource id
        uint64_t obj_id = current_cap->res_id;
        uint64_t server_id = get_server_id_from_badge(obj_id);
        pd_component_resource_manager_entry_t *server_entry = pd_component_resource_manager_get_entry_by_id(server_id);

        if (server_entry == NULL)
        {
            OSDB_PRINTF(PD_DEBUG, PDSERVS "Failed to find resource server with ID 0x%lx\n", server_id);
            return -1;
        }
        seL4_CPtr server_cap = server_entry->server_ep;
        rr_state_t *rs;

        OSDB_PRINTF(PD_DEBUG, PDSERVS "Resource ID 0x%lx, server ID 0x%lx, server EP at %d\n", obj_id, server_id, (int)server_cap);

        // Pre-map the memory so resource server does not need to call root task
        cspacepath_t rr_frame_copy_path;
        int error = vka_cspace_alloc_path(get_gpi_server()->server_vka, &rr_frame_copy_path);
        if (error != seL4_NoError)
        {
            OSDB_PRINTF(PD_DEBUG, PDSERVS "Failed to allocate path for RR frame copy %d", error);
            return -1;
        }

        error = vka_cnode_copy(&rr_frame_copy_path, &rr_frame_path, seL4_AllRights);
        if (error != seL4_NoError)
        {
            OSDB_PRINTF(PD_DEBUG, ADSSERVS "Failed to copy RR frame cap cap, error: %d", error);
            return -1;
        }

        void *rr_remote_vaddr = vspace_map_pages(&server_entry->pd->proc.vspace, &rr_frame_copy_path.capPtr, NULL,
                                                 seL4_AllRights, 1, seL4_PageBits, 1);
        if (rr_remote_vaddr == NULL)
        {
            OSDB_PRINTF(PD_DEBUG, PDSERVS "Failed to map RR frame to resource server, %d", error);
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

            return 0;
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

    return 0;
}

int pd_dump(pd_t *pd)
{
    int error;

    OSDB_PRINTF(PD_DEBUG, PDSERVS "pd_dump_cap: Dumping all details of PD:%u\n", pd->pd_obj_id);

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
    snprintf(pd_name, CSV_MAX_STRING_SIZE, "%s", pd->image_name);
    char pd_id[CSV_MAX_STRING_SIZE];
    make_res_id(pd_id, GPICAP_TYPE_PD, pd->pd_obj_id);
    add_pd(ms, pd_name, pd_id);

    /* Allocate memory for remote rr requests */
    vka_object_t rr_frame_obj;
    error = vka_alloc_frame(get_gpi_server()->server_vka, seL4_PageBits, &rr_frame_obj);
    if (error != seL4_NoError)
    {
        return error;
    }

    cspacepath_t rr_frame_path;
    vka_cspace_make_path(get_gpi_server()->server_vka, rr_frame_obj.cptr, &rr_frame_path);

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

    char ns_id[CSV_MAX_STRING_SIZE];
    char rm_id[CSV_MAX_STRING_SIZE];
    // uint32_t added_pd_rr[MAX_PD_OSM_RDE] = {0};
    // memset(added_pd_rr, -1, sizeof(uint32_t) * MAX_PD_OSM_RDE);

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

                if (rde.ns_id != NSID_DEFAULT)
                {
                    snprintf(ns_id, CSV_MAX_STRING_SIZE, "NS%d", rde.ns_id);
                }
                else
                {
                    snprintf(ns_id, CSV_MAX_STRING_SIZE, "GLOBAL");
                }

                int server_pd_id = rm->pd ? rm->pd->pd_obj_id : 0;
                snprintf(rm_id, CSV_MAX_STRING_SIZE, "PD_%d", server_pd_id);

                add_pd_requests(ms, pd_id, rm_id, rde.type.type, ns_id);
            }
        }
    }

    /* add a special resource for the RT */
    char root_task_id[CSV_MAX_STRING_SIZE];
    make_res_id(root_task_id, GPICAP_TYPE_PD, 0);
    add_resource(ms, "All", "RT_ALL");
    add_has_access_to(ms, root_task_id, "RT_ALL", true);
    add_pd(ms, "ROOT TASK", root_task_id);

    /* add caps from RT (not all caps, just specially tracked ones) */
    for (osmosis_pd_cap_t *current_cap = get_pd_component()->rt_pd.has_access_to; current_cap != NULL; current_cap = current_cap->hh.next)
    {
        // print_pd_osm_cap_info(current_cap);
        if (res_dump(&get_pd_component()->rt_pd, ms, current_cap, root_task_id, rr_frame_path, rr_local_vaddr) != 0)
        {
            return -1;
        }
    }

    /* add caps that this PD has access to */
    for (osmosis_pd_cap_t *current_cap = pd->has_access_to; current_cap != NULL; current_cap = current_cap->hh.next)
    {
        // print_pd_osm_cap_info(current_cap);
        if (res_dump(pd, ms, current_cap, pd_id, rr_frame_path, rr_local_vaddr) != 0)
        {
            return -1;
        }
    }

    /* Free the frame used for rr requests */
    // (XXX) Arya: Again, unmapping this will cause future failures
    // vspace_unmap_pages(get_pd_component()->server_vspace, rr_local_vaddr, 1, seL4_PageBits, pd->vka);

    print_model_state(ms);
    free(ms);
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
    if (o)
    {
        printf("RDE: PD_ID: %u\t Slot_RT:%lu\t Slot_PD: %lu\t T: %s\n",
               o->pd_obj_id,
               o->slot_in_RT,
               o->slot_in_PD,
               cap_type_to_str(o->type.type));
    }
}
