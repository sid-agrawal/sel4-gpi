#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <sel4test/test.h>
#include <sel4test/macros.h>
#include "../test.h"
#include "../helpers.h"

#include <sel4gpi/ads_clientapi.h>

#include <ramdisk_client.h>
#include <fs_client.h>
#include <fs_server.h>

#define TEST_STR_1 "Fuzzy Wuzzy was a bear"
#define TEST_STR_2 "Fuzzy Wuzzy had no hair"
#define TEST_FNAME "somefile"

/**
 * Starts the fs as a thread
 */

int start_fs_thread(env_t env, seL4_CPtr ramdisk_ep, seL4_CPtr *fs_ep)
{

    int error;

    /* create an endpoint for the parent to listen on*/
    vka_object_t ep_object = {0};
    error = vka_alloc_endpoint(&env->vka, &ep_object);
    test_assert(error == 0);

    printf("Starting fs thread\n");

    /* start fs thread */
    error = xv6fs_server_spawn_thread(&env->simple,
                                      &env->vka,
                                      &env->vspace,
                                      env->gpi_endpoint,
                                      ramdisk_ep,
                                      ep_object.cptr,
                                      env->self_ads_cptr,
                                      env->self_pd_cptr,
                                      XV6FS_SERVER_DEFAULT_PRIORITY);

    test_assert(error == 0);

    /* Wait for message from thread */
    cspacepath_t received_cap_path;
    error = vka_cspace_alloc_path(&env->vka, &received_cap_path);
    test_assert(error == 0);

    seL4_SetCapReceivePath(received_cap_path.root,
                           received_cap_path.capPtr,
                           received_cap_path.capDepth);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(ep_object.cptr, NULL);
    test_assert(seL4_MessageInfo_get_extraCaps(tag) == 1);
    test_assert(received_cap_path.capPtr != 0);
    *fs_ep = received_cap_path.capPtr;

    printf("Received ep from fs\n");
    return sel4test_get_result();
}

int test_fs(env_t env)
{
    int error;
    char buf[128];

    printf("------------------STARTING SETUP: %s------------------\n", __func__);

    /* Initialize the ADS */
    ads_client_context_t ads_conn;
    vka_cspace_make_path(&env->vka, env->self_ads_cptr, &ads_conn.badged_server_ep_cspath);
    test_assert(error == 0);

    /* Initialize the PD */
    pd_client_context_t pd_conn;
    vka_cspace_make_path(&env->vka, env->self_pd_cptr, &pd_conn.badged_server_ep_cspath);
    test_assert(error == 0);

    /* Create a memory object for the buffer */
    seL4_CPtr slot;
    vka_cspace_alloc(&env->vka, &slot);

    mo_client_context_t mo_conn;
    error = mo_component_client_connect(env->gpi_endpoint,
                                        slot,
                                        1,
                                        &mo_conn);
    test_assert(error == 0);
    printf("Finished mo_component_client_connect\n");

    /* Start ramdisk server process */
    seL4_CPtr ramdisk_ep;
    error = start_ramdisk_pd(&env->vka, env->gpi_endpoint, &ramdisk_ep);
    test_assert(error == 0);

    /* TODO Start fs server process */
    seL4_CPtr fs_ep;
    start_fs_thread(env, ramdisk_ep, &fs_ep);

    printf("------------------STARTING TESTS: %s------------------\n", __func__);

    // The libc fs ops should go to the xv6fs server
    xv6fs_client_init(&env->vka, fs_ep,
                      env->gpi_endpoint,
                      env->self_ads_cptr,
                      env->self_pd_cptr);

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

    error = close(f);
    test_assert(error == 0);

    // Test unlink

    // (XXX) Arya: Don't support unlink right now

    #if 0
    error = unlink(TEST_FNAME);
    test_assert(error == 0);

    f = open(TEST_FNAME, O_RDONLY);
    test_assert(f == -1); // File should no longer exist
    #endif

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIFS001, "Ensure that the file system is functioning", test_fs, true)
