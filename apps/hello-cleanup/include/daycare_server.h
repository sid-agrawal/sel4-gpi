#pragma once

#include <autoconf.h>

#include <stdio.h>
#include <stdlib.h>

#include <stdint.h>
#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/resource_server_remote_utils.h>

#include <pokemart_server.h>

/**
 * @file A toy server that serves the "pokemon" resource
 * Pokemon resources map to "pokeball" resources
 */

#define POKEMON_RESOURCE_TYPE_NAME "POKEMON"
#define MAX_POKEMON 16

typedef struct _daycare_server_context
{
    // Generic resource server context
    resource_server_context_t gen;

    int count;                                        ///< Track the number of pokemon
    pokeball_client_context_t pokeballs[MAX_POKEMON]; ///< Map the pokemon ID to its pokeball
} daycare_server_context_t;

typedef struct _pokemon_client_context
{
    cspacepath_t ep;
    int id;
} pokemon_client_context_t;

daycare_server_context_t *get_daycare_server(void);

/**
 * Called when the daycare server is started
 */
int daycare_server_init(void);

/**
 * Called when the daycare receives a request
 */
seL4_MessageInfo_t daycare_request_handler(
    seL4_MessageInfo_t tag,
    seL4_Word sender_badge,
    seL4_CPtr cap,
    bool *need_new_recv_cap);

/**
 * To handle root task requests to the pokemart server
 */
int daycare_work_handler(PdWorkReturnMessage *work);

/**
 * Get a pokemon from the daycare.
 *
 * @param server_ep the daycare server endpoint
 * @param result location of a pokemon connection structure to fill out
 * @return 0 on success, error otherwise
 */
int daycare_client_get_pokemon(seL4_CPtr server_ep, pokemon_client_context_t *result);