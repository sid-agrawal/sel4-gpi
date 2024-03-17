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

#include <kvstore_client.h>

/* dummy global for libsel4muslcsys */
char _cpio_archive[1];
char _cpio_archive_end[1];

/* Pointer to free space in the morecore area. */
#define APP_MALLOC_SIZE (PAGE_SIZE_4K * 100)
char __attribute__((aligned(PAGE_SIZE_4K))) morecore_area[APP_MALLOC_SIZE];
size_t morecore_size = APP_MALLOC_SIZE;
static uintptr_t morecore_base = (uintptr_t)&morecore_area;
uintptr_t morecore_top = (uintptr_t)&morecore_area[APP_MALLOC_SIZE];

#define CHECK_ERROR(check, msg)                \
    do                                         \
    {                                          \
        if ((check) != seL4_NoError)           \
        {                                      \
            printf("hello-kvstore: %s, %d.\n", \
                   msg,                        \
                   error);                     \
            goto main_exit;                    \
        }                                      \
    } while (0);

int main(int argc, char **argv)
{
    printf("hello-kvstore main!\n");
    int error;
    seL4_MessageInfo_t tag;

    /* parse args */
    assert(argc == 2);
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);
    seL4_CPtr kvstore_ep = (seL4_CPtr)atol(argv[1]);

    printf("hello-kvstore: parent ep (%d), kvstore ep (%d)\n", (int)parent_ep, (int)kvstore_ep);

    /* initialize */
    bool use_remote_server = kvstore_ep != 0;
    error = kvstore_client_configure(use_remote_server, kvstore_ep);
    CHECK_ERROR(error, "Failed to initialize kvstore client");

    /* run tests */
    uint64_t key, val, val_ret;

    // Set and get one value
    key = 100;
    val = 42;
    error = kvstore_client_set(key, val);
    CHECK_ERROR(error, "Failed to set a value");

    error = kvstore_client_get(key, &val_ret);
    CHECK_ERROR(error, "Failed to get a value");
    CHECK_ERROR(val != val_ret, "Get value is different from set");

    // Overwrite a value
    val = 555;
    error = kvstore_client_set(key, val);
    CHECK_ERROR(error, "Failed to set a value");

    error = kvstore_client_get(key, &val_ret);
    CHECK_ERROR(error, "Failed to get a value");
    CHECK_ERROR(val != val_ret, "Get value is different from set");

    // Get for an invalid key
    error = kvstore_client_get(1111, &val_ret);
    CHECK_ERROR(error != KVSTORE_ERROR_KEY, "Should have returned KVSTORE_ERROR_KEY for invalid key\n");

    // Set and get many values
    for (int i = 0; i < 20; i++)
    {
        key = 1000 + i;
        val = 5000 + i;
        error = kvstore_client_set(key, val);
        CHECK_ERROR(error, "Failed to set a value");

        error = kvstore_client_get(key, &val_ret);
        CHECK_ERROR(error, "Failed to get a value");
        CHECK_ERROR(val != val_ret, "Get value is different from set");
    }

main_exit:
    /* notify parent of test result */
    printf("hello-kvstore: Exiting, notifying parent of test result: %d\n", error);
    tag = seL4_MessageInfo_new(error, 0, 0, 0);
    seL4_Send(parent_ep, tag);

    return 0;
}