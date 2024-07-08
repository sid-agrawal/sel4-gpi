#include <toy_file_server.h>
#include <sel4gpi/pd_utils.h>
#include <basic_rpc.pb.h>

#define CHECK_ERROR_GOTO(check, msg) \
    do                               \
    {                                \
        if ((check) != seL4_NoError) \
        {                            \
            TOY_FILE_SERVER_PRINTF(msg);     \
            error = 1;               \
            goto done;               \
        }                            \
    } while (0);

static toy_file_server_context_t toy_file_server;

static sel4gpi_rpc_env_t rpc_env = {
    .request_desc = &BasicMessage_msg,
    .reply_desc = &BasicReturnMessage_msg,
};

toy_file_server_context_t *get_toy_file_server(void)
{
    return &toy_file_server;
}

int toy_file_server_init(void)
{
    TOY_FILE_SERVER_PRINTF("~~ GRAND OPENING OF THE TOY_FILE_SERVER ~~\n");

    get_toy_file_server()->count = 0;

    /* Map the toy_file space to the toy_block space */
    gpi_cap_t toy_block_cap_type = sel4gpi_get_resource_type_code(TOY_BLOCK_RESOURCE_TYPE_NAME);
    int error = resspc_client_map_space(&get_toy_file_server()->gen.default_space,
                                        sel4gpi_get_default_space_id(toy_block_cap_type));

    return error;
}

/**
 * Called when the toy_file receives a request
 */
void toy_file_request_handler(
    void *msg_p,
    void *msg_reply_p,
    seL4_Word sender_badge,
    seL4_CPtr cap,
    bool *need_new_recv_cap)
{
    int error = 0;
    BasicReturnMessage *reply_msg = (BasicReturnMessage *)msg_reply_p;

    // toy_file does only one thing right now
    TOY_FILE_SERVER_PRINTF("Give me a minute...\n");

    // Create a toy_file
    get_toy_file_server()->count++;
    int toy_file_id = get_toy_file_server()->count;

    error = resource_server_create_resource(&get_toy_file_server()->gen,
                                            &get_toy_file_server()->gen.default_space,
                                            toy_file_id);
    CHECK_ERROR_GOTO(error, "Failed to give the resource");

    // Get a toy_block
    TOY_FILE_SERVER_PRINTF("I need to fetch a toy_block first.\n");
    seL4_CPtr toy_block_server_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(TOY_BLOCK_RESOURCE_TYPE_NAME));

    error = toy_block_client_get_toy_block(toy_block_server_ep, &get_toy_file_server()->toy_blocks[toy_file_id]);
    CHECK_ERROR_GOTO(error, "Failed to get a toy_block\n");
    TOY_FILE_SERVER_PRINTF("Ok, I've got the toy_block #%d.\n", get_toy_file_server()->toy_blocks[toy_file_id].id);

    // Give the toy_file
    seL4_CPtr dest;
    error = resource_server_give_resource(&get_toy_file_server()->gen,
                                          get_toy_file_server()->gen.default_space.id,
                                          toy_file_id,
                                          get_client_id_from_badge(sender_badge),
                                          &dest);
    CHECK_ERROR_GOTO(error, "Failed to give the resource");

    TOY_FILE_SERVER_PRINTF("I've got the perfect toy_file for you, it's #%d!\n", toy_file_id);

    reply_msg->slot = dest;
    reply_msg->object_id = toy_file_id;

done:
    TOY_FILE_SERVER_PRINTF("Returning from request handler\n");
    reply_msg->errorCode = error;
}

int toy_file_work_handler(
    PdWorkReturnMessage *work)
{
    int error = 0;

    int op = work->action;
    if (op == PdWorkAction_EXTRACT)
    {
        uint64_t toy_file_pd_id = sel4gpi_get_pd_conn().id;

        /* Initialize the model state */
        mo_client_context_t mo;
        model_state_t *model_state;
        error = resource_server_extraction_setup(&get_toy_file_server()->gen, 1, &mo, &model_state);
        if (error)
        {
            TOY_FILE_SERVER_PRINTF("Failed to setup model extraction\n");
            return error;
        }

        for (int i = 0; i < work->object_ids_count; i++)
        {
            uint32_t toy_file_id = work->object_ids[i];
            TOY_FILE_SERVER_PRINTF("Get rr for toy_file #%ld\n", toy_file_id);

            if (toy_file_id == BADGE_OBJ_ID_NULL)
            {
                continue;
            }

            /* Add the toy_file -> toy_block map edge*/
            char toy_file_id_str[CSV_MAX_STRING_SIZE];
            char toy_block_id_str[CSV_MAX_STRING_SIZE];

            get_resource_id(make_res_id(get_toy_file_server()->gen.resource_type,
                                        get_toy_file_server()->gen.default_space.id,
                                        toy_file_id),
                            toy_file_id_str);

            get_resource_id(make_res_id(sel4gpi_get_resource_type_code(TOY_BLOCK_RESOURCE_TYPE_NAME),
                                        get_toy_file_server()->toy_blocks[toy_file_id].space_id,
                                        get_toy_file_server()->toy_blocks[toy_file_id].id),
                            toy_block_id_str);

            add_edge_by_id(model_state, GPI_EDGE_TYPE_MAP, toy_file_id_str, toy_block_id_str);
        }

        /* Send the result */
        error = resource_server_extraction_finish(&get_toy_file_server()->gen, &mo, model_state, work->object_ids_count);
        if (error)
        {
            TOY_FILE_SERVER_PRINTF("Failed to finish model extraction\n");
            return error;
        }
    }
    else
    {
        TOY_FILE_SERVER_PRINTF("Unknown work action\n");
        error = 1;
    }

done:
    TOY_FILE_SERVER_PRINTF("Returning from work handler\n");
    return error;
}

int toy_file_client_get_toy_file(seL4_CPtr server_ep, toy_file_client_context_t *result)
{

    BasicMessage msg;
    BasicReturnMessage reply_msg;

    int error = sel4gpi_rpc_call(&rpc_env, server_ep, (void *)&msg, 0, NULL, (void *)&reply_msg);
    error |= reply_msg.errorCode;

    if (!error)
    {
        result->ep.capPtr = reply_msg.slot;
        result->id = reply_msg.object_id;
    }

    return error;
}