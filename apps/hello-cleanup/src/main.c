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
#include <toy_server.h>
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

static const char *abc_test_str = "this has some data in it, I swear\n";

hello_mode_t mode;
static uint64_t n_client_requests;

static int toy_client(seL4_CPtr server_ep)
{
    int error = 0;

    for (int i = 0; i < n_client_requests; i++)
    {
        PRINTF2("Requesting an item #%d\n", i + 1);
        toy_client_context_t result;
        error = toy_client_get(server_ep, &result);

        if (error == 0)
        {
            PRINTF("Got it, thanks!\n");
        }
        else
        {
            PRINTF("Wait, what? This isn't what I asked for.\n");
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
    int error = 0;

    /* parse args */
    ep_client_context_t parent_ep;
    parent_ep.ep = (seL4_CPtr)atol(argv[0]);
    uint64_t parent_pd_id = (uint64_t)atol(argv[1]);
    mode = (seL4_CPtr)atol(argv[2]);
    if (argc > 3)
    {
        n_client_requests = (uint64_t)atol(argv[3]);
    }

    error = ep_client_get_raw_endpoint(&parent_ep);
    if (error)
    {
        PRINTF("Failed to retrieve parent EP\n");
        goto main_exit;
    }

    switch (mode)
    {
    case HELLO_CLEANUP_TOY_BLOCK_SERVER_MODE:
        error = resource_server_start(
            &get_toy_server()->gen,
            TOY_BLOCK_RESOURCE_TYPE_NAME,
            toy_request_handler,
            toy_work_handler,
            parent_ep.ep,
            parent_pd_id,
            toy_server_init,
            false,
            &BasicMessage_msg,
            &BasicReturnMessage_msg);
        break;
    case HELLO_CLEANUP_TOY_FILE_SERVER_MODE:
        error = resource_server_start(
            &get_toy_server()->gen,
            TOY_FILE_RESOURCE_TYPE_NAME,
            toy_request_handler,
            toy_work_handler,
            parent_ep.ep,
            parent_pd_id,
            toy_server_init,
            false,
            &BasicMessage_msg,
            &BasicReturnMessage_msg);
        break;
    case HELLO_CLEANUP_TOY_DB_SERVER_MODE:
        error = resource_server_start(
            &get_toy_server()->gen,
            TOY_DB_RESOURCE_TYPE_NAME,
            toy_request_handler,
            toy_work_handler,
            parent_ep.ep,
            parent_pd_id,
            toy_server_init,
            false,
            &BasicMessage_msg,
            &BasicReturnMessage_msg);
        break;
    case HELLO_CLEANUP_TOY_BLOCK_CLIENT_MODE:
        seL4_CPtr server_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(TOY_BLOCK_RESOURCE_TYPE_NAME));
        error = toy_client(server_ep);
        break;
    case HELLO_CLEANUP_TOY_FILE_CLIENT_MODE:
        server_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(TOY_FILE_RESOURCE_TYPE_NAME));
        error = toy_client(server_ep);
        break;
    case HELLO_CLEANUP_TOY_DB_CLIENT_MODE:
        server_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(TOY_DB_RESOURCE_TYPE_NAME));
        error = toy_client(server_ep);
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

    /* Notify parent of result */
    PRINTF("Notify parent of test result\n");
    seL4_Send(parent_ep.raw_endpoint, seL4_MessageInfo_new(error, 0, 0, 0));

    while (1)
    {
        // (XXX) Arya: Do not exit, so we can dump the model state
    }

    return 0;
}