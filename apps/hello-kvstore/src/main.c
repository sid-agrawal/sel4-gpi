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
#include <fs_client.h>
#include <kvstore_client.h>
#include <sqlite_test.h>

/* dummy global for libsel4muslcsys */
char _cpio_archive[1];
char _cpio_archive_end[1];

/* Initialization for static morecore */
#define APP_MALLOC_SIZE (PAGE_SIZE_4K * 100)
char *morecore_area = (char *)PD_HEAP_LOC;
size_t morecore_size = APP_MALLOC_SIZE;
uintptr_t morecore_base = (uintptr_t)PD_HEAP_LOC;
uintptr_t morecore_top = (uintptr_t)(PD_HEAP_LOC + APP_MALLOC_SIZE);

// (XXX) Linh: TO BE REMOVED: terrible hack for threads - only one thread can use the fs client at a time
extern global_xv6fs_client_context_t xv6fs_client;

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

int kvstore_tests(void)
{
    sel4gpi_set_exit_cb();

    int error;
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
    return error;
}

int main(int argc, char **argv)
{
    printf("hello-kvstore main!\n");
    int error;
    seL4_MessageInfo_t tag;

    /* parse args */
    ep_client_context_t parent_ep = {.badged_server_ep_cspath.capPtr = (seL4_CPtr)atol(argv[0])};
    error = ep_client_get_raw_endpoint(&parent_ep);
    CHECK_ERROR(error, "Failed to retrieve parent EP\n");

    seL4_CPtr kvstore_ep = (seL4_CPtr)atol(argv[1]);
    kvstore_mode_t mode = (seL4_CPtr)atol(argv[2]);
    seL4_CPtr fs_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME));
    seL4_CPtr mo_ep = sel4gpi_get_rde(GPICAP_TYPE_MO);

    printf("hello-kvstore: parent ep (%d), kvstore ep (%d), mode (%d), fs ep(%d), mo ep(%d) \n",
           (int)parent_ep.raw_endpoint, (int)kvstore_ep, (int)mode, (int)fs_ep, (int)mo_ep);

    /* initialize */
    // (XXX) Linh: TO BE REMOVED, terrible hack so that our separate threads test runs - only one thread can use the fs client at a time
    if (mode != SEPARATE_THREAD)
    {
        error = xv6fs_client_init();
        CHECK_ERROR(error, "Failed to initialize file system");
        printf("hello-kvstore: Initialized file system client\n");
    }

    /* run kvstore tests */
    bool use_remote_server = kvstore_ep != 0;
    error = kvstore_client_configure(mode, kvstore_ep);
    CHECK_ERROR(error, "Failed to initialize kvstore client");
    error = kvstore_tests();
    CHECK_ERROR(error, "Failed kvstore tests");
    printf("hello-kvstore: Completed kvstore tests\n");

    /* use the file system a bit */
    if (mode == SEPARATE_ADS)
    {
        kvstore_client_swap_ads_lib();
    }

    // (XXX) Linh: TO BE REMOVED, terrible hack so that our separate threads test runs - only one thread can use the fs client at a time
    if (mode == SEPARATE_THREAD)
    {
        memset(&xv6fs_client, 0, sizeof(global_xv6fs_client_context_t));
        error = xv6fs_client_init();
        CHECK_ERROR(error, "Failed to initialize file system");
    }
    error = sqlite_tests();
    CHECK_ERROR(error, "Failed sqlite tests");
    printf("hello-kvstore: Completed sqlite tests\n");

    if (mode == SEPARATE_ADS)
    {
        kvstore_client_swap_ads_app();
    }

main_exit:
    /* notify parent of test result */
    printf("hello-kvstore: Exiting, notifying parent of test result: %d\n", error);
    tag = seL4_MessageInfo_new(error, 0, 0, 0);
    seL4_Send(parent_ep.raw_endpoint, tag);

    while(1) {
        // (XXX) Arya: Do not exit, so we can dump the model state
    }

    return 0;
}