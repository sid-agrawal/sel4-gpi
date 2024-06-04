#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vspace/vspace.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_remote_utils.h>
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
 * Allocate the next available block
 *
 * @param blockno returns the allocated block number
 * @param return 0 on success, 1 if there are no blocks available
 */
static int alloc_block(uint64_t *blockno)
{
    if (get_ramdisk_server()->free_blocks == NULL)
    {
        return 1;
    }

    *blockno = get_ramdisk_server()->free_blocks->blockno;

    RAMDISK_PRINTF("Allocating blockno %ld\n", *blockno);

    // Update free block list
    get_ramdisk_server()->free_blocks->blockno++;
    get_ramdisk_server()->free_blocks->n_blocks--;

    if (get_ramdisk_server()->free_blocks->n_blocks <= 0)
    {
        // This node has no blocks left, move to the next
        ramdisk_block_node_t *old_node = get_ramdisk_server()->free_blocks;
        get_ramdisk_server()->free_blocks = old_node->next;
        free(old_node);
    }

    return 0;
}

/**
 * Mark a block as free
 */
static void free_block(unsigned int blockno)
{
    // Find a place in the linked list to record this free block
    // (XXX) Arya: Not terribly efficient
    ramdisk_block_node_t *prev = NULL;
    for (ramdisk_block_node_t *curr = get_ramdisk_server()->free_blocks; curr != NULL; curr = curr->next)
    {
        if (curr->blockno == blockno + 1)
        {
            // Insert this block at the beginning of the current node
            curr->blockno = blockno;
            curr->n_blocks++;

            // Check if we can coalesce with previous node
            if (prev && (prev->blockno + prev->n_blocks) == blockno)
            {
                prev->n_blocks += curr->n_blocks;
                prev->next = curr->next;
                free(curr);
            }

            return;
        }
        else if (curr->blockno + curr->n_blocks == blockno)
        {
            // Insert this block at the end of the current node
            curr->n_blocks++;

            // Check if we can coalesce with the next node
            if (curr->next && curr->next->blockno == blockno + 1)
            {
                curr->n_blocks += curr->next->n_blocks;
                curr->next = curr->next->next;
                free(curr->next);
            }

            return;
        }
        else if (curr->blockno > blockno)
        {
            // Should insert a new node for this blockno
            ramdisk_block_node_t *new_node = malloc(sizeof(ramdisk_block_node_t));
            new_node->blockno = blockno;
            new_node->n_blocks = 1;
            new_node->next = curr;

            if (prev == NULL)
            {
                get_ramdisk_server()->free_blocks = new_node;
            }
            else
            {
                prev->next = new_node;
            }

            return;
        }
    }

    // If we get here, then we need to insert at the end of the list
    ramdisk_block_node_t *new_node = malloc(sizeof(ramdisk_block_node_t));
    new_node->blockno = blockno;
    new_node->n_blocks = 1;
    new_node->next = NULL;

    if (prev == NULL)
    {
        get_ramdisk_server()->free_blocks = new_node;
    }
    else
    {
        prev->next = new_node;
    }
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
                              SEL4UTILS_RES_TYPE_GENERIC,
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
        resource_server_create_resource(&server->gen, NULL, i);
    }

    return error;
}

