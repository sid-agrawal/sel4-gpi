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

#include <sel4gpi/ads_obj.h>
#include <sel4gpi/ads_component.h>
#include <sel4gpi/debug.h>

int ads_attach(ads_t *ads, vka_t *vka, void* vaddr, size_t size, seL4_CPtr frame_cap,
               /*sel4utils_process_t*/ vspace_t *process_cookie)
{
    vspace_t *target = ads->vspace;


    /* Reserver the range in the vspace */
    seL4_CapRights_t rights;
    reservation_t res = sel4utils_reserve_range_at(target, vaddr, size, rights, 1);
    if (res.res == NULL)
    {
        ZF_LOGE("Failed to reserve range\n");
        return 1;
    }

    /* Map the frame cap into the vspace */

    seL4_CPtr caps[] = {frame_cap};
    size_t num_pages = size / PAGE_SIZE_4K;
    size_t size_bits = seL4_PageBits;
    int error = sel4utils_map_pages_at_vaddr(target,
    caps,
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

    return 0;
}

int ads_rm(ads_t *ads, vka_t *vka, void* vaddr, size_t size) {
    return 0;
}

int ads_bind(ads_t *ads, vka_t *vka, seL4_CPtr* cpu_cap) {
    return 0;
}

void ads_dump_rr(ads_t *ads) {

    vspace_t *from = ads->vspace;
    assert(from != NULL);
    OSDB_PRINTF(ADSSERVS "vspace: %p\n", from);
    assert(0);

    OSDB_PRINTF(ADSSERVS "===========Start of interesting output================\n");
    sel4utils_alloc_data_t *from_data = get_alloc_data(from);
    assert(from_data != NULL);
    OSDB_PRINTF(ADSSERVS "From_data: %p\n", from_data);
    sel4utils_res_t *from_sel4_res = from_data->reservation_head;
    assert(from_sel4_res != NULL);

    while (from_sel4_res != NULL)
    {
        OSDB_PRINTF(ADSSERVS "Reservation: %p\n", from_sel4_res->start);
        void *va = (void *)from_sel4_res->start;
        OSDB_PRINTF(ADSSERVS  "VA: %p\n", va);

        from_sel4_res = from_sel4_res->next;
    }

    OSDB_PRINTF(ADSSERVS "===========END of interesting output================\n");
}

int ads_clone(vspace_t *loader, ads_t *ads, vka_t *vka, void* omit_vaddr, ads_t *ret_ads) {

    ret_ads->vspace = malloc(sizeof(vspace_t));
    if (ret_ads->vspace == NULL) {
        ZF_LOGE("Failed to allocate vspace\n");
        goto error_exit;
    }
    vspace_t *from = ads->vspace;
    assert(from != NULL);
    vspace_t *to = ret_ads->vspace;

    // OSDB_PRINTF("Cloning vspace\n");
    // printf("Old vspace details:\n");
    // sel4utils_walk_vspace(from, NULL);


    // Give vspace root
    // assign asid pool
    static vka_object_t vspace_root_object;
    sel4utils_alloc_data_t *alloc_data = malloc(sizeof(sel4utils_alloc_data_t));
    if (alloc_data == NULL) {
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
         NULL, //sel4utils_allocated_object,
         NULL // (void *) process
     );
    if (error)
    {
        ZF_LOGE("Failed to get new vspace while making copy: %d\n in %s", error, __FUNCTION__);
        goto error_exit;
    }



    sel4utils_alloc_data_t *from_data = get_alloc_data(from);
    sel4utils_res_t *from_sel4_res = from_data->reservation_head;

    OSDB_PRINTF("===========Start of interesting output================\n");

    /* walk all the reservations */
    int num_pages;
    while (from_sel4_res != NULL)
    {
        OSDB_PRINTF("Reservation: %p\n", from_sel4_res->start);
        // Reserver
        reservation_t new_res = sel4utils_reserve_range_at(to,
                                                           (void *)from_sel4_res->start,
                                                           from_sel4_res->end - from_sel4_res->start,
                                                           from_sel4_res->rights, from_sel4_res->cacheable);

        num_pages = (from_sel4_res->end - from_sel4_res->start) / PAGE_SIZE_4K;
        if (from_sel4_res->start == (uintptr_t)omit_vaddr)
        {
            OSDB_PRINTF("Skipping the region, with start vaddr of %p\n", omit_vaddr);
            from_sel4_res = from_sel4_res->next;
            continue;
        }
        // map
        error = sel4utils_share_mem_at_vaddr(from, to,
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
        // Move to next node.
        from_sel4_res = from_sel4_res->next;
    }
    OSDB_PRINTF("New vspace details:\n");
    // sel4utils_walk_vspace(to, NULL);
    // For each reservation: call share_mem_at_vaddr
    return 0;

error_exit:
    free(alloc_data);
    free(ret_ads->vspace);
    return -1;
}