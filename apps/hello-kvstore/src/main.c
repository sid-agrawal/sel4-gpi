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
char *morecore_area = (char *) PD_HEAP_LOC;
size_t morecore_size = APP_MALLOC_SIZE;
uintptr_t morecore_base = (uintptr_t) PD_HEAP_LOC;
uintptr_t morecore_top = (uintptr_t) (PD_HEAP_LOC + APP_MALLOC_SIZE);

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
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);
    seL4_CPtr kvstore_ep = (seL4_CPtr)atol(argv[1]);
    bool separate_ads = (seL4_CPtr)atol(argv[2]);
    seL4_CPtr fs_ep = sel4gpi_get_rde(GPICAP_TYPE_FILE);
    seL4_CPtr mo_ep = sel4gpi_get_rde(GPICAP_TYPE_MO);

    printf("hello-kvstore: parent ep (%d), kvstore ep (%d), separate_ads? %d, fs ep(%d), mo ep(%d) \n", (int)parent_ep, (int)kvstore_ep, separate_ads, (int)fs_ep, (int)mo_ep);

    /* initialize */
    error = xv6fs_client_init();
    CHECK_ERROR(error, "Failed to initialize file system");
    printf("hello-kvstore: Initialized file system client\n");

    /* run kvstore tests */
    bool use_remote_server = kvstore_ep != 0;
    error = kvstore_client_configure(use_remote_server, separate_ads, kvstore_ep);
    CHECK_ERROR(error, "Failed to initialize kvstore client");
    error = kvstore_tests();
    CHECK_ERROR(error, "Failed kvstore tests");
    printf("hello-kvstore: Completed kvstore tests\n");

    /* use the file system a bit */
    error = sqlite_tests();
    CHECK_ERROR(error, "Failed sqlite tests");
    printf("hello-kvstore: Completed sqlite tests\n");

main_exit:
    /* notify parent of test result */
    printf("hello-kvstore: Exiting, notifying parent of test result: %d\n", error);
    tag = seL4_MessageInfo_new(error, 0, 0, 0);
    seL4_Send(parent_ep, tag);

    return 0;
}