/**
 * @file Entry point to start the sample server in a new process
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
#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_utils.h>

#include <sample_rpc.pb.h>
#include <sample_server.h>

int main(int argc, char **argv)
{
    printf("Sample server main!\n");
    /* parse args */
    assert(argc == 2);
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);
    gpi_obj_id_t parent_pd_id = (gpi_obj_id_t)atol(argv[1]);

    return resource_server_start(
        &get_sample_server()->gen,
        SAMPLE_RESOURCE_TYPE_NAME,
        sample_request_handler,
        sample_work_handler,
        parent_ep,
        parent_pd_id,
        sample_init,
        SAMPLE_DEBUG,
        &SampleMessage_msg,
        &SampleReturnMessage_msg);
}