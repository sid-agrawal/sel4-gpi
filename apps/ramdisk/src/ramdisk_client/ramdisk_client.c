#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vka/capops.h>
#include <vspace/vspace.h>

#include <ramdisk_client.h>
#include <ramdisk_shared.h>

#define RAMDISK_C "RamDisk Client: "

#define CHECK_ERROR(error, msg)        \
    do                                 \
    {                                  \
        if (error != seL4_NoError)     \
        {                              \
            ZF_LOGE(RAMDISK_C "%s: %s" \
                              ", %d.", \
                    __func__,          \
                    msg,               \
                    error);            \
            return error;              \
        }                              \
    } while (0);

static ramdisk_client_context_t ramdisk_client;

int ramdisk_client_sanity_test(seL4_CPtr server_ep_cap,
                               mo_client_context_t *mo,
                               seL4_Word *res)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetMR(RAMDISK_MR_OP, RAMDISK_SANITY_TEST);
    seL4_SetCap(0, mo->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(server_ep_cap, tag);

    int error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "failed ramdisk sanity test\n");

    *res = seL4_GetMR(0);
    return 0;
}

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

uint64_t get_ramdisk_block_size()
{
    return RAMDISK_BLOCK_SIZE;
}