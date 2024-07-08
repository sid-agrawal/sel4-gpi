#include <toy_block_server.h>
#include <sel4gpi/pd_utils.h>
#include <basic_rpc.pb.h>

#define CHECK_ERROR_GOTO(check, msg) \
    do                               \
    {                                \
        if ((check) != seL4_NoError) \
        {                            \
            TOY_BLOCK_SERVER_PRINTF(msg);             \
            error = 1;               \
            goto done;               \
        }                            \
    } while (0);

static toy_block_server_context_t toy_block_server;

static sel4gpi_rpc_env_t rpc_env = {
    .request_desc = &BasicMessage_msg,
    .reply_desc = &BasicReturnMessage_msg,
};

toy_block_server_context_t *get_toy_block_server(void)
{
    return &toy_block_server;
}

int toy_block_server_init(void)
{
    TOY_BLOCK_SERVER_PRINTF("~~ GRAND OPENING OF THE TOY_BLOCK_SERVER ~~\n");

    get_toy_block_server()->count = 0;

    return 0;
}

/**
 * Called when the toy_block receives a request
 */
void toy_block_request_handler(
    void *msg_p,
    void *msg_reply_p,
    seL4_Word sender_badge,
    seL4_CPtr cap,
    bool *need_new_recv_cap)
{
    int error = 0;
    BasicReturnMessage *msg_reply = (BasicReturnMessage *)msg_reply_p;

    // Toy block server does only one thing right now
    TOY_BLOCK_SERVER_PRINTF("Let me look here...\n");

    // Create a toy_block
    get_toy_block_server()->count++;
    int toy_block_id = get_toy_block_server()->count;

    error = resource_server_create_resource(&get_toy_block_server()->gen,
                                            &get_toy_block_server()->gen.default_space,
                                            toy_block_id);
    CHECK_ERROR_GOTO(error, "Failed to give the resource");

    // Give the toy_block
    seL4_CPtr dest;
    error = resource_server_give_resource(&get_toy_block_server()->gen,
                                          get_toy_block_server()->gen.default_space.id,
                                          toy_block_id,
                                          get_client_id_from_badge(sender_badge),
                                          &dest);
    CHECK_ERROR_GOTO(error, "Failed to give the resource");

    TOY_BLOCK_SERVER_PRINTF("... ah, how about this one? Here you go, it's toy_block #%d\n", toy_block_id);

    msg_reply->slot = dest;
    msg_reply->object_id = toy_block_id;
    msg_reply->space_id = get_toy_block_server()->gen.default_space.id;

done:
    msg_reply->errorCode = error;
}

int toy_block_work_handler(
    PdWorkReturnMessage *work)
{
    int error = 0;
    seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

    int op = work->action;
    if (op == PdWorkAction_EXTRACT)
    {
        uint64_t toy_block_pd_id = sel4gpi_get_pd_conn().id;

        TOY_BLOCK_SERVER_PRINTF("Get rr for toy_blocks\n");

        /* Toy blocks never have any resource relations */

        /* Send the result */
        error = resource_server_extraction_no_data(&get_toy_block_server()->gen, work->object_ids_count);
    }
    else
    {
        TOY_BLOCK_SERVER_PRINTF("Unknown work action\n");
        error = 1;
    }

done:
    return error;
}

int toy_block_client_get_toy_block(seL4_CPtr server_ep, toy_block_client_context_t *result)
{
    BasicMessage msg;
    BasicReturnMessage reply_msg;

    int error = sel4gpi_rpc_call(&rpc_env, server_ep, (void *)&msg, 0, NULL, (void *)&reply_msg);
    error |= reply_msg.errorCode;

    if (!error)
    {
        result->ep.capPtr = reply_msg.slot;
        result->space_id = reply_msg.space_id;
        result->id = reply_msg.object_id;
    }

    return error;
}