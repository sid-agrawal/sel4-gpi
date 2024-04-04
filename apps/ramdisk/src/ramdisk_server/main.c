/**
 * @file Entry point to start the ramdisk server in a new process
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>

#ifdef RAMDISK_EXECUTABLE
/* dummy global for libsel4muslcsys */
char _cpio_archive[1];
char _cpio_archive_end[1];

#define RD_MALLOC_SIZE 2 * 1024 * 1024
char __attribute__((aligned(PAGE_SIZE_4K))) morecore_area[RD_MALLOC_SIZE];
size_t morecore_size = RD_MALLOC_SIZE;
/* Pointer to free space in the morecore area. */
uintptr_t morecore_top = (uintptr_t)&morecore_area[RD_MALLOC_SIZE];
#endif

#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_utils.h>

#include <ramdisk_server.h>

int main(int argc, char **argv)
{
    printf("Ramdisk main!\n");

    /* parse args */
    assert(argc == 1);
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);

    return resource_server_start(
        &get_ramdisk_server()->gen,
        ramdisk_request_handler,
        parent_ep,
        ramdisk_init);
}