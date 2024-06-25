#include <daycare_server.h>
#include <sel4gpi/pd_utils.h>

#define PRINTF(...)                                           \
    do                                                        \
    {                                                         \
        printf("hello-cleanup daycare-server: " __VA_ARGS__); \
    } while (0);

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

static daycare_server_context_t daycare_server;

daycare_server_context_t *get_daycare_server(void)
{
    return &daycare_server;
}

int daycare_server_init(void)
{
    PRINTF("~~ GRAND OPENING OF THE DAYCARE ~~\n");

    get_daycare_server()->count = 0;

    return 0;
}

/**
 * Called when the daycare receives a request
 */
seL4_MessageInfo_t daycare_request_handler(
    seL4_MessageInfo_t tag,
    seL4_Word sender_badge,
    seL4_CPtr cap,
    bool *need_new_recv_cap)
{
    int error = 0;
    seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

    // daycare does only one thing right now
    PRINTF("Give me a minute...\n");

    // Create a pokemon
    get_daycare_server()->count++;
    int pokemon_id = get_daycare_server()->count;

    error = resource_server_create_resource(&get_daycare_server()->gen,
                                            &get_daycare_server()->gen.default_space,
                                            pokemon_id);
    CHECK_ERROR_GOTO(error, "Failed to give the resource");

    // Get a pokeball
    PRINTF("I need to fetch a pokeball first.\n");
    seL4_CPtr pokemart_server_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(POKEBALL_RESOURCE_TYPE_NAME));

    error = pokemart_client_get_pokeball(pokemart_server_ep, &get_daycare_server()->pokeballs[pokemon_id]);
    CHECK_ERROR_GOTO(error, "Failed to get a pokeball");
    PRINTF("Ok, I've got the pokeball #%d.\n", get_daycare_server()->pokeballs[pokemon_id].id);

    // Give the pokemon
    seL4_CPtr dest;
    error = resource_server_give_resource(&get_daycare_server()->gen,
                                          get_daycare_server()->gen.default_space.id,
                                          pokemon_id,
                                          get_client_id_from_badge(sender_badge),
                                          &dest);
    CHECK_ERROR_GOTO(error, "Failed to give the resource");

    PRINTF("I've got the perfect pokemon for you, it's #%d!\n", pokemon_id);

    seL4_MessageInfo_ptr_set_length(&reply_tag, 2);
    seL4_SetMR(0, dest);
    seL4_SetMR(1, pokemon_id);

done:
    seL4_MessageInfo_ptr_set_label(&reply_tag, error);
    return reply_tag;
}

int daycare_work_handler(
    PdWorkReturnMessage *work)
{
    int error = 0;
    seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

    int op = work->action;
    if (op == PdWorkAction_EXTRACT)
    {
        uint64_t pokemon_id = work->objectId;
        uint64_t daycare_pd_id = sel4gpi_get_pd_conn().id;

        // Allocate an MO for the extraction
        mo_client_context_t mo_conn;
        size_t mem_size = SIZE_BITS_TO_BYTES(MO_PAGE_BITS);
        error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO), 1, &mo_conn);
        assert(error == 0);

        void *mem_vaddr;
        error = resource_server_attach_mo(get_daycare_server(), mo_conn.badged_server_ep_cspath.capPtr, &mem_vaddr);
        assert(error == 0);

        // Initialize model state
        PRINTF("Get rr for pokemon #%ld\n", pokemon_id);
        model_state_t *model_state = (model_state_t *)mem_vaddr;
        void *free_mem = mem_vaddr + sizeof(model_state_t);
        size_t free_size = mem_size - sizeof(model_state_t);
        init_model_state(model_state, free_mem, free_size);

        /* Add the PD node */
        gpi_model_node_t *self_pd_node = add_pd_node(model_state, NULL, daycare_pd_id);
        // gpi_model_node_t *client_pd_node = add_pd_node(model_state, NULL, pd_id);

        /* Add the pokemon resource space node */
        gpi_model_node_t *pokemon_space_node = add_resource_space_node(model_state,
                                                                       get_daycare_server()->gen.resource_type,
                                                                       get_daycare_server()->gen.default_space.id);
        add_edge(model_state, GPI_EDGE_TYPE_HOLD, self_pd_node, pokemon_space_node);

        /* Add the resource node */
        gpi_model_node_t *pokemon_node = add_resource_node(model_state, get_daycare_server()->gen.resource_type,
                                                           get_daycare_server()->gen.default_space.id, pokemon_id);
        add_edge(model_state, GPI_EDGE_TYPE_HOLD, self_pd_node, pokemon_node);
        // add_edge(model_state, GPI_EDGE_TYPE_HOLD, client_pd_node, pokemon_node);
        add_edge(model_state, GPI_EDGE_TYPE_SUBSET, pokemon_node, pokemon_space_node);

        /* Add the pokeball node */
        gpi_model_node_t *pokeball_node = add_resource_node(model_state,
                                                            sel4gpi_get_resource_type_code(POKEBALL_RESOURCE_TYPE_NAME),
                                                            get_daycare_server()->pokeballs[pokemon_id].space_id,
                                                            get_daycare_server()->pokeballs[pokemon_id].id);
        add_edge(model_state, GPI_EDGE_TYPE_HOLD, self_pd_node, pokeball_node);
        add_edge(model_state, GPI_EDGE_TYPE_MAP, pokemon_node, pokeball_node);

        clean_model_state(model_state);
    }
    else
    {
        PRINTF("Unknown work action\n");
        error = 1;
    }

done:
    return error;
}

int daycare_client_get_pokemon(seL4_CPtr server_ep, pokemon_client_context_t *result)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);

    tag = seL4_Call(server_ep, tag);

    int error = seL4_MessageInfo_get_label(tag);
    result->ep.capPtr = seL4_GetMR(0);
    result->id = seL4_GetMR(1);

    return error;
}