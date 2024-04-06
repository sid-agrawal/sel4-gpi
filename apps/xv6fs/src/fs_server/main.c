/**
 * @file Entry point to start the fs server in a new process
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <sel4runtime.h>
#include <sel4bench/arch/sel4bench.h>

#ifdef FS_EXECUTABLE
/* dummy global for libsel4muslcsys */
char _cpio_archive[1];
char _cpio_archive_end[1];

#define FS_MALLOC_SIZE 2 * 1024 * 1024
char __attribute__((aligned(PAGE_SIZE_4K))) morecore_area[FS_MALLOC_SIZE];
size_t morecore_size = FS_MALLOC_SIZE;
/* Pointer to free space in the morecore area. */
uintptr_t morecore_top = (uintptr_t)&morecore_area[FS_MALLOC_SIZE];
#endif

#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/pd_utils.h>

#include <fs_server.h>

int main(int argc, char **argv)
{
    printf("FS main!\n");
    sel4bench_init();
    seL4_CPtr ramdisk_ep = sel4gpi_get_rde(GPICAP_TYPE_BLOCK);

    printf("FS: RAMDISK EP: %ld\n", (seL4_Word)ramdisk_ep);

    /* parse args */
    assert(argc == 1);
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);

    get_xv6fs_server()->rd_ep = ramdisk_ep;

    return resource_server_start(
        &get_xv6fs_server()->gen,
        GPICAP_TYPE_FILE,
        xv6fs_request_handler,
        parent_ep,
        xv6fs_init);
}