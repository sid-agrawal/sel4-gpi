/**
 * @file Runs the kvstore server in its own process
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <sel4gpi/pd_utils.h>

/* dummy global for libsel4muslcsys */
char _cpio_archive[1];
char _cpio_archive_end[1];

/* Initialization for static morecore */
#define APP_MALLOC_SIZE (PAGE_SIZE_4K * 100)
char *morecore_area = (char *) PD_HEAP_LOC;
size_t morecore_size = APP_MALLOC_SIZE;
uintptr_t morecore_base = (uintptr_t) PD_HEAP_LOC;
uintptr_t morecore_top = (uintptr_t) (PD_HEAP_LOC + APP_MALLOC_SIZE);

#include <sel4gpi/pd_clientapi.h>
#include <fs_client.h>
#include <kvstore_shared.h>
#include <kvstore_server.h>

#define CHECK_ERROR(error, msg)    \
    do                             \
    {                              \
        if (error != seL4_NoError) \
        {                          \
            ZF_LOGE("%s %s: %s"    \
                    ", %d.",       \
                    KVSTORE_S,     \
                    __func__,      \
                    msg,           \
                    error);        \
            goto main_exit;        \
        }                          \
    } while (0);

int main(int argc, char **argv)
{
    sel4gpi_set_exit_cb();
    
    printf("kvstore main!\n");

    /* parse args */
    assert(argc == 1);
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);

    return kvstore_server_main(parent_ep);
}