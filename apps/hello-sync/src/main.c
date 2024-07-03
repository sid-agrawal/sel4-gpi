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
#include <sync/mutex.h>

#include <sel4gpi/pd_utils.h>

/**
 * @file
 * A sample process for testing synchronization
 */

/* dummy global for libsel4muslcsys */
char _cpio_archive[1];
char _cpio_archive_end[1];

/* Initialization for static morecore */
#define APP_MALLOC_SIZE (PAGE_SIZE_4K * 100)
char *morecore_area = (char *)PD_HEAP_LOC;
size_t morecore_size = APP_MALLOC_SIZE;
uintptr_t morecore_base = (uintptr_t)PD_HEAP_LOC;
uintptr_t morecore_top = (uintptr_t)(PD_HEAP_LOC + APP_MALLOC_SIZE);

typedef enum _hello_mode
{
    HELLO_SYNC_1, ///< Basic sync process 1
    HELLO_SYNC_2, ///< Basic sync process 2
} hello_mode_t;

static hello_mode_t mode;

static char *mode_to_str(hello_mode_t mode)
{
    switch (mode)
    {
    case HELLO_SYNC_1:
        return "1";
    case HELLO_SYNC_2:
        return "2";
    default:
        return "unknown";
    }
}

#define N_ITERS 100
#define USE_MX 1

#define PRINTF(msg)                                       \
    do                                                    \
    {                                                     \
        printf("hello-sync %s: " msg, mode_to_str(mode)); \
    } while (0);

#define PRINTF2(msg, ...)                                              \
    do                                                                 \
    {                                                                  \
        printf("hello-sync %s: " msg, mode_to_str(mode), __VA_ARGS__); \
    } while (0);

int critical_loop(seL4_CPtr notif, void *shared_vaddr)
{
    int error = 0;
    seL4_Word notif_badge;

    volatile int *shared_int = (int *)shared_vaddr;

    for (int i = 0; i < N_ITERS; i++)
    {
#if USE_MX
        seL4_Wait(notif, &notif_badge);

        if (!notif_badge)
        {
            PRINTF("Failed to lock mutex\n");
            return error;
        }
#endif

        PRINTF2("Entering critical section, val is %d\n", *shared_int);

        *shared_int = mode;

        for (int j = 0; j < 1000; j++)
        {
            seL4_Yield();

            if (*shared_int != mode)
            {
                PRINTF("Shared value was changed!\n");
                return 1;
            }
        }

        PRINTF("Leaving critical section\n");
#if USE_MX
        seL4_Signal(notif);
#endif
    }

    return error;
}

int main(int argc, char **argv)
{
    int error = 0;

    sel4gpi_set_exit_cb();
    printf("hello-sync main!\n");

    /* parse args */
    assert(argc == 4);
    mode = (seL4_CPtr)atol(argv[0]);
    seL4_CPtr notif_ep = (seL4_CPtr)atol(argv[1]);
    seL4_CPtr parent_ep_resource = (seL4_CPtr)atol(argv[2]);
    void *shared_vaddr = (void *)atol(argv[3]);

    PRINTF2("notif ep (%d), parent ep (%d), mode (%s), shared frame (%p)\n",
            (int)notif_ep,
            (int)parent_ep_resource,
            mode_to_str(mode),
            shared_vaddr);

    ep_client_context_t parent_ep = {.ep = parent_ep_resource};
    error = ep_client_get_raw_endpoint(&parent_ep);
    if (error)
    {
        PRINTF("Failed to retrieve parent EP\n");
        goto main_exit;
    }

    /* run critical loop */
    error = critical_loop(notif_ep, shared_vaddr);

main_exit:

    /* notify parent of test result */
    PRINTF2("Exiting, notifying parent of test result: %d\n", error);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, 0);
    seL4_Send(parent_ep.raw_endpoint, tag);
}