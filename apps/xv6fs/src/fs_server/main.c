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

#define FS_MALLOC_SIZE 2 * 1024 * 1024
char __attribute__((aligned(PAGE_SIZE_4K))) morecore_area[FS_MALLOC_SIZE];
size_t morecore_size = FS_MALLOC_SIZE;
/* Pointer to free space in the morecore area. */
uintptr_t morecore_top = (uintptr_t)&morecore_area[FS_MALLOC_SIZE];
#endif

#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>

#include <fs_server.h>

int main(int argc, char **argv)
{
    printf("FS main!\n");

    seL4_CPtr ads_cap = sel4runtime_get_initial_ads_cap();
    seL4_CPtr gpi_cap = sel4runtime_get_gpi_cap();
    seL4_CPtr pd_cap = sel4runtime_get_pd_cap();

    printf("Hello: ADS_CAP: %ld\n", (seL4_Word)ads_cap);
    printf("Hello: GPI_CAP: %ld\n", (seL4_Word)gpi_cap);
    printf("Hello: PD_CAP: %ld\n", (seL4_Word)pd_cap);

    ads_client_context_t ads_conn;
    ads_conn.badged_server_ep_cspath.capPtr = ads_cap;

    seL4_CPtr slot;
    pd_client_context_t pd_conn;
    pd_conn.badged_server_ep_cspath.capPtr = pd_cap;

    /* parse args */
    assert(argc == 1);
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);

    // (XXX) Arya: Temporary hack to get ramdisk ep
    // replace with RDE mechanism
    seL4_CPtr rd_ep = parent_ep - 1;

    return xv6fs_server_start(&ads_conn,
                              &pd_conn,
                              gpi_cap,
                              rd_ep,
                              parent_ep);
}