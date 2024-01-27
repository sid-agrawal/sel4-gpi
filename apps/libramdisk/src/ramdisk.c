/**
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vka/capops.h>
#include <vspace/vspace.h>

#include <ramdisk/ramdisk.h>

#define RAMDISK_PRINTF(...)       \
    do                            \
    {                             \
        printf("%s ", RAMDISK_S); \
        printf(__VA_ARGS__);      \
    } while (0);

static ramdisk_server_context_t ramdisk_server;

ramdisk_server_context_t *get_ramdisk_server(void)
{
    return &ramdisk_server;
}

static inline seL4_MessageInfo_t recv(seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(get_ramdisk_server()->server_ep_obj.cptr,
                    sender_badge_ptr,
                    get_ramdisk_server()->server_thread.reply.cptr);
}

static inline void reply(seL4_MessageInfo_t tag)
{
    api_reply(get_ramdisk_server()->server_thread.reply.cptr, tag);
}

seL4_Error
ramdisk_server_spawn_thread(simple_t *parent_simple, vka_t *parent_vka,
                            vspace_t *parent_vspace,
                            uint8_t priority,
                            seL4_CPtr *server_ep_cap)
{
    seL4_Error error;
    cspacepath_t parent_cspace_cspath;
    seL4_MessageInfo_t tag;

    if (parent_simple == NULL || parent_vka == NULL || parent_vspace == NULL)
    {
        return seL4_InvalidArgument;
    }

    get_ramdisk_server()->server_simple = parent_simple;
    get_ramdisk_server()->server_vka = parent_vka;
    get_ramdisk_server()->server_cspace = parent_cspace_cspath.root;
    get_ramdisk_server()->server_vspace = parent_vspace;

    /* Get a CPtr to the parent's root cnode. */
    vka_cspace_make_path(parent_vka, 0, &parent_cspace_cspath);

    /* Allocate the Endpoint that the server will be listening on. */
    error = vka_alloc_endpoint(parent_vka, &get_ramdisk_server()->server_ep_obj);
    if (error != 0)
    {
        ZF_LOGE(RAMDISK_S "spawn_thread: failed to alloc endpoint, err=%d.",
                error);
        return error;
    }

    *server_ep_cap = get_ramdisk_server()->server_ep_obj.cptr;

    /* Allocate the ramdisk's virtual disk */

    /*
    error = vka_alloc_frame(parent_vka, RAMDISK_SIZE_BITS, &get_ramdisk_server()->ramdisk_buf_obj);
    if (error)
    {
        ZF_LOGE(RAMDISK_S "spawn_thread: failed to alloc virtual disk, err=%d.",
                error);
        return error;
    }

    reservation_t ramdisk_reservation = vspace_reserve_range_aligned(
        parent_vspace, RAMDISK_SIZE_BYTES, RAMDISK_SIZE_BITS,
        seL4_ReadWrite, 1,
        &get_ramdisk_server()->ramdisk_buf);

    if (ramdisk_reservation.res == NULL)
    {
        ZF_LOGE(RAMDISK_S "spawn_thread: failed to reserve vspace for virtual disk.");
        return 1;
    }

    error = vspace_map_pages_at_vaddr(parent_vspace,
                              &get_ramdisk_server()->ramdisk_buf_obj.cptr,
                              &get_ramdisk_server()->ramdisk_buf_obj.ut,
                              get_ramdisk_server()->ramdisk_buf,
                              1, seL4_LargePageBits, ramdisk_reservation);
    */

    int n_pages = RAMDISK_SIZE_BYTES / SIZE_BITS_TO_BYTES(seL4_LargePageBits);
    get_ramdisk_server()->ramdisk_buf = vspace_new_pages(parent_vspace, seL4_ReadWrite,
                                                         n_pages, seL4_LargePageBits);

    if (get_ramdisk_server()->ramdisk_buf == NULL)
    {
        ZF_LOGE(RAMDISK_S "spawn_thread: failed to map virtual disk, err=%d.",
                error);
        return error;
    }

    /* Configure thread */
    sel4utils_thread_config_t config = thread_config_default(parent_simple,
                                                             parent_cspace_cspath.root,
                                                             seL4_NilData,
                                                             get_ramdisk_server()->server_ep_obj.cptr,
                                                             priority);

    error = sel4utils_configure_thread_config(parent_vka,
                                              parent_vspace,
                                              parent_vspace,
                                              config,
                                              &get_ramdisk_server()->server_thread);
    if (error != 0)
    {
        ZF_LOGE(RAMDISK_S "spawn_thread: sel4utils_configure_thread failed "
                          "with %d.",
                error);
        goto out;
    }

    NAME_THREAD(get_ramdisk_server()->server_thread.tcb.cptr, "ramdisk server");
    error = sel4utils_start_thread(&get_ramdisk_server()->server_thread,
                                   (sel4utils_thread_entry_fn)&ramdisk_server_main,
                                   NULL, NULL, 1);
    if (error != 0)
    {
        ZF_LOGE(RAMDISK_S "spawn_thread: sel4utils_start_thread failed with "
                          "%d.",
                error);
        goto out;
    }

    return 0;

out:
    RAMDISK_PRINTF("spawn_thread: Server ran into an error.\n");
    vka_free_object(parent_vka, &get_ramdisk_server()->server_ep_obj);
    vka_free_object(parent_vka, &get_ramdisk_server()->ramdisk_buf_obj);
    return error;
}

/**
 * @brief The starting point for the ramdisk server's thread.
 *
 */
void ramdisk_server_main()
{
    RAMDISK_PRINTF("main: started\n");

    seL4_MessageInfo_t tag;
    seL4_Error error = 0;
    seL4_Word sender_badge;
    cspacepath_t received_cap_path;

    while (1)
    {
        /* Alloc slot for frame cap from the message */
        error = vka_cspace_alloc_path(get_ramdisk_server()->server_vka, &received_cap_path);
        assert(error == 0);

        seL4_SetCapReceivePath(
            /* _service */ received_cap_path.root,
            /* index */ received_cap_path.capPtr,
            /* depth */ received_cap_path.capDepth);

        tag = recv(&sender_badge);

        seL4_SetMR(0, 42);
        seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(error, 0, 0, 1);

        // Send an empty reply, for now
        reply(reply_tag);
    }

    // serial_server_func_kill();
    /* After we break out of the loop, seL4_TCB_Suspend ourselves */
    ZF_LOGI(RAMDISK_S "main: Suspending.");
    seL4_TCB_Suspend(get_ramdisk_server()->server_thread.tcb.cptr);
}
