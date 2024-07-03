/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <autoconf.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>

#include <sel4gpi/pd_utils.h>
#include <pokemart_server.h>
#include <daycare_server.h>
#include <basic_rpc.pb.h>

/* dummy global for libsel4muslcsys */
char _cpio_archive[1];
char _cpio_archive_end[1];

/* Initialization for static morecore */
#define APP_MALLOC_SIZE (PAGE_SIZE_4K * 100)
char *morecore_area = (char *)PD_HEAP_LOC;
size_t morecore_size = APP_MALLOC_SIZE;
uintptr_t morecore_base = (uintptr_t)PD_HEAP_LOC;
uintptr_t morecore_top = (uintptr_t)(PD_HEAP_LOC + APP_MALLOC_SIZE);

#define N_CLIENT_REQUESTS 100 // Number of requests clients will make from servers

typedef enum _hello_mode
{
    HELLO_CLEANUP_SERVER_POKEMART_MODE, ///< Process will serve pokeballs
    HELLO_CLEANUP_SERVER_DAYCARE_MODE,  ///< Process will serve pokemon
    HELLO_CLEANUP_CLIENT_POKEMART_MODE, ///< Process will request pokeballs
    HELLO_CLEANUP_CLIENT_DAYCARE_MODE,  ///< Process will request pokemon
    HELLO_CLEANUP_NOTHING_MODE,         ///< Process will do nothing
} hello_mode_t;

static hello_mode_t mode;

static char *mode_to_str(hello_mode_t mode)
{
    switch (mode)
    {
    case HELLO_CLEANUP_SERVER_POKEMART_MODE:
        return "pokemart server";
    case HELLO_CLEANUP_CLIENT_POKEMART_MODE:
        return "pokemart client";
    case HELLO_CLEANUP_SERVER_DAYCARE_MODE:
        return "daycare server";
    case HELLO_CLEANUP_CLIENT_DAYCARE_MODE:
        return "daycare client";
    case HELLO_CLEANUP_NOTHING_MODE:
        return "nothing";
    default:
        return "unknown";
    }
}

#define PRINTF(msg)                                          \
    do                                                       \
    {                                                        \
        printf("hello-cleanup %s: " msg, mode_to_str(mode)); \
    } while (0);

#define PRINTF2(msg, ...)                                                 \
    do                                                                    \
    {                                                                     \
        printf("hello-cleanup %s: " msg, mode_to_str(mode), __VA_ARGS__); \
    } while (0);

int pokemart_client(seL4_CPtr server_ep)
{
    int error = 0;

    for (int i = 0; i < N_CLIENT_REQUESTS; i++)
    {
        PRINTF("I want to purchase one pokeball.\n");

        pokeball_client_context_t result;
        error = pokemart_client_get_pokeball(server_ep, &result);

        if (error == 0)
        {
            PRINTF("Yeah! Thanks!\n");
        }
        else
        {
            PRINTF("Wait, what? This isn't a pokeball.\n");
            return error;
        }
    }

    return error;
}

int daycare_client(seL4_CPtr server_ep)
{
    int error = 0;

    for (int i = 0; i < N_CLIENT_REQUESTS; i++)
    {
        PRINTF("I want to adopt a pokemon.\n");

        pokemon_client_context_t result;
        error = daycare_client_get_pokemon(server_ep, &result);

        if (error == 0)
        {
            PRINTF("It's so cute!\n");
        }
        else
        {
            PRINTF("Wait, what? This isn't a pokemon.\n");
            return error;
        }
    }

    return error;
}

int do_nothing(void)
{
    PRINTF("shorts are comfy and easy to wear!\n");

    return 0;
}

int main(int argc, char **argv)
{
    sel4gpi_set_exit_cb();
    printf("hello-cleanup main!\n");

    /* parse args */
    assert(argc == 3);
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);
    uint64_t parent_pd_id = (uint64_t)atol(argv[1]);
    mode = (seL4_CPtr)atol(argv[2]);

    printf("hello-cleanup: parent ep (%d), mode (%d) \n", (int)parent_ep, (int)mode);

    int error = 0;

    switch (mode)
    {
    case HELLO_CLEANUP_SERVER_POKEMART_MODE:
        error = resource_server_start(
            &get_pokemart_server()->gen,
            POKEBALL_RESOURCE_TYPE_NAME,
            pokemart_request_handler,
            pokemart_work_handler,
            parent_ep,
            parent_pd_id,
            pokemart_server_init,
            true,
            &BasicMessage_msg,
            &BasicReturnMessage_msg);
        break;
    case HELLO_CLEANUP_SERVER_DAYCARE_MODE:
        error = resource_server_start(
            &get_daycare_server()->gen,
            POKEMON_RESOURCE_TYPE_NAME,
            daycare_request_handler,
            daycare_work_handler,
            parent_ep,
            parent_pd_id,
            daycare_server_init,
            true,
            &BasicMessage_msg,
            &BasicReturnMessage_msg);
        break;
    case HELLO_CLEANUP_CLIENT_POKEMART_MODE:
        seL4_CPtr server_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(POKEBALL_RESOURCE_TYPE_NAME));
        error = pokemart_client(server_ep);
        break;
    case HELLO_CLEANUP_CLIENT_DAYCARE_MODE:
        server_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(POKEMON_RESOURCE_TYPE_NAME));
        error = daycare_client(server_ep);
        break;
    case HELLO_CLEANUP_NOTHING_MODE:
        error = do_nothing();
        break;
    default:
        error = 1;
        PRINTF2("Invalid mode %d\n", mode);
    }

main_exit:

    if (error)
    {
        PRINTF("Something is wrong in pokeworld\n");
    }

    while (1)
    {
        // (XXX) Arya: Do not exit, so we can dump the model state
    }

    return 0;
}