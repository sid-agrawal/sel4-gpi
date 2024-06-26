#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vspace/vspace.h>

#include <sel4rpc/client.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_remote_utils.h>
#include <sel4gpi/gpi_rpc.h>
#include <ramdisk_rpc.pb.h>

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

#define USE_RPC 1

static sel4gpi_rpc_env_t rpc_client = {
    .request_desc = &RamdiskMessage_msg,
    .reply_desc = &RamdiskReturnMessage_msg,
};

int start_ramdisk_pd(seL4_CPtr *ramdisk_pd_cap,
                     uint64_t *ramdisk_id)
{
    int error;
    error = start_resource_server_pd(GPICAP_TYPE_NONE, 0, RAMDISK_APP,
                                     ramdisk_pd_cap, ramdisk_id);
    CHECK_ERROR(error, "failed to start ramdisk server\n");
    RAMDISK_PRINTF("Successfully started ramdisk server, resource space ID is %d\n",
                   (int)*ramdisk_id);
    return 0;
}

int ramdisk_client_bind(seL4_CPtr server_ep_cap,
                        mo_client_context_t *mo)
{
#if USE_RPC
    int error;

    RamdiskMessage request = {
        .op = RamdiskAction_BIND};

    RamdiskReturnMessage reply;

    error = sel4gpi_rpc_call(&rpc_client, server_ep_cap, &request, 1, &mo->badged_server_ep_cspath.capPtr, &reply);

    return error || reply.errorCode;

#else
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_BIND_REQ);
    seL4_SetCap(0, mo->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(server_ep_cap, tag);

    int error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "failed ramdisk bind request\n");
    return 0;
#endif
}

int ramdisk_client_unbind(seL4_CPtr server_ep_cap)
{
#if USE_RPC
    int error;

    RamdiskMessage request = {
        .op = RamdiskAction_UNBIND};

    RamdiskReturnMessage reply;

    error = sel4gpi_rpc_call(&rpc_client, server_ep_cap, &request, 0, NULL, &reply);

    return error || reply.errorCode;

#else
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_UNBIND_REQ);
    tag = seL4_Call(server_ep_cap, tag);

    int error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "failed ramdisk unbind request\n");
    return 0;
#endif
}

int ramdisk_client_alloc_block(seL4_CPtr server_ep_cap,
                               ramdisk_client_context_t *ret_conn)
{
#if USE_RPC
    int error;

    RamdiskMessage request = {
        .op = RamdiskAction_ALLOC};

    RamdiskReturnMessage reply;

    error = sel4gpi_rpc_call(&rpc_client, server_ep_cap, &request, 0, NULL, &reply);

    if (reply.which_msg != RamdiskReturnMessage_alloc_tag)
    {
        return 1;
    }

    ret_conn->badged_server_ep_cspath.capPtr = reply.msg.alloc.slot;
    ret_conn->space_id = reply.msg.alloc.space_id;
    ret_conn->res_id = reply.msg.alloc.block_id;

    return error || reply.errorCode;

#else
    /* Request a new block from server */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_CREATE_REQ);
    tag = seL4_Call(server_ep_cap, tag);
    int error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "failed to get block from ramdisk server\n");

    ret_conn->badged_server_ep_cspath.capPtr = seL4_GetMR(RDMSGREG_CREATE_ACK_DEST);
    ret_conn->space_id = seL4_GetMR(RDMSGREG_CREATE_ACK_SPACE_ID);
    ret_conn->res_id = seL4_GetMR(RDMSGREG_CREATE_ACK_RES_ID);

    return error;
#endif
}

int ramdisk_client_read(ramdisk_client_context_t *conn)
{
#if USE_RPC
    int error;

    RamdiskMessage request = {
        .op = RamdiskAction_READ};

    RamdiskReturnMessage reply;

    error = sel4gpi_rpc_call(&rpc_client, conn->badged_server_ep_cspath.capPtr, &request, 0, NULL, &reply);

    return error || reply.errorCode;

#else
    seL4_Error error;

    /* Send IPC to ramdisk server */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_READ_REQ);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    error = seL4_MessageInfo_get_label(tag);

    return error;
#endif
}

int ramdisk_client_write(ramdisk_client_context_t *conn)
{
#if USE_RPC
    int error;

    RamdiskMessage request = {
        .op = RamdiskAction_WRITE};

    RamdiskReturnMessage reply;

    error = sel4gpi_rpc_call(&rpc_client, conn->badged_server_ep_cspath.capPtr, &request, 0, NULL, &reply);

    return error || reply.errorCode;

#else
    seL4_Error error;

    /* Send IPC to ramdisk server */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(RDMSGREG_FUNC, RD_FUNC_WRITE_REQ);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    error = seL4_MessageInfo_get_label(tag);

    return error;
#endif
}

uint64_t get_ramdisk_block_size()
{
    return RAMDISK_BLOCK_SIZE;
}