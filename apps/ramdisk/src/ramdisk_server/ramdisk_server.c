#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vspace/vspace.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>

#include <ramdisk_server.h>

#define RAMDISK_S "RamDisk Server: "

#if RAMDISK_DEBUG
#define RAMDISK_PRINTF(...)       \
    do                            \
    {                             \
        printf("%s ", RAMDISK_S); \
        printf(__VA_ARGS__);      \
    } while (0);
#else
#define RAMDISK_PRINTF(...)
#endif

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
        if ((check) != seL4_NoError)      \
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

    return api_recv(get_ramdisk_server()->server_ep,
                    sender_badge_ptr,
                    get_ramdisk_server()->mcs_reply);
}

static inline void reply(seL4_MessageInfo_t tag)
{
    api_reply(get_ramdisk_server()->mcs_reply, tag);
}

static int vka_next_slot_fn(seL4_CPtr *slot)
{
    return vka_cspace_alloc(get_ramdisk_server()->server_vka, slot);
}

static int pd_next_slot_fn(seL4_CPtr *slot)
{
    return pd_client_next_slot(get_ramdisk_server()->pd_conn, slot);
}

seL4_Error
ramdisk_server_spawn_thread(simple_t *parent_simple,
                            vka_t *parent_vka,
                            vspace_t *parent_vspace,
                            seL4_CPtr gpi_ep,
                            seL4_CPtr parent_ep,
                            seL4_CPtr ads_ep,
                            uint8_t priority)
{
    RAMDISK_PRINTF("Starting ramdisk thread\n");

    seL4_Error error;
    cspacepath_t parent_cspace_cspath;
    seL4_MessageInfo_t tag;

    if (parent_simple == NULL || parent_vka == NULL || parent_vspace == NULL)
    {
        return seL4_InvalidArgument;
    }

    ramdisk_server_context_t *server = get_ramdisk_server();

    server->server_vka = parent_vka;
    server->gpi_server = gpi_ep;
    server->parent_ep = parent_ep;
    server->ads_conn = malloc(sizeof(ads_client_context_t));
    server->ads_conn->badged_server_ep_cspath.capPtr = ads_ep;
    server->next_slot = vka_next_slot_fn;

    /* Get a CPtr to the parent's root cnode. */
    vka_cspace_make_path(parent_vka, 0, &parent_cspace_cspath);

    /* Allocate the Endpoint that the server will be listening on. */
    vka_object_t server_ep_obj;
    error = vka_alloc_endpoint(parent_vka, &server_ep_obj);
    CHECK_ERROR(error, "failed in vka_alloc_endpoint");
    server->server_ep = server_ep_obj.cptr;

    RAMDISK_PRINTF("Allocated endpoint\n");

    /* Configure thread */
    sel4utils_thread_config_t config = thread_config_default(parent_simple,
                                                             parent_cspace_cspath.root,
                                                             seL4_NilData,
                                                             server->server_ep,
                                                             priority);

    sel4utils_thread_t thread;
    error = sel4utils_configure_thread_config(parent_vka,
                                              parent_vspace,
                                              parent_vspace,
                                              config,
                                              &thread);
    CHECK_ERROR_GOTO(error, "sel4utils_configure_thread failed", out2);

    RAMDISK_PRINTF("Starting ramdisk thread\n");
    NAME_THREAD(thread.tcb.cptr, "ramdisk server");
    error = sel4utils_start_thread(&thread,
                                   (sel4utils_thread_entry_fn)&ramdisk_server_main,
                                   NULL, NULL, 1);
    CHECK_ERROR_GOTO(error, "sel4utils_start_thread failed", out2);

    return 0;

out2:
    RAMDISK_PRINTF("spawn_thread: Server ran into an error.\n");
    vka_free_object(parent_vka, &server_ep_obj);
    return error;
}

