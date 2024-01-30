#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <sel4test/test.h>
#include <sel4test/macros.h>
#include "../test.h"
#include "../helpers.h"

#include <xv6fs/xv6fs.h>

#define TEST_STR_1 "Fuzzy Wuzzy was a bear"
#define TEST_STR_2 "Fuzzy Wuzzy had no hair"
#define TEST_FNAME "somefile"

int test_fs(env_t env)
{
    int error;
    char buf[128];

    printf("------------------STARTING: %s------------------\n", __func__);

    // The libc fs ops should go to the xv6fs server
    xv6fs_client_init(&env->vka, &env->vspace, env->xv6fs_endpoint);

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
    for (int i = 0; i < 10; i++) {
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
    error = unlink(TEST_FNAME);
    test_assert(error == 0);

    f = open(TEST_FNAME, O_RDONLY);
    test_assert(f == -1); // File should no longer exist
    
    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIFS001, "Ensure that the file system is functioning", test_fs, true)