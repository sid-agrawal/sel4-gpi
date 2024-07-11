#pragma once

#include <autoconf.h>

#include <stdio.h>
#include <stdlib.h>

#include <stdint.h>
#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/gpi_rpc.h>
#include <basic_rpc.pb.h>

/**
 * @file A toy server that serves the "toy_file" resource
 * Toy file resources map to "toy_block" resources
 */

#define TOY_DB_RESOURCE_TYPE_NAME "TOY_DB"
#define TOY_FILE_RESOURCE_TYPE_NAME "TOY_FILE"
#define TOY_BLOCK_RESOURCE_TYPE_NAME "TOY_BLOCK"
#define MAX_TOY_OBJECT 16

typedef enum _hello_mode
{
    HELLO_CLEANUP_TOY_BLOCK_SERVER_MODE, ///< Process will serve toy_blocks
    HELLO_CLEANUP_TOY_FILE_SERVER_MODE,  ///< Process will serve toy_file
    HELLO_CLEANUP_TOY_DB_SERVER_MODE,    ///< Process will serve toy_db
    HELLO_CLEANUP_TOY_BLOCK_CLIENT_MODE, ///< Process will request toy_blocks
    HELLO_CLEANUP_TOY_FILE_CLIENT_MODE,  ///< Process will request toy_file
    HELLO_CLEANUP_TOY_DB_CLIENT_MODE,    ///< Process will request toy_db
    HELLO_CLEANUP_NOTHING_MODE,          ///< Process will do nothing
} hello_mode_t;

typedef struct _toy_client_context
{
    cspacepath_t ep;
    int space_id;
    int id;
} toy_client_context_t;

typedef struct _toy_server_context
{
    // Generic resource server context
    resource_server_context_t gen;

    int count;                                     ///< Track the number of toy objects
    gpi_cap_t maps_type;                           ///< Type of resource that these toy objects mapt o
    toy_client_context_t toy_maps[MAX_TOY_OBJECT]; ///< Track the toy objects' mapped-to resource
} toy_server_context_t;

toy_server_context_t *get_toy_server(void);

static char *mode_to_str(hello_mode_t mode)
{
    switch (mode)
    {
    case HELLO_CLEANUP_TOY_BLOCK_SERVER_MODE:
        return "toy_block server";
    case HELLO_CLEANUP_TOY_BLOCK_CLIENT_MODE:
        return "toy_block client";
    case HELLO_CLEANUP_TOY_FILE_SERVER_MODE:
        return "toy_file server";
    case HELLO_CLEANUP_TOY_FILE_CLIENT_MODE:
        return "toy_file client";
    case HELLO_CLEANUP_TOY_DB_SERVER_MODE:
        return "toy_db server";
    case HELLO_CLEANUP_TOY_DB_CLIENT_MODE:
        return "toy_db client";
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

/**
 * Called when the toy server is started
 */
int toy_server_init(void);

/**
 * Called when the toy_file receives a request
 */
void toy_request_handler(
    void *msg_p,
    void *msg_reply_p,
    seL4_Word sender_badge,
    seL4_CPtr cap,
    bool *need_new_recv_cap);

/**
 * To handle root task requests to the toy_block server
 */
int toy_work_handler(PdWorkReturnMessage *work);

/**
 * Get a toy object from the toy server
 *
 * @param server_ep the toy server endpoint
 * @param result location of a toy connection structure to fill out
 * @return 0 on success, error otherwise
 */
int toy_client_get(seL4_CPtr server_ep, toy_client_context_t *result);