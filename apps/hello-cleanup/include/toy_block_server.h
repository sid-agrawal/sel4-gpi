#pragma once

#include <autoconf.h>

#include <stdio.h>
#include <stdlib.h>

#include <stdint.h>
#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/resource_server_utils.h>

/**
 * @file A toy server that serves the "toy_block" resource
 */

#define TOY_BLOCK_RESOURCE_TYPE_NAME "TOY_BLOCK"

#define TOY_BLOCK_SERVER_PRINTF(...)                                   \
    do                                                         \
    {                                                          \
        printf("hello-cleanup toy_block-server: " __VA_ARGS__); \
    } while (0);

typedef struct _toy_block_server_context
{
    // Generic resource server context
    resource_server_context_t gen;

    int count; ///< Track the number of toy_blocks
} toy_block_server_context_t;

typedef struct _toy_block_client_context
{
    cspacepath_t ep;
    int space_id;
    int id;
} toy_block_client_context_t;

toy_block_server_context_t *get_toy_block_server(void);

/**
 * Called when the toy_block server is started
 */
int toy_block_server_init(void);

/**
 * Called when the toy_block receives a request
 */
void toy_block_request_handler(
    void *msg_p,
    void *msg_reply_p,
    seL4_Word sender_badge,
    seL4_CPtr cap,
    bool *need_new_recv_cap);

/**
 * To handle root task requests to the toy_block server
 */
int toy_block_work_handler(PdWorkReturnMessage *work);

/**
 * Get a toy_block from the toy_block.
 *
 * @param server_ep the toy_block server endpoint
 * @param result location of a toy_block connection structure to fill out
 * @return 0 on success, error otherwise
 */
int toy_block_client_get_toy_block(seL4_CPtr server_ep, toy_block_client_context_t *result);