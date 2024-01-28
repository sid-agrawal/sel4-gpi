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

/* Memory regions for IPC to ramdisk server */
#define RAMDISK_OP 0
#define RAMDISK_SECTOR 1

/* Ramdisk opcodes */
#define RAMDISK_READ 0
#define RAMDISK_WRITE 1
#define RAMDISK_FLUSH 2
#define RAMDISK_REGISTER_CLIENT 3

/* Other constants */
#define IPC_FRAME_PAGE_BITS seL4_PageBits

#define RAMDISK_PRINTF(...)       \
    do                            \
    {                             \
        printf("%s ", RAMDISK_S); \
        printf(__VA_ARGS__);      \
    } while (0);

#define CHECK_ERROR(error, msg)        \
    do                                 \
    {                                  \
        if (error != seL4_NoError)     \
        {                              \
            ZF_LOGE(RAMDISK_S "%s: %s" \
                              ", %d.", \
                    __func__,          \
                    msg,               \
                    error);            \
            return error;              \
        }                              \
    } while (0);

/*--- RAMDISK SERVER ---*/
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
    get_ramdisk_server()->shared_mem =  NULL;

    /* Get a CPtr to the parent's root cnode. */
    vka_cspace_make_path(parent_vka, 0, &parent_cspace_cspath);

    /* Allocate the Endpoint that the server will be listening on. */
    error = vka_alloc_endpoint(parent_vka, &get_ramdisk_server()->server_ep_obj);
    if (error != seL4_NoError)
    {
        ZF_LOGE(RAMDISK_S "spawn_thread: failed to alloc endpoint, err=%d.",
                error);
        return error;
    }

    *server_ep_cap = get_ramdisk_server()->server_ep_obj.cptr;

    /* Allocate the ramdisk's virtual disk */

    /*
    error = vka_alloc_frame(parent_vka, RAMDISK_SIZE_BITS, &get_ramdisk_server()->ramdisk_buf_obj);
    if (error != seL4_NoError)
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
    if (error != seL4_NoError)
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
    if (error != seL4_NoError)
    {
        ZF_LOGE(RAMDISK_S "spawn_thread: sel4utils_start_thread failed with "
                          "%d.",
                error);
        goto out;
    }

    return 0;

out:
    RAMDISK_PRINTF("spawn_thread: Server ran into an error.\n");
    vka_free_object(parent_vka, &get_ramdisk_server()->server_ep_obj); // ARYA-TODO does this unmap?
    vka_free_object(parent_vka, &get_ramdisk_server()->ramdisk_buf_obj);
    return error;
}

/**
 * Map a shared frame in the vspace
 */
static seL4_Error map_shared_frame(vspace_t *vspace, seL4_CPtr *frame, void **vaddr)
{
    *vaddr = vspace_map_pages(vspace, frame, NULL, seL4_ReadWrite, 1, seL4_PageBits, 0);

    return vaddr == NULL; // Returns error if vaddr is NULL
}

/**
 * Unmap a shared frame from the vspace
 */
static void unmap_shared_frame(vspace_t *vspace,  void *vaddr)
{
    // ARYA-TODO free the shared mem cap as well
    vspace_unmap_pages(vspace, vaddr, 1, seL4_PageBits, NULL);
}

/**
 * Create a pointer to the ramdisk buf from a sector and offset
 */