int ramdisk_server_start(ads_client_context_t *ads_conn,
                         pd_client_context_t *pd_conn,
                         seL4_CPtr gpi_ep,
                         seL4_CPtr parent_ep)
{
    seL4_Error error;

    ramdisk_server_context_t *server = get_ramdisk_server();

    server->gpi_server = gpi_ep;
    server->ads_conn = ads_conn;
    server->pd_conn = pd_conn;
    server->parent_ep = parent_ep;
    server->next_slot = pd_next_slot_fn;

    /* Allocate the Endpoint that the server will be listening on. */
    error = pd_client_alloc_ep(server->pd_conn, &server->server_ep);
    CHECK_ERROR(error, "Failed to allocate endpoint for ramdisk server");
    RAMDISK_PRINTF("Allocated server ep at %d\n", (int)server->server_ep);

    RAMDISK_PRINTF("Going to main function\n");
    return ramdisk_server_main();
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
 * To be run once at the beginning of ramdisk main
 */
static int ramdisk_init()
{
    ramdisk_server_context_t *server = get_ramdisk_server();
    int error;

    /* Allocate the ramdisk's virtual disk */
    int n_pages = RAMDISK_SIZE_BYTES / SIZE_BITS_TO_BYTES(seL4_PageBits);
    printf("Allocating %d pages\n", n_pages);
    server->ramdisk_mo = malloc(sizeof(mo_client_context_t));
    seL4_CPtr free_slot;
    error = get_ramdisk_server()->next_slot(&free_slot);
    CHECK_ERROR(error, "failed to get next cspace slot");

    error = mo_component_client_connect(server->gpi_server,
                                        free_slot,
                                        n_pages,
                                        server->ramdisk_mo);
    CHECK_ERROR(error, "failed to allocate virtual disk");
    RAMDISK_PRINTF("Allocated ramdisk\n");

    /* Map the virtual disk */
    error = ads_client_attach(server->ads_conn,
                              NULL,
                              server->ramdisk_mo,
                              &server->ramdisk_buf);
    CHECK_ERROR(error, "failed to map virtual disk");
    RAMDISK_PRINTF("Mapped ramdisk\n");

    /* Setup disk block data structure */
    server->free_blocks = malloc(sizeof(ramdisk_block_node_t));
    server->free_blocks->blockno = 0;
    server->free_blocks->n_blocks = RAMDISK_SIZE_BYTES / RAMDISK_BLOCK_SIZE;
    server->free_blocks->next = NULL;

    /* Send our ep to the parent process */
    RAMDISK_PRINTF("Messaging parent process at slot %d, sending ep %d\n", (int)server->parent_ep, (int)server->server_ep);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 0);
    seL4_SetCap(0, server->server_ep);
    seL4_Send(server->parent_ep, tag);

    return error;
}

/**
 * @brief The starting point for the ramdisk server's thread.
 *
 */
