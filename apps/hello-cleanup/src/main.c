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
#include <toy_block_server.h>
#include <toy_file_server.h>
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

#define N_CLIENT_REQUESTS 10 // Number of requests clients will make from servers

static const char* abc_test_str = "this has some data in it, I swear\n";

typedef enum _hello_mode
{
    HELLO_CLEANUP_TOY_BLOCK_SERVER_MODE, ///< Process will serve toy_blocks
    HELLO_CLEANUP_TOY_FILE_SERVER_MODE,  ///< Process will serve toy_file
    HELLO_CLEANUP_TOY_BLOCK_CLIENT_MODE, ///< Process will request toy_blocks
    HELLO_CLEANUP_TOY_FILE_CLIENT_MODE,  ///< Process will request toy_file
    HELLO_CLEANUP_NOTHING_MODE,         ///< Process will do nothing
} hello_mode_t;

static hello_mode_t mode;

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

int toy_block_client(seL4_CPtr server_ep)
{
    int error = 0;

    for (int i = 0; i < N_CLIENT_REQUESTS; i++)
    {
        PRINTF("I want to purchase one toy_block.\n");

        toy_block_client_context_t result;
        error = toy_block_client_get_toy_block(server_ep, &result);

        if (error == 0)
        {
            PRINTF("Yeah! Thanks!\n");
        }
        else
        {
            PRINTF("Wait, what? This isn't a toy_block.\n");
            return error;
        }
    }

    return error;
}

int toy_file_client(seL4_CPtr server_ep)
{
    int error = 0;

    for (int i = 0; i < N_CLIENT_REQUESTS; i++)
    {
        PRINTF("I want to adopt a toy_file.\n");

        toy_file_client_context_t result;
        error = toy_file_client_get_toy_file(server_ep, &result);

        if (error == 0)
        {
            PRINTF("It's so cute!\n");
        }
        else
        {
            PRINTF("Wait, what? This isn't a toy_file.\n");
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
    case HELLO_CLEANUP_TOY_BLOCK_SERVER_MODE:
        error = resource_server_start(
            &get_toy_block_server()->gen,
            TOY_BLOCK_RESOURCE_TYPE_NAME,
            toy_block_request_handler,
            toy_block_work_handler,
            parent_ep,
            parent_pd_id,
            toy_block_server_init,
            true,
            &BasicMessage_msg,
            &BasicReturnMessage_msg);
        break;
    case HELLO_CLEANUP_TOY_FILE_SERVER_MODE:
        error = resource_server_start(
            &get_toy_file_server()->gen,
            TOY_FILE_RESOURCE_TYPE_NAME,
            toy_file_request_handler,
            toy_file_work_handler,
            parent_ep,
            parent_pd_id,
            toy_file_server_init,
            true,
            &BasicMessage_msg,
            &BasicReturnMessage_msg);
        break;
    case HELLO_CLEANUP_TOY_BLOCK_CLIENT_MODE:
        seL4_CPtr server_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(TOY_BLOCK_RESOURCE_TYPE_NAME));
        error = toy_block_client(server_ep);
        break;
    case HELLO_CLEANUP_TOY_FILE_CLIENT_MODE:
        server_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(TOY_FILE_RESOURCE_TYPE_NAME));
        error = toy_file_client(server_ep);
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
        PRINTF("Something is wrong in toy world\n");
    }

    while (1)
    {
        // (XXX) Arya: Do not exit, so we can dump the model state
    }

    return 0;
}