static void *ramdisk_ptr(unsigned int sector)
{
    assert(sector < (RAMDISK_SIZE_BYTES / RAMDISK_BLOCK_SIZE));
    return get_ramdisk_server()->ramdisk_buf + sector * RAMDISK_BLOCK_SIZE;
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

    // Allocate initial cap receive path
    error = vka_cspace_alloc_path(get_ramdisk_server()->server_vka, &received_cap_path);
    if (error != seL4_NoError)
    {
        ZF_LOGE(RAMDISK_S "%s: failed to alloc initial cap receive path ",
                __func__);
        goto exit;
    }

    seL4_SetCapReceivePath(
            /* _service */ received_cap_path.root,
            /* index */ received_cap_path.capPtr,
            /* depth */ received_cap_path.capDepth);

    while (1)
    {
        tag = recv(&sender_badge);
        unsigned int op = seL4_GetMR(RAMDISK_OP);

        seL4_MessageInfo_t reply_tag;

        switch (op)
        {
        case RAMDISK_REGISTER_CLIENT:
            // Map the shared memory page
            assert(seL4_MessageInfo_get_extraCaps(tag) == 1);
            map_shared_frame(get_ramdisk_server()->server_vspace, &received_cap_path.capPtr, &get_ramdisk_server()->shared_mem);

            /* Alloc slot for future IPC caps */
            // ARYA-TODO need to free this slot?
            error = vka_cspace_alloc_path(get_ramdisk_server()->server_vka, &received_cap_path);
            assert(error == 0);

            seL4_SetCapReceivePath(
                /* _service */ received_cap_path.root,
                /* index */ received_cap_path.capPtr,
                /* depth */ received_cap_path.capDepth);
            
            break;
        case RAMDISK_READ:
        case RAMDISK_WRITE:
            if (&get_ramdisk_server()->shared_mem == NULL) {
                ZF_LOGE(RAMDISK_S "%s: client hasn't registered shared memory ",
                        __func__);
                error = 1;
                goto done;
            }

            void *ramdisk_vaddr = ramdisk_ptr(seL4_GetMR(RAMDISK_SECTOR));

            if (op == RAMDISK_READ)
            {
                memcpy(get_ramdisk_server()->shared_mem, ramdisk_vaddr, RAMDISK_BLOCK_SIZE);
            }
            else
            { // op is write
                memcpy(ramdisk_vaddr, get_ramdisk_server()->shared_mem, RAMDISK_BLOCK_SIZE);
            }
            break;
        case RAMDISK_FLUSH:
            // ARYA-TODO what to do for flush?
            error = 0;
            break;
        default:
            ZF_LOGE(RAMDISK_S "%s: got unexpected opcode %d\n",
                    __func__,
                    op);

            error = 1;
        }

    done:
        reply_tag = seL4_MessageInfo_new(error, 0, 0, 1);
        reply(reply_tag);
    }

    // serial_server_func_kill();
    /* After we break out of the loop, seL4_TCB_Suspend ourselves */
exit:
    ZF_LOGI(RAMDISK_S "main: Suspending.");
    seL4_TCB_Suspend(get_ramdisk_server()->server_thread.tcb.cptr);
}

/*--- RAMDISK CLIENT ---*/

static ramdisk_client_context_t ramdisk_client;

ramdisk_client_context_t *get_ramdisk_client(void)
{
    return &ramdisk_client;
}

seL4_Error
ramdisk_client_init(vka_t *client_vka,
                    vspace_t *client_vspace,
                    seL4_CPtr server_ep_cap)
{
    get_ramdisk_client()->client_vka = client_vka;
    get_ramdisk_client()->client_vspace = client_vspace;
    get_ramdisk_client()->server_ep_cap = server_ep_cap;

    /* Alloc shared memory for IPC */
    seL4_Error error;
    vka_object_t frame_obj;
    error = vka_alloc_frame(get_ramdisk_client()->client_vka, seL4_PageBits, &frame_obj);
    CHECK_ERROR(error, "failed to allocate shared memory\n");

    error = map_shared_frame(get_ramdisk_client()->client_vspace, &frame_obj.cptr, &get_ramdisk_client()->shared_mem);
    CHECK_ERROR(error, "failed to map shared memory\n");
    // ARYA-TODO: support deregistering client and unmapping these frames

    /* Register shared mem with the server */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetMR(RAMDISK_OP, RAMDISK_REGISTER_CLIENT);
    seL4_SetCap(0, frame_obj.cptr);
    tag = seL4_Call(get_ramdisk_client()->server_ep_cap, tag);
    error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "failed to register client with ramdisk server\n");

    return error;
}

int ramdisk_read(unsigned int sector, void *buf)
{
    seL4_Error error;

    // Send IPC to ramdisk server
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(RAMDISK_OP, RAMDISK_READ);
    seL4_SetMR(RAMDISK_SECTOR, sector);
    tag = seL4_Call(get_ramdisk_client()->server_ep_cap, tag);
    error = seL4_MessageInfo_get_label(tag);

    // Copy result to local buffer
    if (error == seL4_NoError)
    {
        memcpy(buf, get_ramdisk_client()->shared_mem, RAMDISK_BLOCK_SIZE);
    }

    return error;
}

int ramdisk_write(unsigned int sector, void *buf)
{
    seL4_Error error;

    // Copy buffer to ipc frame
    memcpy(get_ramdisk_client()->shared_mem, buf, RAMDISK_BLOCK_SIZE);

    // Send IPC to ramdisk server
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(RAMDISK_OP, RAMDISK_WRITE);
    seL4_SetMR(RAMDISK_SECTOR, sector);
    tag = seL4_Call(get_ramdisk_client()->server_ep_cap, tag);
    error = seL4_MessageInfo_get_label(tag);

    return error;
}

int ramdisk_flush(void)
{
    seL4_Error error;

    // Send IPC to ramdisk server
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(RAMDISK_OP, RAMDISK_FLUSH);

    tag = seL4_Call(get_ramdisk_client()->server_ep_cap, tag);
    error = seL4_MessageInfo_get_label(tag);

    return error;
}