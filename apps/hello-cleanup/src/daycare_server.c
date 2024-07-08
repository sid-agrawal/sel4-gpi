#include <daycare_server.h>
#include <sel4gpi/pd_utils.h>
#include <basic_rpc.pb.h>

#define CHECK_ERROR_GOTO(check, msg) \
    do                               \
    {                                \
        if ((check) != seL4_NoError) \
        {                            \
            DAYCARE_PRINTF(msg);     \
            error = 1;               \
            goto done;               \
        }                            \
    } while (0);

static daycare_server_context_t daycare_server;

static sel4gpi_rpc_env_t rpc_env = {
    .request_desc = &BasicMessage_msg,
    .reply_desc = &BasicReturnMessage_msg,
};

daycare_server_context_t *get_daycare_server(void)
{
    return &daycare_server;
}

int daycare_server_init(void)
{
    DAYCARE_PRINTF("~~ GRAND OPENING OF THE DAYCARE ~~\n");

    get_daycare_server()->count = 0;

    /* Map the pokemon space to the pokeball space */
    gpi_cap_t pokeball_cap_type = sel4gpi_get_resource_type_code(POKEBALL_RESOURCE_TYPE_NAME);
    int error = resspc_client_map_space(&get_daycare_server()->gen.default_space,
                                        sel4gpi_get_default_space_id(pokeball_cap_type));

    return error;
}

/**
 * Called when the daycare receives a request
 */
void daycare_request_handler(
    void *msg_p,
    void *msg_reply_p,
    seL4_Word sender_badge,
    seL4_CPtr cap,
    bool *need_new_recv_cap)
{
    int error = 0;
    BasicReturnMessage *reply_msg = (BasicReturnMessage *)msg_reply_p;

    // daycare does only one thing right now
    DAYCARE_PRINTF("Give me a minute...\n");

    // Create a pokemon
    get_daycare_server()->count++;
    int pokemon_id = get_daycare_server()->count;

    error = resource_server_create_resource(&get_daycare_server()->gen,
                                            &get_daycare_server()->gen.default_space,
                                            pokemon_id);
    CHECK_ERROR_GOTO(error, "Failed to give the resource");

    // Get a pokeball
    DAYCARE_PRINTF("I need to fetch a pokeball first.\n");
    seL4_CPtr pokemart_server_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(POKEBALL_RESOURCE_TYPE_NAME));

    error = pokemart_client_get_pokeball(pokemart_server_ep, &get_daycare_server()->pokeballs[pokemon_id]);
    CHECK_ERROR_GOTO(error, "Failed to get a pokeball\n");
    DAYCARE_PRINTF("Ok, I've got the pokeball #%d.\n", get_daycare_server()->pokeballs[pokemon_id].id);

    // Give the pokemon
    seL4_CPtr dest;
    error = resource_server_give_resource(&get_daycare_server()->gen,
                                          get_daycare_server()->gen.default_space.id,
                                          pokemon_id,
                                          get_client_id_from_badge(sender_badge),
                                          &dest);
    CHECK_ERROR_GOTO(error, "Failed to give the resource");

    DAYCARE_PRINTF("I've got the perfect pokemon for you, it's #%d!\n", pokemon_id);

    reply_msg->slot = dest;
    reply_msg->object_id = pokemon_id;

done:
    DAYCARE_PRINTF("Returning from request handler\n");
    reply_msg->errorCode = error;
}

int daycare_work_handler(
    PdWorkReturnMessage *work)
{
    int error = 0;

    int op = work->action;
    if (op == PdWorkAction_EXTRACT)
    {
        uint64_t daycare_pd_id = sel4gpi_get_pd_conn().id;

        /* Initialize the model state */
        mo_client_context_t mo;
        model_state_t *model_state;
        error = resource_server_extraction_setup(&get_daycare_server()->gen, 1, &mo, &model_state);
        if (error)
        {
            DAYCARE_PRINTF("Failed to setup model extraction\n");
            return error;
        }

        for (int i = 0; i < work->object_ids_count; i++)
        {
            uint32_t pokemon_id = work->object_ids[i];
            DAYCARE_PRINTF("Get rr for pokemon #%ld\n", pokemon_id);

            if (pokemon_id == BADGE_OBJ_ID_NULL)
            {
                continue;
            }

            /* Add the pokemon -> pokeball map edge*/
            char pokemon_id_str[CSV_MAX_STRING_SIZE];
            char pokeball_id_str[CSV_MAX_STRING_SIZE];

            get_resource_id(make_res_id(get_daycare_server()->gen.resource_type,
                                        get_daycare_server()->gen.default_space.id,
                                        pokemon_id),
                            pokemon_id_str);

            get_resource_id(make_res_id(sel4gpi_get_resource_type_code(POKEBALL_RESOURCE_TYPE_NAME),
                                        get_daycare_server()->pokeballs[pokemon_id].space_id,
                                        get_daycare_server()->pokeballs[pokemon_id].id),
                            pokeball_id_str);

            add_edge_by_id(model_state, GPI_EDGE_TYPE_MAP, pokemon_id_str, pokeball_id_str);
        }

        /* Send the result */
        error = resource_server_extraction_finish(&get_daycare_server()->gen, &mo, model_state, work->object_ids_count);
        if (error)
        {
            DAYCARE_PRINTF("Failed to finish model extraction\n");
            return error;
        }
    }
    else
    {
        DAYCARE_PRINTF("Unknown work action\n");
        error = 1;
    }

done:
    DAYCARE_PRINTF("Returning from work handler\n");
    return error;
}

int daycare_client_get_pokemon(seL4_CPtr server_ep, pokemon_client_context_t *result)
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