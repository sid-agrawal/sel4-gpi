#include <toy_server.h>
#include <sel4gpi/pd_utils.h>
#include <basic_rpc.pb.h>

#define CHECK_ERROR_GOTO(check, msg) \
    do                               \
    {                                \
        if ((check) != seL4_NoError) \
        {                            \
            PRINTF(msg);             \
            error = 1;               \
            goto done;               \
        }                            \
    } while (0);

extern hello_mode_t mode;
static toy_server_context_t toy_server;

static sel4gpi_rpc_env_t rpc_env = {
    .request_desc = &BasicMessage_msg,
    .reply_desc = &BasicReturnMessage_msg,
};

toy_server_context_t *get_toy_server(void)
{
    return &toy_server;
}

int toy_server_init(void)
{
    int error = 0;

    PRINTF("~~ GRAND OPENING OF THE SERVER ~~\n");

    get_toy_server()->count = 0;

    /* Map the toy space if necessary */
    if (mode == HELLO_CLEANUP_TOY_FILE_SERVER_MODE)
    {
        get_toy_server()->maps_type = sel4gpi_get_resource_type_code(TOY_BLOCK_RESOURCE_TYPE_NAME);
    }
    else if (mode == HELLO_CLEANUP_TOY_DB_SERVER_MODE)
    {
        get_toy_server()->maps_type = sel4gpi_get_resource_type_code(TOY_FILE_RESOURCE_TYPE_NAME);
    }
    else
    {
        get_toy_server()->maps_type = GPICAP_TYPE_NONE;
    }

    if (get_toy_server()->maps_type != GPICAP_TYPE_NONE)
    {
        error = resspc_client_map_space(&get_toy_server()->gen.default_space,
                                        sel4gpi_get_default_space_id(get_toy_server()->maps_type));
    }

    return error;
}

/**
 * Called when the toy_file receives a request
 */
void toy_request_handler(
    void *msg_p,
    void *msg_reply_p,
    seL4_Word sender_badge,
    seL4_CPtr cap,
    bool *need_new_recv_cap)
{
    int error = 0;
    BasicReturnMessage *reply_msg = (BasicReturnMessage *)msg_reply_p;

    // Create a toy object
    get_toy_server()->count++;
    int toy_id = get_toy_server()->count;

    error = resource_server_create_resource(&get_toy_server()->gen,
                                            &get_toy_server()->gen.default_space,
                                            toy_id);
    CHECK_ERROR_GOTO(error, "Failed to give the resource");

    if (get_toy_server()->maps_type != GPICAP_TYPE_NONE)
    {
        seL4_CPtr map_server_ep = sel4gpi_get_rde(get_toy_server()->maps_type);

        error = toy_client_get(map_server_ep, &get_toy_server()->toy_maps[toy_id]);
        CHECK_ERROR_GOTO(error, "Failed to get a toy object to map to\n");
    }

    // Give the toy object
    seL4_CPtr dest;
    error = resource_server_give_resource(&get_toy_server()->gen,
                                          get_toy_server()->gen.default_space.id,
                                          toy_id,
                                          get_client_id_from_badge(sender_badge),
                                          &dest);
    CHECK_ERROR_GOTO(error, "Failed to give the resource");

    PRINTF2("I've got the perfect toy for you, it's #%d!\n", toy_id);

    reply_msg->slot = dest;
    reply_msg->space_id = get_toy_server()->gen.default_space.id;
    reply_msg->object_id = toy_id;

done:
    PRINTF("Returning from request handler\n");
    reply_msg->errorCode = error;
}

int toy_work_handler(
    PdWorkReturnMessage *work)
{
    int error = 0;

    int op = work->action;
    if (op == PdWorkAction_EXTRACT)
    {
        if (get_toy_server()->maps_type == GPICAP_TYPE_NONE)
        {
            /* Send the empty esult */
            error = resource_server_extraction_no_data(&get_toy_server()->gen, work->object_ids_count);
        }
        else
        {
            /* Calculate the amount of memory needed */
            size_t size_needed = work->object_ids_count * 2 * sizeof(gpi_model_state_component_t);
            uint16_t pages_needed = DIV_ROUND_UP(size_needed, SIZE_BITS_TO_BYTES(MO_PAGE_BITS));

            /* Initialize the model state */
            mo_client_context_t mo;
            model_state_t *model_state;
            error = resource_server_extraction_setup(&get_toy_server()->gen, pages_needed, &mo, &model_state);
            if (error)
            {
                PRINTF("Failed to setup model extraction\n");
                return error;
            }

            for (int i = 0; i < work->object_ids_count; i++)
            {
                gpi_obj_id_t toy_id = work->object_ids[i];
                PRINTF2("Get rr for toy #%d\n", toy_id);

                if (toy_id == BADGE_OBJ_ID_NULL)
                {
                    continue;
                }

                /* Add the toy -> toy_maps map edge*/
                char toy_id_str[CSV_MAX_STRING_SIZE];
                char toy_map_id_str[CSV_MAX_STRING_SIZE];

                get_resource_id(make_res_id(get_toy_server()->gen.resource_type,
                                            get_toy_server()->gen.default_space.id,
                                            toy_id),
                                toy_id_str);

                get_resource_id(make_res_id(get_toy_server()->maps_type,
                                            get_toy_server()->toy_maps[toy_id].space_id,
                                            get_toy_server()->toy_maps[toy_id].id),
                                toy_map_id_str);

                add_edge_by_id(model_state, GPI_EDGE_TYPE_MAP, toy_id_str, toy_map_id_str);
            }

            /* Send the result */
            error = resource_server_extraction_finish(&get_toy_server()->gen, &mo, model_state, work->object_ids_count);
            if (error)
            {
                PRINTF("Failed to finish model extraction\n");
                return error;
            }
        }
    }
    else if (op == PdWorkAction_FREE)
    {
        // Toy server does nothing
        error = pd_client_finish_work(&get_toy_server()->gen.pd_conn, work);
    }
    else if (op == PdWorkAction_DESTROY)
    {
        // Toy server does nothing
        error = pd_client_finish_work(&get_toy_server()->gen.pd_conn, work);
    }
    else
    {
        PRINTF("Unknown work action\n");
        error = 1;
    }

done:
    PRINTF("Returning from work handler\n");
    return error;
}

int toy_client_get(seL4_CPtr server_ep, toy_client_context_t *result)
{

    BasicMessage msg;
    BasicReturnMessage reply_msg;

    int error = sel4gpi_rpc_call(&rpc_env, server_ep, (void *)&msg, 0, NULL, (void *)&reply_msg);
    error |= reply_msg.errorCode;

    if (!error)
    {
        result->ep.capPtr = reply_msg.slot;
        result->id = reply_msg.object_id;
        result->space_id = reply_msg.space_id;
    }

    return error;
}