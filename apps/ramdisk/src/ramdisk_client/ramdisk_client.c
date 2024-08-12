#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vspace/vspace.h>

#include <sel4rpc/client.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_utils.h>
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

static sel4gpi_rpc_env_t rpc_client = {
    .request_desc = &RamdiskMessage_msg,
    .reply_desc = &RamdiskReturnMessage_msg,
};

int start_ramdisk_pd(pd_client_context_t *ramdisk_pd,
                     gpi_obj_id_t *ramdisk_id)
{
    int error;
    error = start_resource_server_pd(GPICAP_TYPE_NONE, 0, RAMDISK_APP,
                                     ramdisk_pd, ramdisk_id);
    CHECK_ERROR(error, "failed to start ramdisk server\n");
    RAMDISK_PRINTF("Successfully started ramdisk server, resource space ID is %d\n",
                   (int)*ramdisk_id);
    return 0;
}

int ramdisk_client_bind(seL4_CPtr server_ep_cap,
                        mo_client_context_t *mo)
{
    int error;

    RamdiskMessage request = {
        .magic = RD_RPC_MAGIC,
        .op = RamdiskAction_BIND};

    RamdiskReturnMessage reply = {0};

    error = sel4gpi_rpc_call(&rpc_client, server_ep_cap, &request, 1, &mo->ep, &reply);

    return error || reply.errorCode;
}

int ramdisk_client_unbind(seL4_CPtr server_ep_cap)
{
    int error;

    RamdiskMessage request = {
        .magic = RD_RPC_MAGIC,
        .op = RamdiskAction_UNBIND};

    RamdiskReturnMessage reply = {0};

    error = sel4gpi_rpc_call(&rpc_client, server_ep_cap, &request, 0, NULL, &reply);

    return error || reply.errorCode;
}

int ramdisk_client_alloc_block(seL4_CPtr server_ep_cap,
                               ramdisk_client_context_t *ret_conn)
{
    int error;

    RamdiskMessage request = {
        .magic = RD_RPC_MAGIC,
        .op = RamdiskAction_ALLOC};

    RamdiskReturnMessage reply = {0};

    error = sel4gpi_rpc_call(&rpc_client, server_ep_cap, &request, 0, NULL, &reply);

    if (reply.which_msg != RamdiskReturnMessage_alloc_tag)
    {
        return 1;
    }

    ret_conn->ep = reply.msg.alloc.slot;
    ret_conn->space_id = reply.msg.alloc.space_id;
    ret_conn->res_id = reply.msg.alloc.block_id;

    return error || reply.errorCode;
}

int ramdisk_client_free_block(ramdisk_client_context_t *conn)
{
    int error;

    RamdiskMessage request = {
        .magic = RD_RPC_MAGIC,
        .op = RamdiskAction_FREE};

    RamdiskReturnMessage reply = {0};

    error = sel4gpi_rpc_call(&rpc_client, conn->ep, &request, 0, NULL, &reply);

    return error || reply.errorCode;
}

int ramdisk_client_read(ramdisk_client_context_t *conn)
{
    int error;

    RamdiskMessage request = {
        .magic = RD_RPC_MAGIC,
        .op = RamdiskAction_READ};

    RamdiskReturnMessage reply = {0};

    error = sel4gpi_rpc_call(&rpc_client, conn->ep, &request, 0, NULL, &reply);

    return error || reply.errorCode;
}

int ramdisk_client_write(ramdisk_client_context_t *conn)
{
    int error;

    RamdiskMessage request = {
        .magic = RD_RPC_MAGIC,
        .op = RamdiskAction_WRITE};

    RamdiskReturnMessage reply = {0};

    error = sel4gpi_rpc_call(&rpc_client, conn->ep, &request, 0, NULL, &reply);

    return error || reply.errorCode;
}

uint64_t get_ramdisk_block_size()
{
    return RAMDISK_BLOCK_SIZE;
}