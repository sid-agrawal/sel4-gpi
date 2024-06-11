#include <pokemart_server.h>
#include <sel4gpi/pd_utils.h>

#define PRINTF(...)                                   \
    do                                                \
    {                                                 \
        printf("hello-cleanup server: " __VA_ARGS__); \
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

static pokemart_server_context_t pokemart_server;

pokemart_server_context_t *get_pokemart_server(void)
{
    return &pokemart_server;
}

int pokemart_server_init(void)
{
    PRINTF("~~ GRAND OPENING OF THE POKEMART ~~\n");

    get_pokemart_server()->count = 0;

    return 0;
}

/**
 * Called when the pokemart receives a request
 */
seL4_MessageInfo_t pokemart_request_handler(
    seL4_MessageInfo_t tag,
    seL4_Word sender_badge,
    seL4_CPtr cap,
    bool *need_new_recv_cap)
{
    int error = 0;
    seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

    // Pokemart does only one thing right now

    int op = seL4_GetMR(RSMSGREG_FUNC);

    if (op == RS_FUNC_GET_RR_REQ)
    {
        uint64_t pokeball_id = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_ID);
        uint64_t pd_id = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_PD_ID);
        uint64_t pokemart_pd_id = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_RS_PD_ID);

        PRINTF("Get rr for pokeball #%ld\n", pokeball_id);
        void *mem_vaddr = (void *)seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_VADDR);
        model_state_t *model_state = (model_state_t *)mem_vaddr;
        void *free_mem = mem_vaddr + sizeof(model_state_t);
        size_t free_size = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_SIZE) - sizeof(model_state_t);

        // Initialize the model state
        init_model_state(model_state, free_mem, free_size);

        /* Add the PD nodes */
        gpi_model_node_t *self_pd_node = add_pd_node(model_state, NULL, pokemart_pd_id);
        gpi_model_node_t *client_pd_node = add_pd_node(model_state, NULL, pd_id);

        /* Add the pokeball resource space node */
        gpi_model_node_t *pokeball_space_node = add_resource_space_node(model_state,
                                                                        get_pokemart_server()->gen.resource_type,
                                                                        get_pokemart_server()->gen.default_space.id);
        add_edge(model_state, GPI_EDGE_TYPE_HOLD, self_pd_node, pokeball_space_node);

        /* Add the resource node */
        gpi_model_node_t *pokeball_node = add_resource_node(model_state, get_pokemart_server()->gen.resource_type,
                                                         get_pokemart_server()->gen.default_space.id, pokeball_id);
        add_edge(model_state, GPI_EDGE_TYPE_HOLD, self_pd_node, pokeball_node);
        add_edge(model_state, GPI_EDGE_TYPE_HOLD, client_pd_node, pokeball_node);
        add_edge(model_state, GPI_EDGE_TYPE_SUBSET, pokeball_node, pokeball_space_node);

        clean_model_state(model_state);

        seL4_SetMR(RSMSGREG_FUNC, RS_FUNC_GET_RR_ACK);
    }
    else
    {
        // Pokemart does only one thing right now
        PRINTF("Let me look here...\n");

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

        PRINTF("... ah, how about this one? Here you go, it's pokeball #%d\n", pokeball_id);
    }

done:
    seL4_MessageInfo_ptr_set_label(&reply_tag, error);
    return reply_tag;
}

int pokemart_client_get_pokeball(seL4_CPtr server_ep)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);

    tag = seL4_Call(server_ep, tag);

    int error = seL4_MessageInfo_get_label(tag);

    return error;
}