int ramdisk_server_main()
{
    RAMDISK_PRINTF("main: started\n");

    seL4_MessageInfo_t tag;
    seL4_Error error = 0;
    seL4_Word sender_badge;
    cspacepath_t received_cap_path;
    received_cap_path.root = PD_CAP_ROOT;
    received_cap_path.capDepth = PD_CAP_DEPTH;

    error = ramdisk_init();
    CHECK_ERROR_GOTO(error, "failed to initialize ramdisk", out);

    while (1)
    {
        /* Alloc cap receive slot*/
        error = get_ramdisk_server()->next_slot(&received_cap_path.capPtr);
        CHECK_ERROR_GOTO(error, "failed to alloc cap receive slot", out);
        RAMDISK_PRINTF("Next slot is %d\n", received_cap_path.capPtr);

        seL4_SetCapReceivePath(
            received_cap_path.root,
            received_cap_path.capPtr,
            received_cap_path.capDepth);

        /* Receive a message */
        tag = recv(&sender_badge);
        unsigned int op = seL4_GetMR(RAMDISK_MR_OP);

        seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

        if (sender_badge == 0)
        { /* Handle Untyped Request */
            RAMDISK_PRINTF("Got message on EP with no badge value\n");

            switch (op)
            {
            case RAMDISK_SANITY_TEST:
                mo_client_context_t mo_conn;
                mo_conn.badged_server_ep_cspath = received_cap_path;
                void *mo_vaddr;
                error = ads_client_attach(get_ramdisk_server()->ads_conn,
                                          NULL,
                                          &mo_conn,
                                          &mo_vaddr);
                CHECK_ERROR_GOTO(error, "failed to attach client's MO to ADS", done);

                RAMDISK_PRINTF("Can access vaddr %p, val 0x%x\n", mo_vaddr, *((int *)mo_vaddr));

                reply_tag = seL4_MessageInfo_set_length(reply_tag, 1);
                seL4_SetMR(0, *(int *)mo_vaddr);
                break;

            case RAMDISK_GET_BLOCK:
                // Assign a new block to this ep
                CHECK_ERROR_GOTO(get_ramdisk_server()->free_blocks == NULL, "no more free blocks to assign", done);
                uint64_t blockno = get_ramdisk_server()->free_blocks->blockno;

                RAMDISK_PRINTF("Allocating blockno %ld\n", blockno);

                // Update free block list
                get_ramdisk_server()->free_blocks->blockno++;
                get_ramdisk_server()->free_blocks->n_blocks--;
                if (get_ramdisk_server()->free_blocks->n_blocks <= 0)
                {
                    get_ramdisk_server()->free_blocks = get_ramdisk_server()->free_blocks->next;
                }

                // Create the badged endpoint
                seL4_Word badge = ramdisk_assign_new_badge(blockno);
                seL4_CPtr badged_ep;

                error = pd_client_badge_ep(get_ramdisk_server()->pd_conn,
                                           get_ramdisk_server()->server_ep,
                                           badge,
                                           &badged_ep);
                CHECK_ERROR_GOTO(error, "failed to mint client badge", done);

                /* Return this badged end point in the return message. */
                seL4_SetCap(0, badged_ep);
                reply_tag = seL4_MessageInfo_set_extraCaps(reply_tag, 1);

                RAMDISK_PRINTF("Replying with badged EP: ");
                badge_print(badge);
                printf("\n");
                break;
            default:
                CHECK_ERROR_GOTO(1, "got invalid op on unbadged ep", done);
            }
        }
        else
        { /* Handle Typed Request */
            RAMDISK_PRINTF("Got message on EP with badge:");
            badge_print(sender_badge);
            printf("\n");

            gpi_cap_t cap_type = get_cap_type_from_badge(sender_badge);
            CHECK_ERROR_GOTO(cap_type != GPICAP_TYPE_BLOCK, "ramdisk server got invalid captype in badged EP", done);
            uint64_t blockno = get_object_id_from_badge(sender_badge);

            RAMDISK_PRINTF("Got op for blockno %ld\n", blockno);

            switch (op)
            {
            case RAMDISK_READ:
            case RAMDISK_WRITE:
                /* Attach memory object to server ADS */
                CHECK_ERROR_GOTO(seL4_MessageInfo_get_extraCaps(tag) != 1,
                                 "client did not attach MO for read/write op", done);
                mo_client_context_t mo_conn;
                mo_conn.badged_server_ep_cspath = received_cap_path;
                void *mo_vaddr;
                error = ads_client_attach(get_ramdisk_server()->ads_conn,
                                          NULL,
                                          &mo_conn,
                                          &mo_vaddr);
                CHECK_ERROR_GOTO(error, "failed to attach client's MO to ADS", done);

                /* Read/write ramdisk */
                void *ramdisk_vaddr = ramdisk_ptr(blockno);
                if (op == RAMDISK_READ)
                {
                    RAMDISK_PRINTF("Reading from blockno %ld to %p\n", blockno, mo_vaddr);
                    memcpy(mo_vaddr, ramdisk_vaddr, RAMDISK_BLOCK_SIZE);
                }
                else
                { // op is write
                    RAMDISK_PRINTF("Writing from %p to blockno %ld\n", mo_vaddr, blockno);
                    memcpy(ramdisk_vaddr, mo_vaddr, RAMDISK_BLOCK_SIZE);
                }

                /* Detach MO from server ADS */
                // ARYA-TODO what if the MO is not of RAMDISK_BLOCK_SIZE?
                error = ads_client_rm(get_ramdisk_server()->ads_conn, mo_vaddr, RAMDISK_BLOCK_SIZE);
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
        reply_tag = seL4_MessageInfo_set_label(reply_tag, error);
        reply(reply_tag);
        pd_client_free_slot(get_ramdisk_server()->pd_conn, received_cap_path.capPtr);
    }

    // serial_server_func_kill();
    /* After we break out of the loop, seL4_TCB_Suspend ourselves */
out:
    ZF_LOGI(RAMDISK_S "main: Suspending.");
    return -1;
}