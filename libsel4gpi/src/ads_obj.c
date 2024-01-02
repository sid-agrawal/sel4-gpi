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

void ads_dump_rr(ads_t *ads, void *buff, size_t size)
{

    assert(buff != NULL);

/*===================start=====================*/
    ZF_LOGE("Dumping RR for ads object %d\n", ads->ads_obj_id);
    ZF_LOGE("Buf: [%p, %p) num_pages: %lu\n", buff, buff + size, size / PAGE_SIZE_4K);

    void *buff_start_aligned = (void *) ROUND_DOWN((uintptr_t)buff, PAGE_SIZE_4K);
    void *buff_end_aligned = (void *) ROUND_UP((uintptr_t)(buff + size), PAGE_SIZE_4K);
    uint64_t num_pages = (buff_end_aligned - buff_start_aligned) / PAGE_SIZE_4K;
    ZF_LOGE("Buf _alg: [%p, %p) num_pages: %lu\n", buff_start_aligned, buff_end_aligned, num_pages);




    vspace_t *ads_vspace = ads->vspace;

    /* Get the page frame cap for the client buf VA */
    seL4_CPtr * caps = malloc(sizeof(seL4_CPtr) * num_pages);
    assert (caps != NULL);

    for (int i = 0; i < num_pages; i++)
    {
        seL4_CPtr buf_cap = vspace_get_cap(ads_vspace, buff_start_aligned + i * PAGE_SIZE_4K);
        assert(buf_cap != 0);

        /* Clone the cap */
        /* Fix up this copy of cap*/
        /* create a path to the cap */
        cspacepath_t from_path, to_path;
        vka_cspace_make_path(get_gpi_server()->server_vka, buf_cap, &from_path);

        /* allocate a path to put the copy in the destination */
        int error = vka_cspace_alloc_path(get_gpi_server()->server_vka, &to_path);
        if (error)
        {
            ZF_LOGF("Failed to allocate slot in to cspace, error: %d", error);
        }

        /* copy the frame cap into the to cspace */
        error = vka_cnode_copy(&to_path, &from_path, seL4_AllRights);
        if (error)
        {
            ZF_LOGF("Failed to copy cap, error %d num_pages: %d\n",
                    error, 1);
        }
        caps[i] = to_path.capPtr;
    }

    /* a. Create a reservation in the server vspace*/
    /* Find the page where the buffer lies*/
    vspace_t *server_vspace = get_gpi_server()->server_vspace;




    void *server_buffer_va = sel4utils_map_pages(server_vspace,
                                                 caps,
                                                 NULL,
                                                 seL4_AllRights,
                                                 num_pages,
                                                 seL4_PageBits,
                                                 1);
/*===================end ======================*/

    /*
        Move the buf to the correct offset.
    */
    server_buffer_va += (uintptr_t)buff % PAGE_SIZE_4K;



    //=============================================================================

    /* Dump the info */
    sel4utils_res_t *from_sel4_res = get_alloc_data(ads_vspace)->reservation_head;
    assert(from_sel4_res != NULL);

    vka_t *vka = get_alloc_data(ads_vspace)->vka;

    assert(vka != NULL);
    OSDB_PRINTF(ADSSERVS "vka address: %p\n", vka);

    model_state_t *ms = (model_state_t *)malloc(sizeof(model_state_t));
    assert(ms != NULL);
    init_model_state(ms);

    while (from_sel4_res != NULL)
    {
#if 0
        OSDB_PRINTF(ADSSERVS "Reservation: 0x%lx -- 0x%lx = %s %s\n",
                    from_sel4_res->start, from_sel4_res->end,
                    human_readable_size(from_sel4_res->end - from_sel4_res->start),
                    human_readable_va_res_type(from_sel4_res->type));
#else

        char res_type[CSV_MAX_STRING_SIZE];
        char res_id[CSV_MAX_STRING_SIZE];
        snprintf(res_type, CSV_MAX_STRING_SIZE, "%s", human_readable_va_res_type(from_sel4_res->type));
        snprintf(res_id, CSV_MAX_STRING_SIZE, "%u_%lx_%lx",
                 ads->ads_obj_id, from_sel4_res->start,
                 from_sel4_res->end);
        add_resource(ms, res_type, res_id);

        /* These two do not belong here*/
        add_has_access_to(ms, "PD.1.0", res_id, "true");
        add_pd(ms, "Proc1", "PD.1.0");


#endif

        /* Print all the caps of this reservation */
        void *va = (void *)from_sel4_res->start;
        for (void *start = (void *)from_sel4_res->start;
             start < (void *)from_sel4_res->end;
             start += PAGE_SIZE_4K)
        {
            seL4_CPtr cap = vspace_get_cap(ads_vspace, start);
            if (cap == 0)
            {
                OSDB_PRINTF(ADSSERVS "No cap for %p\n", start);
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
                //    osmosis_cap_t cap_data;
                //    int error = retrive_cap_data(cap, &cap_data);
                //    assert(error == 0);

                //    cap_data.paddr);
                // OSDB_PRINTF(ADSSERVS "\tGetting paddr for cap %ld\n", cap);

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

#if 0
                OSDB_PRINTF(ADSSERVS "Cap for %p: %lu Type: %u Paddr: %p isMinted: %d\n",
                            start,
                            page_frame_cap,
                            seL4_DebugCapIdentify(page_frame_cap),
                            paddr,
                            cap_info.isMinted);
#else

        char page_res_type[CSV_MAX_STRING_SIZE];
        char page_res_id[CSV_MAX_STRING_SIZE];
        snprintf(page_res_type, CSV_MAX_STRING_SIZE, "PhysicalPage");
        snprintf(page_res_id, CSV_MAX_STRING_SIZE, "%p", paddr);
        add_resource(ms, page_res_type, page_res_id);

        add_resource_depends_on(ms, res_id, page_res_id);
#endif
            }
        }

        from_sel4_res = from_sel4_res->next;
    }
    OSDB_PRINTF(ADSSERVS "Done with while loop\n");

    export_model_state(ms, server_buffer_va, size);
    OSDB_PRINTF(ADSSERVS "Done with model expoert loop\n");
    /*(XXX) Unmap the page */
}

int ads_clone(vspace_t *loader, ads_t *ads, vka_t *vka, void* omit_vaddr, ads_t *ret_ads) {

    ret_ads->vspace = malloc(sizeof(vspace_t));
    if (ret_ads->vspace == NULL) {
        ZF_LOGE("Failed to allocate vspace\n");
        goto error_exit;
    }
    ret_ads->process_for_cookies = malloc(sizeof(sel4utils_process_t));
    if (ret_ads->process_for_cookies == NULL) {
        ZF_LOGE("Failed to allocate process struct for coolies in ads_clone\n");
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
         sel4utils_allocated_object,

        /*
            sel4utils_allocated_object expects a process struct as a cookie
            Instead use a different function which suited are needs better.
        */

         &(ret_ads->process_for_cookies)
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
        OSDB_PRINTF("Reservation: %p\n", (void *) from_sel4_res->start);
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


        /*


        */

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