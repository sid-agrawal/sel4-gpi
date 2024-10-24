#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <vka/capops.h>
#include <sel4test/test.h>
#include <sel4test/macros.h>
#include "../test.h"
#include "../helpers.h"
#include "test_shared.h"

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/pd_utils.h>

#include <ramdisk_client.h>
#include <fs_client.h>

#define TEST_STR_1 "Fuzzy Wuzzy was a bear"
#define TEST_STR_2 "Fuzzy Wuzzy had no hair"
#define TEST_STR_3 "I don't know the next line"
#define TEST_FNAME "somefile"
#define TEST_FNAME_2 "longfile"
#define TEST_FNAME_3 "somefile2"
#define RR_MO_N_PAGES 2

int test_fs(env_t env)
{
    int error;
    char buf[128];

    printf("------------------STARTING SETUP: %s------------------\n", __func__);

    /* Initialize the ADS */
    seL4_CPtr vmr_rde = sel4gpi_get_bound_vmr_rde();

    /* Initialize the PD */
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();

    /* Create a memory object for the RR dump */
    mo_client_context_t mo_conn;
    error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO),
                                        RR_MO_N_PAGES,
                                        MO_PAGE_BITS,
                                        &mo_conn);
    test_assert(error == 0);
    printf("Finished mo_component_client_connect\n");

    /* Start ramdisk server process */
    gpi_space_id_t ramdisk_id;
    pd_client_context_t ramdisk_pd;
    error = start_ramdisk_pd(&ramdisk_pd, &ramdisk_id);
    test_assert(error == 0);

    /* Start fs server process */
    gpi_space_id_t fs_id;
    pd_client_context_t fs_pd;
    error = start_xv6fs_pd(ramdisk_id, &fs_pd, &fs_id);
    test_assert(error == 0);

    // Get the FS ep
    seL4_CPtr fs_client_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME));
    test_assert(fs_client_ep != seL4_CapNull);

    printf("------------------STARTING TESTS: %s------------------\n", __func__);

    /* Attach MO to test's ADS */
    void *mo_vaddr;
    error = vmr_client_attach_no_reserve(vmr_rde,
                                         NULL,
                                         &mo_conn,
                                         SEL4UTILS_RES_TYPE_GENERIC,
                                         &mo_vaddr);
    test_assert(error == 0);

    // The libc fs ops should go to the xv6fs server
    xv6fs_client_init();

    // Test file open/write
    int f = open(TEST_FNAME, O_CREAT | O_RDWR);
    test_assert(f > 0);

    int nbytes = write(f, TEST_STR_1, strlen(TEST_STR_1) + 1);
    test_assert(nbytes == strlen(TEST_STR_1) + 1);

    // Test sending a file resource
    seL4_CPtr file_cap;
    error = xv6fs_client_get_file(f, &file_cap);
    test_assert(error == 0);

    error = pd_client_send_cap(&fs_pd, file_cap, NULL);
    test_assert(error == 0);

    // Test file close
    error = close(f);
    test_assert(error == 0);

    nbytes = write(f, TEST_STR_1, strlen(TEST_STR_1) + 1);
    test_assert(nbytes == -1); // write should fail on closed file

    // Test file open/read
    f = open(TEST_FNAME, O_RDONLY);
    test_assert(f > 0);

    nbytes = read(f, buf, strlen(TEST_STR_1) + 1);
    test_assert(nbytes == strlen(TEST_STR_1) + 1);
    test_assert(strcmp(buf, TEST_STR_1) == 0);

    error = close(f);
    test_assert(error == 0);

    // Test creating multiple files
    for (int i = 0; i < 10; i++)
    {
        char fname[16];
        sprintf(fname, "%s-%d", TEST_FNAME, i);
        f = open(fname, O_CREAT | O_RDWR);
        test_assert(f > 0);

        nbytes = write(f, TEST_STR_1, strlen(TEST_STR_1) + 1);
        test_assert(nbytes == strlen(TEST_STR_1) + 1);

        error = close(f);
        test_assert(error == 0);
    }

    // Test lseek
    f = open(TEST_FNAME, O_RDONLY);
    test_assert(f > 0);

    int offset = 5;
    nbytes = lseek(f, offset, 0);
    test_assert(nbytes == offset);

    nbytes = read(f, buf, strlen(TEST_STR_1) + 1);
    test_assert(nbytes == strlen(TEST_STR_1) + 1 - offset);
    test_assert(strcmp(buf, TEST_STR_1 + offset) == 0);

    // Test pread
    offset = 10;
    nbytes = pread(f, buf, strlen(TEST_STR_1) + 1, offset);
    test_assert(nbytes == strlen(TEST_STR_1) + 1 - offset);
    test_assert(strcmp(buf, TEST_STR_1 + offset) == 0);

    error = close(f);
    test_assert(error == 0);

    // Test stat
    struct stat test_stat;
    error = stat(TEST_FNAME, &test_stat);
    test_assert(error == 0);
    test_assert(test_stat.st_size == strlen(TEST_STR_1) + 1);

    // Test getcwd
    char cwd[14];
    getcwd(cwd, 14);
    test_assert(strcmp(cwd, ROOT_DIR) == 0);

    // Test multiple FD for same file
    int f1 = open(TEST_FNAME, O_RDWR);
    int f2 = open(TEST_FNAME, O_RDWR);

    test_assert(f1 > 0);
    test_assert(f2 > 0);

    nbytes = write(f1, TEST_STR_2, strlen(TEST_STR_2) + 1);
    test_assert(nbytes == strlen(TEST_STR_2) + 1);

    nbytes = read(f2, buf, strlen(TEST_STR_2) + 1);
    test_assert(nbytes == strlen(TEST_STR_2) + 1);
    test_assert(strcmp(buf, TEST_STR_2) == 0);

    error = close(f1);
    test_assert(error == 0);
    error = close(f2);
    test_assert(error == 0);

    // Test unlink
    error = unlink(TEST_FNAME);
    test_assert(error == 0);

    f = open(TEST_FNAME, O_RDONLY);
    test_assert(f == -1); // File should no longer exist

    // Write a large file
    int file_n_blocks = 5;
    void *write_buf = malloc(RAMDISK_BLOCK_SIZE);
    f = open(TEST_FNAME_2, O_CREAT | O_RDWR);

    for (int i = 0; i < file_n_blocks; i++)
    {
        write(f, write_buf, RAMDISK_BLOCK_SIZE);
    }
    free(write_buf);

    error = close(f);
    test_assert(error == 0);

    // Create a namespace
    gpi_space_id_t ns_id;
    error = xv6fs_client_new_ns(&ns_id);
    test_assert(error == 0);
    test_assert(ns_id != 0);

    seL4_CPtr fs_client_ep_ns1 =
        sel4gpi_get_rde_by_space_id(ns_id, sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME));
    assert(fs_client_ep_ns1 != seL4_CapNull);

    // Test a file within namespace
    error = xv6fs_client_set_namespace(ns_id);
    test_assert(error == 0);

    f = open(TEST_FNAME, O_CREAT | O_RDWR);
    test_assert(f > 0);

    nbytes = write(f, TEST_STR_3, strlen(TEST_STR_3) + 1);
    test_assert(nbytes == strlen(TEST_STR_3) + 1);

    nbytes = lseek(f, 0, 0);
    test_assert(nbytes == 0);

    nbytes = read(f, buf, strlen(TEST_STR_3) + 1);
    test_assert(nbytes == strlen(TEST_STR_3) + 1);
    test_assert(strcmp(buf, TEST_STR_3) == 0);

    error = close(f);
    test_assert(error == 0);

    // Check the file exists in global NS
    error = xv6fs_client_set_namespace(fs_id);
    test_assert(error == 0);

    char fname[16];
    sprintf(fname, "/ns%u/%s", ns_id, TEST_FNAME);
    f = open(fname, O_RDWR);
    test_assert(f > 0);

    nbytes = read(f, buf, strlen(TEST_STR_3) + 1);
    test_assert(nbytes == strlen(TEST_STR_3) + 1);
    test_assert(strcmp(buf, TEST_STR_3) == 0);

    // Link file in another new NS
    error = xv6fs_client_new_ns(&ns_id);
    test_assert(error == 0);
    test_assert(ns_id != 0);
    error = xv6fs_client_set_namespace(ns_id);
    test_assert(error == 0);

    error = xv6fs_client_get_file(f, &file_cap);
    test_assert(error == 0);

    error = xv6fs_client_link_file(file_cap, TEST_FNAME_3);
    test_assert(error == 0);

    error = close(f);
    test_assert(error == 0);

    f = open(TEST_FNAME_3, O_RDWR);
    test_assert(f > 0);

    nbytes = read(f, buf, strlen(TEST_STR_3) + 1);
    test_assert(nbytes == strlen(TEST_STR_3) + 1);
    test_assert(strcmp(buf, TEST_STR_3) == 0);

    // Test destroying a NS
    seL4_CPtr ns_server_ep = sel4gpi_get_rde_by_space_id(ns_id, sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME));
    error = xv6fs_client_delete_ns(ns_server_ep);
    test_assert(error == 0);

    // Ensure the old NS no longer works
    printf("Note: it is expected to see two 'Attempted to invoke a null cap' messages after this point\n");
    nbytes = write(f, buf, strlen(TEST_STR_3) + 1);
    test_assert(nbytes == -1);

    f = open(TEST_FNAME_3, O_RDWR);
    test_assert(f == -1);

    // The file from the NS should be deleted from the global NS
    error = xv6fs_client_set_namespace(fs_id);
    test_assert(error == 0);

    f = open(TEST_FNAME_3, O_RDWR);
    test_assert(f == -1);

    extract_model(&pd_conn);

    /* Remove RDEs from test process so that it won't be cleaned up by recursive cleanup */
    error = pd_client_remove_rde(&pd_conn, sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME), BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    error = pd_client_remove_rde(&pd_conn, sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME), BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    // Cleanup servers
    test_error_eq(maybe_terminate_pd(&fs_pd), 0);
    test_error_eq(maybe_terminate_pd(&ramdisk_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIFS001, "Ensure that the file system is functioning", test_fs, true)

/**
 * Tests that the currently connected FS is sufficiently functional to
 * write and read a file
 */
static int basic_fs_test()
{
    int error = 0;
    char buf[128];

    // Test file open/write
    int f = open(TEST_FNAME, O_CREAT | O_RDWR);
    test_assert(f > 0);

    int nbytes = write(f, TEST_STR_1, strlen(TEST_STR_1) + 1);
    test_assert(nbytes == strlen(TEST_STR_1) + 1);

    error = close(f);
    test_assert(error == 0);

    // Test file open/read
    f = open(TEST_FNAME, O_RDONLY);
    test_assert(f > 0);

    memset(buf, 0, 128);
    nbytes = read(f, buf, strlen(TEST_STR_1) + 1);
    test_assert(nbytes == strlen(TEST_STR_1) + 1);
    test_assert(strcmp(buf, TEST_STR_1) == 0);

    error = close(f);
    test_assert(error == 0);

    return error;
}

int test_multiple_fs(env_t env)
{
    int error;
    char buf[128];

    printf("------------------STARTING SETUP: %s------------------\n", __func__);

    /* Initialize the ADS */
    seL4_CPtr vmr_rde = sel4gpi_get_bound_vmr_rde();
    
    /* Initialize the PD */
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();

    /* Create a memory object for the RR dump */
    mo_client_context_t mo_conn;
    error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO),
                                        RR_MO_N_PAGES,
                                        MO_PAGE_BITS,
                                        &mo_conn);
    test_assert(error == 0);
    printf("Finished mo_component_client_connect\n");

    /* Start ramdisk server process */
    gpi_space_id_t ramdisk_id;
    pd_client_context_t ramdisk_pd;
    error = start_ramdisk_pd(&ramdisk_pd, &ramdisk_id);
    test_assert(error == 0);

    printf("------------------STARTING TESTS: %s------------------\n", __func__);

    /* Start FS 1 */
    gpi_space_id_t fs_1_id;
    pd_client_context_t fs_1_pd;
    error = start_xv6fs_pd(ramdisk_id, &fs_1_pd, &fs_1_id);
    test_assert(error == 0);

    /* Attach MO to test's ADS */
    void *mo_vaddr;
    error = vmr_client_attach_no_reserve(vmr_rde,
                                         NULL,
                                         &mo_conn,
                                         SEL4UTILS_RES_TYPE_GENERIC,
                                         &mo_vaddr);
    test_assert(error == 0);

    // The libc fs ops should go to the xv6fs server
    xv6fs_client_init();

    // Test FS1 is functional
    error = basic_fs_test();
    test_assert(error == 0);

    /* Start FS 2 */
    gpi_space_id_t fs_2_id;
    pd_client_context_t fs_2_pd;
    error = start_xv6fs_pd(ramdisk_id, &fs_2_pd, &fs_2_id);
    test_assert(error == 0);

    // Swap to using FS2
    xv6fs_client_set_namespace(fs_2_id);

    // Check that we are in a new FS
    int f = open(TEST_FNAME, O_RDWR);
    test_assert(f == -1); // File should not exist in new FS

    // Test FS2 is functional
    error = basic_fs_test();
    test_assert(error == 0);

    /* Remove RDEs from test process so that it won't be cleaned up by recursive cleanup */
    error = pd_client_remove_rde(&pd_conn, sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME), BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    // Destroy an FS and start another one
    // If the FS is configured to use half of the ramdisk, this test checks that blocks are being
    // reclaimed from the destroyed FS
    error = pd_client_terminate(&fs_1_pd);
    test_assert(error == 0);

    /* Start FS 3 */
    gpi_space_id_t fs_3_id;
    pd_client_context_t fs_3_pd;
    error = start_xv6fs_pd(ramdisk_id, &fs_3_pd, &fs_3_id);
    test_assert(error == 0);

    // Swap to using FS3
    xv6fs_client_set_namespace(fs_3_id);

    // Check that we are in a new FS
    f = open(TEST_FNAME, O_RDWR);
    test_assert(f == -1); // File should not exist in new FS

    // Test FS3 is functional
    error = basic_fs_test();
    test_assert(error == 0);

    extract_model(&pd_conn);

    /* Remove RDEs from test process so that it won't be cleaned up by recursive cleanup */
    error = pd_client_remove_rde(&pd_conn, sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME), BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    error = pd_client_remove_rde(&pd_conn, sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME), BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    // Cleanup other servers
    test_error_eq(maybe_terminate_pd(&fs_2_pd), 0);
    test_error_eq(maybe_terminate_pd(&fs_3_pd), 0);
    test_error_eq(maybe_terminate_pd(&ramdisk_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIFS002, "Start multiple file systems", test_multiple_fs, true)