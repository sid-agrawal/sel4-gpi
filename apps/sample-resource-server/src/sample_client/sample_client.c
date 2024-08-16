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

#include <sample_rpc.pb.h>
#include <sample_client.h>
#include <sample_shared.h>

#define SAMPLE_c "Sample Client: "
#define SAMPLE_SERVER_APP "sample_server"

#if SAMPLE_DEBUG
#define SAMPLE_PRINTF(...)       \
    do                           \
    {                            \
        printf("%s ", SAMPLE_c); \
        printf(__VA_ARGS__);     \
    } while (0);
#else
#define SAMPLE_PRINTF(...)
#endif

#define CHECK_ERROR(check, msg)       \
    do                                \
    {                                 \
        if ((check) != seL4_NoError)  \
        {                             \
            ZF_LOGE(SAMPLE_c "%s: %s" \
                             ", %d.", \
                    __func__,         \
                    msg,              \
                    error);           \
            error = 1;                \
            return error;             \
        }                             \
    } while (0);

static sel4gpi_rpc_env_t rpc_client = {
    .request_desc = &SampleMessage_msg,
    .reply_desc = &SampleReturnMessage_msg,
};

int start_sample_server_proc(pd_client_context_t *sample_server_pd, gpi_space_id_t *sample_space_id)
{
    int error;
    error = start_resource_server_pd(GPICAP_TYPE_NONE, 0, SAMPLE_SERVER_APP,
                                     sample_server_pd, sample_space_id);

    CHECK_ERROR(error, "failed to start sample server\n");
    SAMPLE_PRINTF("Successfully started sample server, resource space ID is %d\n",
                  (int)*sample_space_id);
    return 0;
}

int sample_client_alloc(seL4_CPtr server_ep, sample_client_context_t *ret_conn)
{
    int error;

    SampleMessage request = {
        .magic = SAMPLE_RPC_MAGIC,
        .which_msg = SampleMessage_alloc_tag};

    SampleReturnMessage reply = {0};

    error = sel4gpi_rpc_call(&rpc_client, server_ep, &request, 0, NULL, &reply);

    if (reply.which_msg != SampleReturnMessage_alloc_tag)
    {
        return 1;
    }

    ret_conn->ep = reply.msg.alloc.dest;
    return error || reply.errorCode;
}

int sample_client_free(sample_client_context_t *conn)
{
    int error;

    SampleMessage request = {
        .magic = SAMPLE_RPC_MAGIC,
        .which_msg = SampleMessage_free_tag};

    SampleReturnMessage reply = {0};

    error = sel4gpi_rpc_call(&rpc_client, conn->ep, &request, 0, NULL, &reply);

    return error || reply.errorCode;
}

int sample_client_invoke(sample_client_context_t *conn, uint64_t x, uint64_t y, char *response)
{
    int error;

    SampleMessage request = {
        .magic = SAMPLE_RPC_MAGIC,
        .which_msg = SampleMessage_invoke_tag,
        .msg.invoke = {
            .x = x,
            .y = y,
        }};

    SampleReturnMessage reply = {0};

    error = sel4gpi_rpc_call(&rpc_client, conn->ep, &request, 0, NULL, &reply);
    error |= reply.errorCode;

    if (error == 0) {
        strncpy(response, reply.msg.invoke.z, 40);
    }

    return error;
}

/* INSERT HERE more client api functions */