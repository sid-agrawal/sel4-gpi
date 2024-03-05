#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vspace/vspace.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/model_exporting.h>

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

#define CHECK_ERROR_GOTO(check, msg, err, loc) \
    do                                         \
    {                                          \
        if ((check) != seL4_NoError)           \
        {                                      \
            ZF_LOGE(RAMDISK_S "%s: %s"         \
                              ", %d.",         \
                    __func__,                  \
                    msg,                       \
                    check);                    \
            error = err;                       \
            goto loc;                          \
        }                                      \
    } while (0);

/*--- RAMDISK SERVER ---*/
static ramdisk_server_context_t ramdisk_server;

ramdisk_server_context_t *get_ramdisk_server(void)
{
    return &ramdisk_server;
}

int ramdisk_server_spawn_thread(simple_t *parent_simple,
                                vka_t *parent_vka,
                                vspace_t *parent_vspace,
                                seL4_CPtr gpi_ep,
                                seL4_CPtr parent_ep,
                                seL4_CPtr ads_ep,
                                uint8_t priority)
{
    return resource_server_spawn_thread(
        &get_ramdisk_server()->gen,
        GPICAP_TYPE_FILE,
        ramdisk_request_handler,
        parent_simple,
        parent_vka,
        parent_vspace,
        gpi_ep,
        parent_ep,
        ads_ep,
        priority,
        "ramdisk server",
        ramdisk_init);
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
 * To be run once at the beginning of ramdisk main
 */
int ramdisk_init()
{
    ramdisk_server_context_t *server = get_ramdisk_server();
    int error;

    /* Allocate the ramdisk's virtual disk */
    int n_pages = RAMDISK_SIZE_BYTES / SIZE_BITS_TO_BYTES(seL4_PageBits);
    printf("Allocating %d pages\n", n_pages);
    server->ramdisk_mo = malloc(sizeof(mo_client_context_t));
    seL4_CPtr free_slot;
    error = resource_server_next_slot(&get_ramdisk_server()->gen, &free_slot);
    CHECK_ERROR(error, "failed to get next cspace slot");

    error = mo_component_client_connect(server->gen.gpi_ep,
                                        free_slot,
                                        n_pages,
                                        server->ramdisk_mo);
    CHECK_ERROR(error, "failed to allocate virtual disk");
    RAMDISK_PRINTF("Allocated ramdisk\n");

    /* Map the virtual disk */
    error = ads_client_attach(server->gen.ads_conn,
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

    return error;
}

seL4_MessageInfo_t ramdisk_request_handler(seL4_MessageInfo_t tag, seL4_Word sender_badge, seL4_CPtr cap)
{
    int error;
    void *mo_vaddr;

    unsigned int op = seL4_GetMR(RDMSGREG_FUNC);
    uint64_t obj_id = get_object_id_from_badge(sender_badge);

    seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);
    if (sender_badge == 0)
    { /* Handle Unbadged Request */
        RAMDISK_PRINTF("Got message on unbadged EP\n");

        switch (op)
        {
        case RS_FUNC_GET_RR_REQ:
            uint64_t resource_id = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_ID);
            uint64_t blockno = get_local_object_id(resource_id);

            RAMDISK_PRINTF("Get RR for blockno %d\n", blockno);

            size_t mem_size = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_SIZE);
            void *mem_vaddr = (void *)seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_VADDR);

            RAMDISK_PRINTF("Shared mem at %p\n", mem_vaddr);
            RAMDISK_PRINTF("Can access shared mem %d\n", *((int *)mem_vaddr));

            // Initialize the model state
            CHECK_ERROR_GOTO(mem_size < (sizeof(rr_state_t) + sizeof(csv_rr_row_t)),
                             "Shared memory for RR is too small", RS_ERROR_RR_SIZE, done);
            rr_state_t *rr_state = (rr_state_t *)mem_vaddr;
            init_rr_state(rr_state);
            csv_rr_row_t *row_ptr = mem_vaddr + sizeof(rr_state_t);

            // Add the entry for the resource
            char block_res_id[CSV_MAX_STRING_SIZE];
            make_res_id(block_res_id, GPICAP_TYPE_BLOCK, resource_id);
            add_resource_rr(rr_state, GPICAP_TYPE_BLOCK, block_res_id, row_ptr);

            seL4_SetMR(RDMSGREG_FUNC, RS_FUNC_GET_RR_ACK);
            RAMDISK_PRINTF("Returning RR\n");

            break;
        default:
            RAMDISK_PRINTF("Op is %d\n", op);
            CHECK_ERROR_GOTO(1, "got invalid op on unbadged ep", error, done);
        }
    }
    else if (obj_id == BADGE_OBJ_ID_NULL)
    { /* Handle Untyped Request */
        RAMDISK_PRINTF("Got message on badged EP with no object id\n");

        switch (op)
        {
        case RD_FUNC_SANITY_REQ:
            /* Attach memory object to server ADS */
            error = resource_server_attach_mo(&get_ramdisk_server()->gen, cap, &mo_vaddr);
            CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

            RAMDISK_PRINTF("Can access vaddr %p, val 0x%x\n", mo_vaddr, *((int *)mo_vaddr));

            seL4_MessageInfo_ptr_set_length(&reply_tag, RDMSGREG_SANITY_ACK_END);
            seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_SANITY_ACK);
            seL4_SetMR(RDMSGREG_SANITY_ACK_VAL, *(int *)mo_vaddr);
            break;

        case RD_FUNC_CREATE_REQ:
            // Assign a new block to this ep
            CHECK_ERROR_GOTO(get_ramdisk_server()->free_blocks == NULL, "no more free blocks to assign",
                             RD_SERVER_ERROR_NO_BLOCKS, done);
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
            seL4_Word badge = resource_server_assign_new_badge(&get_ramdisk_server()->gen,
                                                               blockno,
                                                               get_client_id_from_badge(sender_badge));
            CHECK_ERROR_GOTO(badge == 0, "failed to assign new badge", RD_SERVER_ERROR_UNKNOWN, done);

            seL4_CPtr badged_ep;

            error = resource_server_badge_ep(&get_ramdisk_server()->gen,
                                             badge, &badged_ep);
            CHECK_ERROR_GOTO(error, "failed to mint client badge", error, done);

            /* Return this badged end point in the return message. */
            seL4_SetCap(0, badged_ep);
            seL4_MessageInfo_ptr_set_extraCaps(&reply_tag, 1);
            seL4_MessageInfo_ptr_set_length(&reply_tag, RDMSGREG_CREATE_ACK_END);
            seL4_SetMR(RDMSGREG_CREATE_ACK_ID, get_object_id_from_badge(badge));
            seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_CREATE_ACK);

            RAMDISK_PRINTF("Replying with badged EP: ");
            badge_print(badge);
            RAMDISK_PRINTF("\n");
            break;
        default:
            RAMDISK_PRINTF("Op is %d\n", op);
            CHECK_ERROR_GOTO(1, "got invalid op on badged ep without obj id", RD_SERVER_ERROR_UNKNOWN, done);
        }
    }
    else
    { /* Handle Typed Request */
        RAMDISK_PRINTF("Got message on EP with badge:");
        badge_print(sender_badge);
        RAMDISK_PRINTF("\n");

        gpi_cap_t cap_type = get_cap_type_from_badge(sender_badge);
        CHECK_ERROR_GOTO(cap_type != GPICAP_TYPE_BLOCK, "ramdisk server got invalid captype in badged EP",
                         RD_SERVER_ERROR_UNKNOWN, done);
        uint64_t blockno = get_local_object_id(obj_id);
        RAMDISK_PRINTF("Got op for blockno %ld\n", blockno);

        switch (op)
        {
        case RD_FUNC_READ_REQ:
            /* Attach memory object to server ADS */
            error = resource_server_attach_mo(&get_ramdisk_server()->gen, cap, &mo_vaddr);
            CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

            /* Read ramdisk */
            void *ramdisk_vaddr = ramdisk_ptr(blockno);
            RAMDISK_PRINTF("Reading from blockno %ld to %p\n", blockno, mo_vaddr);
            memcpy(mo_vaddr, ramdisk_vaddr, RAMDISK_BLOCK_SIZE);

            /* Detach MO from server ADS */
            // ARYA-TODO what if the MO is not of RAMDISK_BLOCK_SIZE?
            error = ads_client_rm(get_ramdisk_server()->gen.ads_conn, mo_vaddr, RAMDISK_BLOCK_SIZE);

            CHECK_ERROR_GOTO(error, "failed to detach client's MO from ADS", error, done);

            seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_READ_ACK);
            break;
        case RD_FUNC_WRITE_REQ:
            /* Attach memory object to server ADS */
            error = resource_server_attach_mo(&get_ramdisk_server()->gen, cap, &mo_vaddr);
            CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

            /* Write ramdisk */
            ramdisk_vaddr = ramdisk_ptr(blockno);
            RAMDISK_PRINTF("Writing from %p to blockno %ld\n", mo_vaddr, blockno);
            memcpy(ramdisk_vaddr, mo_vaddr, RAMDISK_BLOCK_SIZE);

            /* Detach MO from server ADS */
            // ARYA-TODO what if the MO is not of RAMDISK_BLOCK_SIZE?
            error = ads_client_rm(get_ramdisk_server()->gen.ads_conn, mo_vaddr, RAMDISK_BLOCK_SIZE);
            seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_WRITE_ACK);
            CHECK_ERROR_GOTO(error, "failed to detach client's MO from ADS", error, done);

            seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_WRITE_ACK);
            break;
        default:
            RAMDISK_PRINTF("Op is %d\n", op);
            CHECK_ERROR_GOTO(1, "got invalid op on badged ep with obj id", RD_SERVER_ERROR_UNKNOWN, done);
        }
    }

done:
    seL4_MessageInfo_ptr_set_label(&reply_tag, error);
    return reply_tag;
}