seL4_MessageInfo_t ramdisk_request_handler(seL4_MessageInfo_t tag, seL4_Word sender_badge, seL4_CPtr cap, bool *need_new_recv_cap)
{
    int error;
    void *mo_vaddr;
    *need_new_recv_cap = false;
    unsigned int op = seL4_GetMR(RDMSGREG_FUNC);
    uint64_t obj_id = get_object_id_from_badge(sender_badge);

    seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);
    if (sender_badge == 0)
    { /* Handle Unbadged Request */
        RAMDISK_PRINTF("Got message on unbadged EP\n");

        switch (op)
        {
        case RS_FUNC_GET_RR_REQ:
            uint64_t blockno = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_ID);
            uint64_t pd_id = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_PD_ID);
            uint64_t rd_pd_id = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_RS_PD_ID);

            RAMDISK_PRINTF("Get RR for blockno %d\n", blockno);
            void *mem_vaddr = (void *)seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_VADDR);
            model_state_t *model_state = (model_state_t *)mem_vaddr;
            void *free_mem = mem_vaddr + sizeof(model_state_t);
            size_t free_size = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_SIZE) - sizeof(model_state_t);

            // Initialize the model state
            init_model_state(model_state, free_mem, free_size);

            // (XXX) Arya: A lot of this should be moved to PD component once we have resource spaces implemented

            /* Add the PD nodes */
            gpi_model_node_t *self_pd_node = add_pd_node(model_state, NULL, rd_pd_id);
            gpi_model_node_t *client_pd_node = add_pd_node(model_state, NULL, pd_id);

            /* Add the block resource space node */
            gpi_model_node_t *block_space_node = add_resource_space_node(model_state,
                                                                         get_ramdisk_server()->gen.resource_type,
                                                                         get_ramdisk_server()->gen.default_space.id);
            add_edge(model_state, GPI_EDGE_TYPE_HOLD, self_pd_node, block_space_node);

            /* Add the resource node */

            gpi_model_node_t *block_node = add_resource_node(model_state, get_ramdisk_server()->gen.resource_type,
                                                             1, blockno);
            add_edge(model_state, GPI_EDGE_TYPE_HOLD, self_pd_node, block_node);
            add_edge(model_state, GPI_EDGE_TYPE_HOLD, client_pd_node, block_node);
            add_edge(model_state, GPI_EDGE_TYPE_SUBSET, block_node, block_space_node);

            // Add RR from block to MO
            // (XXX) Arya: Actually don't show this, we are pretending these are real blocks?
            // char mo_res_id[CSV_MAX_STRING_SIZE];
            // make_res_id(mo_res_id, GPICAP_TYPE_MO, get_ramdisk_server()->ramdisk_mo->id);
            // add_resource_depends_on_rr(rr_state, block_res_id, mo_res_id, row_ptr);

            clean_model_state(model_state);

            seL4_SetMR(RDMSGREG_FUNC, RS_FUNC_GET_RR_ACK);
            RAMDISK_PRINTF("Returning RR\n");

            break;
        case RS_FUNC_FREE_REQ:
            uint64_t space_id = seL4_GetMR(RSMSGREG_FREE_REQ_SPACE_ID);
            uint64_t obj_id = seL4_GetMR(RSMSGREG_FREE_REQ_OBJ_ID);

            RAMDISK_PRINTF("Free blockno %d\n", obj_id);

            assert(space_id == get_ramdisk_server()->gen.default_space.id);

            free_block(obj_id);

            seL4_SetMR(RDMSGREG_FUNC, RS_FUNC_FREE_ACK);
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
        case RD_FUNC_BIND_REQ:
            *need_new_recv_cap = true;

            RAMDISK_PRINTF("Binding MO for client %ld\n", client_id);

            /* Attach memory object to server ADS */
            error = resource_server_attach_mo(&get_ramdisk_server()->gen, cap, &mo_vaddr);
            CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

            get_ramdisk_server()->shared_mem[client_id] = mo_vaddr;

            // RAMDISK_PRINTF("Can access vaddr %p, val 0x%x\n", mo_vaddr, *((int *)mo_vaddr));

            seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_BIND_ACK);
            break;
        case RD_FUNC_UNBIND_REQ:
            RAMDISK_PRINTF("Unbinding MO for client %ld\n", client_id);

            /* Remove shared mem from server ADS */
            mo_vaddr = get_ramdisk_server()->shared_mem[client_id];

            error = resource_server_unattach(&get_ramdisk_server()->gen, mo_vaddr);
            CHECK_ERROR_GOTO(error, "Failed to unattach MO", error, done);

            // (XXX) Arya: Free the cap as well

            seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_BIND_ACK);
            break;
        case RD_FUNC_CREATE_REQ:
            // Assign a new block to this ep
            uint64_t blockno;
            error = alloc_block(&blockno);

            CHECK_ERROR_GOTO(error, "no more free blocks to assign", RD_SERVER_ERROR_NO_BLOCKS, done);

            // Create the resource endpoint
            seL4_CPtr dest;
            error = resource_server_give_resource(&get_ramdisk_server()->gen,
                                                  get_space_id_from_badge(sender_badge),
                                                  blockno,
                                                  get_client_id_from_badge(sender_badge),
                                                  &dest);
            CHECK_ERROR_GOTO(error, "Failed to give the resource", error, done);

            // Send the reply
            seL4_MessageInfo_ptr_set_length(&reply_tag, RDMSGREG_CREATE_ACK_END);
            seL4_SetMR(RDMSGREG_CREATE_ACK_DEST, dest);
            seL4_SetMR(RDMSGREG_CREATE_ACK_SPACE_ID, get_ramdisk_server()->gen.default_space.id);
            seL4_SetMR(RDMSGREG_CREATE_ACK_RES_ID, blockno);
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
        RAMDISK_PRINTF("Got message on EP with badge: %lx\n", sender_badge);

        gpi_cap_t cap_type = get_cap_type_from_badge(sender_badge);
        CHECK_ERROR_GOTO(cap_type != get_ramdisk_server()->gen.resource_type,
                         "ramdisk server got invalid captype in badged EP",
                         RD_SERVER_ERROR_UNKNOWN, done);
        uint64_t blockno = obj_id;
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
