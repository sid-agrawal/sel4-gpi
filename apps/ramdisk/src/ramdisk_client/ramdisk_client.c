#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vspace/vspace.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_utils.h>

#include <ramdisk_client.h>
#include <ramdisk_shared.h>

#define RAMDISK_C "RamDisk Client: "
#define RAMDISK_APP "ramdisk_server"

#if RAMDISK_DEBUG
#define RAMDISK_PRINTF(...)       \
    do                            \
    {                             \
        printf("%s ", RAMDISK_C); \
        printf(__VA_ARGS__);      \
    } while (0);
#else
#define RAMDISK_PRINTF(...)
#endif

#define CHECK_ERROR(check, msg)        \
    do                                 \
    {                                  \
        if ((check) != seL4_NoError)   \
        {                              \
            ZF_LOGE(RAMDISK_C "%s: %s" \
                              ", %d.", \
                    __func__,          \
                    msg,               \
                    error);            \
            error = -1;                \
            return error;              \
        }                              \
    } while (0);

static ramdisk_client_context_t ramdisk_client;

int start_ramdisk_pd(seL4_CPtr *ramdisk_pd_cap,
                     uint64_t *ramdisk_id)
{
    int error;
    error = start_resource_server_pd(0, 0, RAMDISK_APP,
                                     ramdisk_pd_cap, ramdisk_id);
    CHECK_ERROR(error, "failed to start ramdisk server\n");
    RAMDISK_PRINTF("Successfully started ramdisk server, resource manager ID is %d\n",
                   (int)*ramdisk_id);
    return 0;
}

int ramdisk_client_sanity_test(seL4_CPtr server_ep_cap,
                               mo_client_context_t *mo,
                               seL4_Word *res)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, RDMSGREG_SANITY_REQ_END);
    seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_SANITY_REQ);
    seL4_SetCap(0, mo->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(server_ep_cap, tag);

    int error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "failed ramdisk sanity test\n");

    *res = seL4_GetMR(RDMSGREG_SANITY_ACK_VAL);
    return 0;
}

int ramdisk_client_alloc_block(seL4_CPtr server_ep_cap,
                               ramdisk_client_context_t *ret_conn)
{
    /* Request a new block from server */
    seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_CREATE_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);

    tag = seL4_Call(server_ep_cap, tag);
    int error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "failed to get block from ramdisk server\n");

    ret_conn->badged_server_ep_cspath.capPtr = seL4_GetMR(RDMSGREG_CREATE_ACK_DEST);
    ret_conn->id = seL4_GetMR(RDMSGREG_CREATE_ACK_ID);
    return 0;
}

int ramdisk_client_read(ramdisk_client_context_t *conn, mo_client_context_t *mo)
{
    seL4_Error error;

    /* Send IPC to ramdisk server */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_READ_REQ);
    seL4_SetCap(0, mo->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    error = seL4_MessageInfo_get_label(tag);

    return error;
}

int ramdisk_client_write(ramdisk_client_context_t *conn, mo_client_context_t *mo)
{
    seL4_Error error;

    /* Send IPC to ramdisk server */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_WRITE_REQ);
    seL4_SetCap(0, mo->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    error = seL4_MessageInfo_get_label(tag);

    return error;
}

uint64_t get_ramdisk_block_size()
{
    return RAMDISK_BLOCK_SIZE;
}