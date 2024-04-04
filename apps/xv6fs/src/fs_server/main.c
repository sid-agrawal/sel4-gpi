/**
 * @file Entry point to start the fs server in a new process
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <sel4runtime.h>

#ifdef FS_EXECUTABLE
/* dummy global for libsel4muslcsys */
char _cpio_archive[1];
char _cpio_archive_end[1];

/* Pointer to free space in the morecore area. */
#define APP_MALLOC_SIZE (PAGE_SIZE_4K * 100)
char __attribute__((aligned(PAGE_SIZE_4K))) morecore_area[APP_MALLOC_SIZE];
size_t morecore_size = APP_MALLOC_SIZE;
static uintptr_t morecore_base = (uintptr_t)&morecore_area;
uintptr_t morecore_top = (uintptr_t)&morecore_area[APP_MALLOC_SIZE];
#endif

#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/pd_utils.h>

#include <fs_server.h>

int main(int argc, char **argv)
{
    printf("FS main!\n");

    seL4_CPtr ramdisk_ep = sel4gpi_get_rde(GPICAP_TYPE_BLOCK);

    printf("FS: RAMDISK EP: %ld\n", (seL4_Word)ramdisk_ep);

    /* parse args */
    assert(argc == 1);
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);

    get_xv6fs_server()->rd_ep = ramdisk_ep;

    return resource_server_start(
        &get_xv6fs_server()->gen,
        xv6fs_request_handler,
        parent_ep,
        xv6fs_init);
}