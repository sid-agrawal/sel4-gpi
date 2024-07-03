#include <pokemart_server.h>
#include <sel4gpi/pd_utils.h>
#include <basic_rpc.pb.h>

#define CHECK_ERROR_GOTO(check, msg) \
    do                               \
    {                                \
        if ((check) != seL4_NoError) \
        {                            \
            POKEMART_PRINTF(msg);             \
            error = 1;               \
            goto done;               \
        }                            \
    } while (0);

static pokemart_server_context_t pokemart_server;

static sel4gpi_rpc_env_t rpc_env = {
    .request_desc = &BasicMessage_msg,
    .reply_desc = &BasicReturnMessage_msg,
};

pokemart_server_context_t *get_pokemart_server(void)
{
    return &pokemart_server;
}

int pokemart_server_init(void)
{
    POKEMART_PRINTF("~~ GRAND OPENING OF THE POKEMART ~~\n");

    get_pokemart_server()->count = 0;

    return 0;
}

/**
 * Called when the pokemart receives a request
 */
void pokemart_request_handler(
    void *msg_p,
    void *msg_reply_p,
    seL4_Word sender_badge,
    seL4_CPtr cap,
    bool *need_new_recv_cap)
{
    int error = 0;
    BasicReturnMessage *msg_reply = (BasicReturnMessage *)msg_reply_p;

    // Pokemart does only one thing right now
    POKEMART_PRINTF("Let me look here...\n");

    // Create a pokeball
    get_pokemart_server()->count++;
    int pokeball_id = get_pokemart_server()->count;

    error = resource_server_create_resource(&get_pokemart_server()->gen,
                                            &get_pokemart_server()->gen.default_space,
                                            pokeball_id);
    CHECK_ERROR_GOTO(error, "Failed to give the resource");

    // Give the pokeball
    seL4_CPtr dest;
    error = resource_server_give_resource(&get_pokemart_server()->gen,
                                          get_pokemart_server()->gen.default_space.id,
                                          pokeball_id,
                                          get_client_id_from_badge(sender_badge),
                                          &dest);
    CHECK_ERROR_GOTO(error, "Failed to give the resource");

    POKEMART_PRINTF("... ah, how about this one? Here you go, it's pokeball #%d\n", pokeball_id);

    msg_reply->slot = dest;
    msg_reply->object_id = pokeball_id;
    msg_reply->space_id = get_pokemart_server()->gen.default_space.id;

done:
    msg_reply->errorCode = error;
}

int pokemart_work_handler(
    PdWorkReturnMessage *work)
{
    int error = 0;
    seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

    int op = work->action;
    if (op == PdWorkAction_EXTRACT)
    {
        uint64_t pokeball_id = work->object_id;
        uint64_t pokemart_pd_id = sel4gpi_get_pd_conn().id;

        POKEMART_PRINTF("Get rr for pokeball #%ld\n", pokeball_id);

        /* Pokeballs never have any resource relations */

        /* Send the result */
        error = resource_server_extraction_no_data(&get_pokemart_server()->gen);
    }
    else
    {
        POKEMART_PRINTF("Unknown work action\n");
        error = 1;
    }

done:
    return error;
}

int pokemart_client_get_pokeball(seL4_CPtr server_ep, pokeball_client_context_t *result)
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