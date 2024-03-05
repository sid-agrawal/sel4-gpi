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
    seL4_CPtr rde_cap = sel4runtime_get_rde_cap();

    printf("Hello: ADS_CAP: %ld\n", (seL4_Word)ads_cap);
    printf("Hello: RDE_CAP: %ld\n", (seL4_Word)rde_cap);

    ads_client_context_t ads_conn;
    ads_conn.badged_server_ep_cspath.capPtr = ads_cap;

    /* Attach the resource directory so we can access it */
    void *rde_vaddr;
    mo_client_context_t rde_mo;
    rde_mo.badged_server_ep_cspath.capPtr = rde_cap;
    int error = ads_client_attach(&ads_conn,
                                  0, /*vaddr*/
                                  &rde_mo,
                                  &rde_vaddr);
    assert(error == 0);
    printf("Attached to vaddr %p\n", rde_vaddr);

    osmosis_rde_t *pd_rde = (osmosis_rde_t *)rde_vaddr;
    seL4_CPtr gpi_cap = pd_rde[GPICAP_TYPE_MO].slot_in_PD;

    seL4_CPtr slot;
    pd_client_context_t pd_conn;
    pd_conn.badged_server_ep_cspath.capPtr = pd_rde[GPICAP_TYPE_PD].slot_in_PD;

    seL4_CPtr ramdisk_ep = pd_rde[GPICAP_TYPE_BLOCK].slot_in_PD;

    printf("Ramdisk: GPI_CAP: %ld\n", (seL4_Word)gpi_cap);
    printf("Ramdisk: PD_CAP: %ld\n", (seL4_Word)pd_conn.badged_server_ep_cspath.capPtr);
    printf("Ramdisk: RAMDISK_CAP: %ld\n", (seL4_Word)ramdisk_ep);

    /* parse args */
    assert(argc == 1);
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);

    get_xv6fs_server()->rd_ep = ramdisk_ep;

    return resource_server_start(
        &get_xv6fs_server()->gen,
        GPICAP_TYPE_FILE,
        xv6fs_request_handler,
        &ads_conn,
        &pd_conn,
        gpi_cap,
        parent_ep,
        xv6fs_init);
}