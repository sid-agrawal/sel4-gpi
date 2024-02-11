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
#define RAMDISK_MR_OP 0
#define RAMDISK_CAP_MO 0

/* Ramdisk opcodes */
enum RAMDISK_OPCODE
{
    RAMDISK_READ,
    RAMDISK_WRITE,
    RAMDISK_GET_BLOCK
};

/* Other constants */
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

#define CHECK_ERROR_GOTO(check, msg, loc) \
    do                                    \
    {                                     \
        if (check != seL4_NoError)        \
        {                                 \
            ZF_LOGE(RAMDISK_S "%s: %s"    \
                              ", %d.",    \
                    __func__,             \
                    msg,                  \
                    check);               \
            error = -1;                   \
            goto loc;                     \
        }                                 \
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
                            vspace_t *parent_vspace, seL4_CPtr gpi_server,
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

    ramdisk_server_context_t *server = get_ramdisk_server();

    server->server_simple = parent_simple;
    server->server_vka = parent_vka;
    server->server_cspace = parent_cspace_cspath.root;
    server->server_vspace = parent_vspace;
    server->gpi_server = gpi_server;

    /* Get a CPtr to the parent's root cnode. */
    vka_cspace_make_path(parent_vka, 0, &parent_cspace_cspath);

    /* Allocate the Endpoint that the server will be listening on. */
    error = vka_alloc_endpoint(parent_vka, &server->server_ep_obj);
    CHECK_ERROR(error, "failed in vka_alloc_endpoint");
    *server_ep_cap = server->server_ep_obj.cptr;

    /* Initialize ADS connection */
    /*
    error = ads_component_client_connect(server->gpi_server,
                                         server->server_vka,
                                         &server->ads_conn);
    */
    seL4_CPtr ads_cap;
    error = forge_ads_cap_from_vspace(server->server_vspace, server->server_vka,
                                      &ads_cap);
    vka_cspace_make_path(server->server_vka, ads_cap, &server->ads_conn.badged_server_ep_cspath);
    CHECK_ERROR(error, "failed to connect to ADS component");

    /* Allocate the ramdisk's virtual disk */
    int n_pages = RAMDISK_SIZE_BYTES / SIZE_BITS_TO_BYTES(seL4_LargePageBits);
    server->ramdisk_buf = vspace_new_pages(parent_vspace, seL4_ReadWrite,
                                           n_pages, seL4_LargePageBits);

    CHECK_ERROR(server->ramdisk_buf == NULL, "failed to map virtual disk");

    /* Setup disk block data structure */
    server->free_blocks = malloc(sizeof(ramdisk_block_node_t));
    server->free_blocks->blockno = 0;
    server->free_blocks->n_blocks = n_pages;

    /* Configure thread */
    sel4utils_thread_config_t config = thread_config_default(parent_simple,
                                                             parent_cspace_cspath.root,
                                                             seL4_NilData,
                                                             server->server_ep_obj.cptr,
                                                             priority);

    error = sel4utils_configure_thread_config(parent_vka,
                                              parent_vspace,
                                              parent_vspace,
                                              config,
                                              &server->server_thread);
    CHECK_ERROR_GOTO(error, "sel4utils_configure_thread failed", out);

    NAME_THREAD(server->server_thread.tcb.cptr, "ramdisk server");
    error = sel4utils_start_thread(&server->server_thread,
                                   (sel4utils_thread_entry_fn)&ramdisk_server_main,
                                   NULL, NULL, 1);
    CHECK_ERROR_GOTO(error, "sel4utils_start_thread failed", out);

    return 0;

out:
    RAMDISK_PRINTF("spawn_thread: Server ran into an error.\n");
    vka_free_object(parent_vka, &server->server_ep_obj);
    vka_free_object(parent_vka, &server->ramdisk_buf_obj); // ARYA-TODO does this unmap?
    return error;
}

/**
 * Create a pointer to the ramdisk buf from a sector and offset
 */
static void *ramdisk_ptr(unsigned int sector)
{
    assert(sector < (RAMDISK_SIZE_BYTES / RAMDISK_BLOCK_SIZE));
    return get_ramdisk_server()->ramdisk_buf + sector * RAMDISK_BLOCK_SIZE;
}

