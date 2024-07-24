/**
 * @file Entry point to start the fs server in a new process
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <sel4runtime.h>
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

#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_utils.h>
#include <fs_rpc.pb.h>
#include <fs_server.h>

int main(int argc, char **argv)
{
    printf("FS main!\n");

    seL4_CPtr ramdisk_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME));

    printf("FS: RAMDISK EP: %lu\n", (seL4_Word)ramdisk_ep);

    /* parse args */
    assert(argc == 2);
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);
    gpi_obj_id_t parent_pd_id = (gpi_obj_id_t)atol(argv[1]);

    get_xv6fs_server()->rd_ep = ramdisk_ep;

    return resource_server_start(
        &get_xv6fs_server()->gen,
        FILE_RESOURCE_TYPE_NAME,
        xv6fs_request_handler,
        xv6fs_work_handler,
        parent_ep,
        parent_pd_id,
        xv6fs_init,
        FS_DEBUG_ENABLED,
        &FsMessage_msg,
        &FsReturnMessage_msg);
}