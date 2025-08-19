/**
 * @file Entry point to start the sddf blk server in a new process
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


#define SDDF_BLK_RESOURCE_TYPE_NAME "SDDF_BLK"

#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_utils.h>
#include <sddf_blk_rpc.pb.h>
#include <sddf_blk_server.h>

int main(int argc, char **argv)
{
    printf("SDDF-BLK main!\n");
    /* parse args */
    assert(argc == 2);
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);
    gpi_obj_id_t parent_pd_id = (gpi_obj_id_t)atol(argv[1]);

    return resource_server_start(
        &get_sddf_blk_server()->gen,
        SDDF_BLK_RESOURCE_TYPE_NAME,
        sddf_blk_request_handler,
        sddf_blk_work_handler,
        parent_ep,
        parent_pd_id,
        sddf_blk_init,
        0,
        &SDDFBLKMessage_msg,
        &SDDFBLKReturnMessage_msg);
}