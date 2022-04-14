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

#include <sel4gpi/ads_obj.h>

int ads_attach(ads_t *ads, vka_t *vka, void* vaddr, size_t size){
    return 0;
}

int ads_rm(ads_t *ads, vka_t *vka, void* vaddr, size_t size) {
    return 0;
}

int ads_bind(ads_t *ads, vka_t *vka, seL4_CPtr* cpu_cap) {
    return 0;
}
int ads_clone(vspace_t *loader, ads_t *ads, vka_t *vka, void* omit_vaddr, ads_t *ret_ads) {

    ret_ads->vspace = malloc(sizeof(vspace_t));
    vspace_t *from = ads->vspace;
    vspace_t *to = ret_ads->vspace;

    printf("Cloning vspace\n");
    printf("Old vspace details:\n");
    sel4utils_walk_vspace(from, NULL);


    // Give vspace root
    // assign asid pool
    static vka_object_t vspace_root_object;
    static sel4utils_alloc_data_t alloc_data; // remove from stack.

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


    // It is stuck here...
    error = sel4utils_get_vspace(
         loader,
         to,
         &alloc_data,
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

    printf("===========Start of interesting output================\n");

    /* walk all the reservations */
    int num_pages;
    while (from_sel4_res != NULL)
    {
        printf("Reservation: %p\n", from_sel4_res->start);
        // Reserver
        reservation_t new_res = sel4utils_reserve_range_at(to,
                                                           (void *)from_sel4_res->start,
                                                           from_sel4_res->end - from_sel4_res->start,
                                                           from_sel4_res->rights, from_sel4_res->cacheable);

        num_pages = (from_sel4_res->end - from_sel4_res->start) / PAGE_SIZE_4K;
        if (from_sel4_res->start == (uintptr_t)omit_vaddr)
        {
            printf("Skipping the region, with start vaddr of %p\n", omit_vaddr);
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
    printf("New vspace details:\n");
    sel4utils_walk_vspace(to, NULL);
    // For each reservation: call share_mem_at_vaddr
    return 0;

error_exit:
    return -1;
}