#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <vka/capops.h>
#include <sel4test/test.h>
#include <sel4test/macros.h>
#include "../test.h"
#include "../helpers.h"

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_utils.h>

#include <ramdisk_client.h>
#include <fs_client.h>

#define TEST_STR_1 "Fuzzy Wuzzy was a bear"
#define TEST_STR_2 "Fuzzy Wuzzy had no hair"
#define TEST_FNAME "somefile"
#define TEST_FNAME_2 "longfile"
#define RR_MO_N_PAGES 2

int test_fs(env_t env)
{
    int error;
    char buf[128];

    printf("------------------STARTING SETUP: %s------------------\n", __func__);

    /* Initialize the ADS */
    ads_client_context_t ads_conn;
    vka_cspace_make_path(&env->vka, sel4gpi_get_ads_cap(), &ads_conn.badged_server_ep_cspath);

    /* Initialize the PD */
    pd_client_context_t pd_conn;
    vka_cspace_make_path(&env->vka, sel4gpi_get_pd_cap(), &pd_conn.badged_server_ep_cspath);

    /* Create a memory object for the RR dump */
    seL4_CPtr slot;
    vka_cspace_alloc(&env->vka, &slot);

    mo_client_context_t mo_conn;
    error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO),
                                        slot,
                                        RR_MO_N_PAGES,
                                        &mo_conn);
    test_assert(error == 0);
    printf("Finished mo_component_client_connect\n");

    /* Start ramdisk server process */
    uint64_t ramdisk_id;
    seL4_CPtr ramdisk_pd_cap;
    error = start_ramdisk_pd(&ramdisk_pd_cap, &ramdisk_id);
    test_assert(error == 0);

    /* Start fs server process */
    uint64_t fs_id;
    seL4_CPtr fs_pd_cap;
    error = start_xv6fs_pd(ramdisk_id, ramdisk_pd_cap, &fs_pd_cap, &fs_id);
    test_assert(error == 0);

    // Add FS ep to RDE
    error = pd_client_add_rde(&pd_conn, fs_pd_cap, fs_id);
    test_assert(error == 0);
    seL4_CPtr fs_client_ep = sel4gpi_get_rde(GPICAP_TYPE_FILE);

    printf("------------------STARTING TESTS: %s------------------\n", __func__);

    /* Attach MO to test's ADS */
    void *mo_vaddr;
    error = ads_client_attach(&ads_conn,
                              NULL,
                              &mo_conn,
                              &mo_vaddr);
    test_assert(error == 0);

    // The libc fs ops should go to the xv6fs server
    xv6fs_client_init();

    // Test file open/write
    int f = open(TEST_FNAME, O_CREAT | O_RDWR);
    test_assert(f > 0);

    int nbytes = write(f, TEST_STR_1, strlen(TEST_STR_1) + 1);
    test_assert(nbytes == strlen(TEST_STR_1) + 1);

    error = close(f);
    test_assert(error == 0);

    // Test file close
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

    /* Dump RR for a file */
    model_state_t *model_state = malloc(sizeof(model_state_t));
    init_model_state(model_state);
    rr_state_t *file_rr_state;

    // Write a large file
    int file_n_blocks = 5;
    void *write_buf = malloc(RAMDISK_BLOCK_SIZE);
    f = open(TEST_FNAME_2, O_CREAT | O_RDWR);

    for (int i = 0; i < file_n_blocks; i++)
    {
        write(f, write_buf, RAMDISK_BLOCK_SIZE);
    }
    free(write_buf);

    // Print whole-pd model state
    error = pd_client_dump(&pd_conn, NULL, 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIFS001, "Ensure that the file system is functioning", test_fs, true)
