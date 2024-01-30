
#include <stdio.h>

#include <sel4test/test.h>
#include <sel4test/macros.h>
#include "../test.h"
#include "../helpers.h"

#include <ramdisk/ramdisk.h>

#define TEST_STR_1 "Fuzzy Wuzzy was a bear"
#define TEST_STR_2 "Fuzzy Wuzzy had no hair"

int test_ramdisk(env_t env)
{
    seL4_Error error;
    char* buf = malloc(RAMDISK_BLOCK_SIZE);

    printf("------------------STARTING: %s------------------\n", __func__);

    // Should get failure before initializing client
    error = ramdisk_read(0, buf);
    test_assert(error != seL4_NoError);

    // Initialize the ramdisk client
    ramdisk_client_init(&env->vka, &env->vspace, env->ramdisk_endpoint);

    // Write and read from beginning of disk
    strcpy(buf, TEST_STR_1);
    error = ramdisk_write(0, buf);
    test_assert(error == seL4_NoError);

    memset(buf, 0, RAMDISK_BLOCK_SIZE); // clear the test string from the buffer
    error = ramdisk_read(0, buf);
    test_assert(error == seL4_NoError);
    test_assert(strcmp(buf, TEST_STR_1) == 0);

    // Write and read from offset into disk
    unsigned int sector = 10;
    strcpy(buf, TEST_STR_2);
    error = ramdisk_write(sector, buf);
    test_assert(error == seL4_NoError);

    memset(buf, 0, RAMDISK_BLOCK_SIZE); // clear the test string from the buffer
    error = ramdisk_read(sector, buf);
    test_assert(error == seL4_NoError);
    test_assert(strcmp(buf, TEST_STR_2) == 0);

    // Write/read entire block
    memset(buf, 0x42, RAMDISK_BLOCK_SIZE);

    error = ramdisk_write(0, buf);
    test_assert(error == seL4_NoError);

    memset(buf, 0, RAMDISK_BLOCK_SIZE); // clear the test string from the buffer
    error = ramdisk_read(0, buf);
    test_assert(error == seL4_NoError);
    test_assert(buf[0] == 0x42);
    test_assert(buf[RAMDISK_BLOCK_SIZE - 1] == 0x42);
    
    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIRD001, "Ensure that the ramdisk is functioning", test_ramdisk, true)