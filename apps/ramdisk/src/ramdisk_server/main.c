/**
 * @file Entry point to start the ramdisk server in a new process
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
#define APP_MALLOC_SIZE (PAGE_SIZE_4K * 200)
char *morecore_area = (char *) PD_HEAP_LOC;
size_t morecore_size = APP_MALLOC_SIZE;
uintptr_t morecore_base = (uintptr_t) PD_HEAP_LOC;
uintptr_t morecore_top = (uintptr_t) (PD_HEAP_LOC + APP_MALLOC_SIZE);

#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_utils.h>
#include <ramdisk_rpc.pb.h>
#include <ramdisk_server.h>

int main(int argc, char **argv)
{
    sel4gpi_set_exit_cb();
    printf("Ramdisk main!\n");
    /* parse args */
    assert(argc == 2);
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);
    uint64_t parent_pd_id = (uint64_t)atol(argv[1]);

    return resource_server_start(
        &get_ramdisk_server()->gen,
        BLOCK_RESOURCE_TYPE_NAME,
        ramdisk_request_handler,
        ramdisk_work_handler,
        parent_ep,
        parent_pd_id,
        ramdisk_init,
        RAMDISK_DEBUG,
        &RamdiskMessage_msg,
        &RamdiskReturnMessage_msg);
}