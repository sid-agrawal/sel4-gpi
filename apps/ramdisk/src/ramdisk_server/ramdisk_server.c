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
    RAMDISK_PRINTF("Allocating %d pages\n", n_pages);
    server->ramdisk_mo = malloc(sizeof(mo_client_context_t));
    seL4_CPtr free_slot;
    error = resource_server_next_slot(&get_ramdisk_server()->gen, &free_slot);
    CHECK_ERROR(error, "failed to get next cspace slot");

    error = mo_component_client_connect(server->gen.mo_ep,
                                        free_slot,
                                        n_pages,
                                        server->ramdisk_mo);
    CHECK_ERROR(error, "failed to allocate virtual disk");
    RAMDISK_PRINTF("Allocated ramdisk\n");

    /* Map the virtual disk */
    error = ads_client_attach(&server->gen.ads_conn,
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

    /* Create the block resources */
    for (int i = 0; i < server->free_blocks->n_blocks; i++)
    {
        // Local resource ID is the block ID
        resource_server_create_resource(&server->gen, i);
    }

    return error;
}

seL4_MessageInfo_t ramdisk_request_handler(seL4_MessageInfo_t tag, seL4_Word sender_badge, seL4_CPtr cap, bool *need_new_recv_cap)
{
    int error;
    void *mo_vaddr;
    *need_new_recv_cap = true;
    unsigned int op = seL4_GetMR(RDMSGREG_FUNC);
    uint64_t obj_id = get_object_id_from_badge(sender_badge);

    seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);
    if (sender_badge == 0)
    { /* Handle Unbadged Request */
        RAMDISK_PRINTF("Got message on unbadged EP\n");

        switch (op)
        {
        case RS_FUNC_GET_RR_REQ:
            *need_new_recv_cap = false;

            uint64_t resource_id = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_ID);
            uint64_t blockno = get_local_object_id_from_badge(resource_id);
            char pd_id[CSV_MAX_STRING_SIZE];
            make_res_id(pd_id, GPICAP_TYPE_PD, seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_PD_ID));
            char rd_pd_id[CSV_MAX_STRING_SIZE];
            make_res_id(rd_pd_id, GPICAP_TYPE_PD, seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_RS_PD_ID));

            RAMDISK_PRINTF("Get RR for blockno %d\n", blockno);

            size_t mem_size = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_SIZE);
            void *mem_vaddr = (void *)seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_VADDR);

            RAMDISK_PRINTF("Shared mem at %p\n", mem_vaddr);
            RAMDISK_PRINTF("Can access shared mem %d\n", *((int *)mem_vaddr));

            // Initialize the model state
            CHECK_ERROR_GOTO(mem_size < (sizeof(rr_state_t) + 6 * sizeof(csv_rr_row_t)),
                             "Shared memory for RR is too small", RS_ERROR_RR_SIZE, done);
            rr_state_t *rr_state = (rr_state_t *)mem_vaddr;
            init_rr_state(rr_state);
            csv_rr_row_t *row_ptr = mem_vaddr + sizeof(rr_state_t);

            /* Add the block resource space */
            // (XXX) Arya: Assumes there is only one block space, and it is space 1
            char block_space_res_id[CSV_MAX_STRING_SIZE];
            snprintf(block_space_res_id, CSV_MAX_STRING_SIZE, "BLOCK_SPACE_%d", 1);
            add_resource_rr(rr_state, GPICAP_TYPE_BLOCK, block_space_res_id, row_ptr);
            row_ptr++;

            // Add the has_access_to row for block resource space
            add_has_access_to_rr(rr_state,
                                 rd_pd_id,
                                 block_space_res_id,
                                 false,
                                 row_ptr);
            row_ptr++;

            // Add the entry for the resource
            char block_res_id[CSV_MAX_STRING_SIZE];
            make_res_id(block_res_id, GPICAP_TYPE_BLOCK, resource_id);
            add_resource_rr(rr_state, GPICAP_TYPE_BLOCK, block_res_id, row_ptr);
            row_ptr++;

            // Add the has_access_to row
            add_has_access_to_rr(rr_state,
                                 pd_id,
                                 block_res_id,
                                 false,
                                 row_ptr); // (XXX) Arya: how to determine is_mapped
            row_ptr++;

            // Add the subset row
            add_resource_depends_on_rr(rr_state, block_res_id, block_space_res_id, REL_TYPE_SUBSET, row_ptr);
            row_ptr++;

            // Add RR from block to MO
            // (XXX) Arya: Actually don't show this, we are pretending these are real blocks?
            // char mo_res_id[CSV_MAX_STRING_SIZE];
            // make_res_id(mo_res_id, GPICAP_TYPE_MO, get_ramdisk_server()->ramdisk_mo->id);
            // add_resource_depends_on_rr(rr_state, block_res_id, mo_res_id, row_ptr);

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
        uint64_t client_id = get_client_id_from_badge(sender_badge);

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
            //* Attach memory object to server ADS */
            mo_vaddr = get_ramdisk_server()->shared_mem[client_id];
            if (mo_vaddr == NULL)
            {
                error = resource_server_attach_mo(&get_ramdisk_server()->gen, cap, &mo_vaddr);
                CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

                RAMDISK_PRINTF("Attached MO\n");
                get_ramdisk_server()->shared_mem[client_id] = mo_vaddr;
            }

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

            // Create the resource endpoint
            seL4_CPtr dest;
            error = resource_server_give_resource(&get_ramdisk_server()->gen,
                                                  get_ns_id_from_badge(sender_badge),
                                                  blockno,
                                                  get_client_id_from_badge(sender_badge),
                                                  &dest);
            CHECK_ERROR_GOTO(error, "Failed to give the resource", error, done);

            // Send the reply
            seL4_MessageInfo_ptr_set_length(&reply_tag, RDMSGREG_CREATE_ACK_END);
            seL4_SetMR(RDMSGREG_CREATE_ACK_DEST, dest);
            seL4_SetMR(RDMSGREG_CREATE_ACK_ID, get_global_object_id_from_local(get_ramdisk_server()->gen.server_id, blockno));
            seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_CREATE_ACK);

            RAMDISK_PRINTF("Resource is in dest slot %d\n", (int)dest);
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
        uint64_t blockno = get_local_object_id_from_badge(obj_id);
        uint64_t client_id = get_client_id_from_badge(sender_badge);
        RAMDISK_PRINTF("Got op for blockno %ld\n", blockno);

        switch (op)
        {
        case RD_FUNC_READ_REQ:
            RAMDISK_PRINTF("Op is read\n");

            /* Attach memory object to server ADS */
            mo_vaddr = get_ramdisk_server()->shared_mem[client_id];
            CHECK_ERROR_GOTO(mo_vaddr == NULL, "MO for client did not exist", RD_SERVER_ERROR_UNKNOWN, done);
            *need_new_recv_cap = false;

            /* Read ramdisk */
            void *ramdisk_vaddr = ramdisk_ptr(blockno);
            RAMDISK_PRINTF("Reading from blockno %ld to %p\n", blockno, mo_vaddr);
            memcpy(mo_vaddr, ramdisk_vaddr, RAMDISK_BLOCK_SIZE);

            RAMDISK_PRINTF("Read block\n");

            /* (XXX) Arya: Todo Detach MO from server ADS */
            // ARYA-TODO what if the MO is not of RAMDISK_BLOCK_SIZE?
            // error = ads_client_rm(&get_ramdisk_server()->gen.ads_conn, mo_vaddr, RAMDISK_BLOCK_SIZE);
            // CHECK_ERROR_GOTO(error, "failed to detach client's MO from ADS", error, done);

            seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_READ_ACK);
            break;
        case RD_FUNC_WRITE_REQ:
            RAMDISK_PRINTF("Op is write\n");

            //* Attach memory object to server ADS */
            mo_vaddr = get_ramdisk_server()->shared_mem[client_id];
            CHECK_ERROR_GOTO(mo_vaddr == NULL, "MO for client did not exist", RD_SERVER_ERROR_UNKNOWN, done);
            *need_new_recv_cap = false;

            /* Write ramdisk */
            ramdisk_vaddr = ramdisk_ptr(blockno);
            RAMDISK_PRINTF("Writing from %p to blockno %ld\n", mo_vaddr, blockno);
            memcpy(ramdisk_vaddr, mo_vaddr, RAMDISK_BLOCK_SIZE);

            /* (XXX) Arya: Todo Detach MO from server ADS */
            // ARYA-TODO what if the MO is not of RAMDISK_BLOCK_SIZE?
            // error = ads_client_rm(&get_ramdisk_server()->gen.ads_conn, mo_vaddr, RAMDISK_BLOCK_SIZE);
            // CHECK_ERROR_GOTO(error, "failed to detach client's MO from ADS", error, done);

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