uint64_t ramdisk_assign_new_badge(uint64_t blockno)
{
    // Add the blockno to the badge
    seL4_Word badge_val = gpi_new_badge(GPICAP_TYPE_BLOCK,
                                        0x00,
                                        0x00,
                                        blockno);

    assert(badge_val != 0);
    RAMDISK_PRINTF(RAMDISK_S "ramdisk_assign_new_badge: new badge: %lx\n", badge_val);
    return badge_val;
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
        /* Alloc cap receive path*/
        error = vka_cspace_alloc_path(get_ramdisk_server()->server_vka, &received_cap_path);
        CHECK_ERROR_GOTO(error, "failed to alloc cap receive path", out);

        seL4_SetCapReceivePath(
            /* _service */ received_cap_path.root,
            /* index */ received_cap_path.capPtr,
            /* depth */ received_cap_path.capDepth);

        /* Receive a message */
        tag = recv(&sender_badge);
        unsigned int op = seL4_GetMR(RAMDISK_MR_OP);

        seL4_MessageInfo_t reply_tag;

        if (sender_badge == 0)
        { /* Handle Untyped Request */
            RAMDISK_PRINTF("Got message on EP with no badge value\n");
            CHECK_ERROR_GOTO(op != RAMDISK_GET_BLOCK, "got invalid op on unbadged ep", done);

            // Assign a new block to this ep
            CHECK_ERROR_GOTO(get_ramdisk_server()->free_blocks == NULL, "no more free blocks to assign", done);
            uint64_t blockno = get_ramdisk_server()->free_blocks->blockno;

            // Update free block list
            get_ramdisk_server()->free_blocks->blockno++;
            get_ramdisk_server()->free_blocks->n_blocks--;
            if (get_ramdisk_server()->free_blocks->n_blocks <= 0)
            {
                get_ramdisk_server()->free_blocks = get_ramdisk_server()->free_blocks->next;
            }

            // Create the badged endpoint
            cspacepath_t src, dest;
            vka_cspace_make_path(get_ramdisk_server()->server_vka,
                                 get_ramdisk_server()->server_ep_obj.cptr, &src);

            seL4_CPtr dest_cptr;
            vka_cspace_alloc(get_ramdisk_server()->server_vka, &dest_cptr);
            vka_cspace_make_path(get_ramdisk_server()->server_vka, dest_cptr, &dest);

            seL4_Word badge = ramdisk_assign_new_badge(blockno);
            error = vka_cnode_mint(&dest,
                                   &src,
                                   seL4_AllRights,
                                   badge);
            CHECK_ERROR_GOTO(error, "failed to mint client badge", done);

            /* Return this badged end point in the return message. */
            seL4_SetCap(0, dest.capPtr);
            reply_tag = seL4_MessageInfo_new(error, 0, 1, 1);
        }
        else
        { /* Handle Typed Request */
            RAMDISK_PRINTF("Got message on EP with badge:");
            badge_print(sender_badge);
            printf("\n");

            gpi_cap_t cap_type = get_cap_type_from_badge(sender_badge);
            CHECK_ERROR_GOTO(cap_type != GPICAP_TYPE_BLOCK, "ramdisk server got invalid captype in badged EP", done);
            uint64_t blockno = get_object_id_from_badge(sender_badge);

            reply_tag = seL4_MessageInfo_new(error, 0, 0, 1);
            switch (op)
            {
            case RAMDISK_READ:
            case RAMDISK_WRITE:
                /* Attach memory object to server ADS */
                CHECK_ERROR_GOTO(seL4_MessageInfo_get_extraCaps(tag) != 1, "client did not attach MO for read/write op", done);
                mo_client_context_t mo_conn;
                mo_conn.badged_server_ep_cspath = received_cap_path;
                void *mo_vaddr;
                error = ads_client_attach(&get_ramdisk_server()->ads_conn,
                                          NULL,
                                          &mo_conn,
                                          &mo_vaddr);
                CHECK_ERROR_GOTO(error, "failed to attach client's MO to ADS", done);

                /* Read/write ramdisk */
                void *ramdisk_vaddr = ramdisk_ptr(blockno);
                if (op == RAMDISK_READ)
                {
                    memcpy(mo_vaddr, ramdisk_vaddr, RAMDISK_BLOCK_SIZE);
                }
                else
                { // op is write
                    memcpy(ramdisk_vaddr, mo_vaddr, RAMDISK_BLOCK_SIZE);
                }

                /* Detach MO from server ADS */
                // ARYA-TODO what if the MO is not of RAMDISK_BLOCK_SIZE?
                error = ads_client_rm(&get_ramdisk_server()->ads_conn, mo_vaddr, RAMDISK_BLOCK_SIZE);
                CHECK_ERROR_GOTO(error, "failed to detach client's MO from ADS", done);

                break;
            default:
                ZF_LOGE(RAMDISK_S "%s: got unexpected opcode %d\n",
                        __func__,
                        op);

                error = -1;
            }
        }

    done:
        reply(reply_tag);
    }

    // serial_server_func_kill();
    /* After we break out of the loop, seL4_TCB_Suspend ourselves */
out:
    ZF_LOGI(RAMDISK_S "main: Suspending.");
    seL4_TCB_Suspend(get_ramdisk_server()->server_thread.tcb.cptr);
}

/*--- RAMDISK CLIENT ---*/

static ramdisk_client_context_t ramdisk_client;

int ramdisk_client_alloc_block(seL4_CPtr server_ep_cap,
                               vka_t *client_vka,
                               ramdisk_client_context_t *ret_conn)
{
    /* Send a request to the server on its public EP */

    // Alloc a slot for the incoming cap.
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(client_vka, &dest_cptr);
    cspacepath_t path;
    vka_cspace_make_path(client_vka, dest_cptr, &path);
    seL4_SetCapReceivePath(
        /* _service */ path.root,
        /* index */ path.capPtr,
        /* depth */ path.capDepth);

    /* Request a new block from server */
    seL4_SetMR(RAMDISK_MR_OP, RAMDISK_GET_BLOCK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);

    tag = seL4_Call(server_ep_cap, tag);
    int error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "failed to get block from ramdisk server\n");
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);

    ret_conn->badged_server_ep_cspath = path;
    return 0;
}

int ramdisk_client_read(ramdisk_client_context_t *conn, mo_client_context_t *mo)
{
    seL4_Error error;

    /* Send IPC to ramdisk server */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetMR(RAMDISK_MR_OP, RAMDISK_READ);
    seL4_SetCap(RAMDISK_CAP_MO, mo->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    error = seL4_MessageInfo_get_label(tag);

    return error;
}

int ramdisk_client_write(ramdisk_client_context_t *conn, mo_client_context_t *mo)
{
    seL4_Error error;

    /* Send IPC to ramdisk server */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetMR(RAMDISK_MR_OP, RAMDISK_WRITE);
    seL4_SetCap(RAMDISK_CAP_MO, mo->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    error = seL4_MessageInfo_get_label(tag);

    return error;
}