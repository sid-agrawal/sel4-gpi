#pragma once

#include <autoconf.h>

#include <stdio.h>
#include <stdlib.h>

#include <stdint.h>
#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/resource_server_remote_utils.h>

/**
 * @file A toy server that serves the "pokeball" resource
 */

#define POKEBALL_RESOURCE_TYPE_NAME "POKEBALL"

typedef struct _pokemart_server_context
{
    // Generic resource server context
    resource_server_context_t gen;

    int count; ///< Track the number of pokeballs
} pokemart_server_context_t;

typedef struct _pokeball_client_context
{
    cspacepath_t ep;
    int space_id;
    int id;
} pokeball_client_context_t;

pokemart_server_context_t *get_pokemart_server(void);

/**
 * Called when the pokemart server is started
 */
int pokemart_server_init(void);

/**
 * Called when the pokemart receives a request
 */
seL4_MessageInfo_t pokemart_request_handler(
    seL4_MessageInfo_t tag,
    seL4_Word sender_badge,
    seL4_CPtr cap,
    bool *need_new_recv_cap);

/**
 * Get a pokeball from the pokemart.
 *
 * @param server_ep the pokemart server endpoint
 * @param result location of a pokeball connection structure to fill out
 * @return 0 on success, error otherwise
 */
int pokemart_client_get_pokeball(seL4_CPtr server_ep, pokeball_client_context_t *result);