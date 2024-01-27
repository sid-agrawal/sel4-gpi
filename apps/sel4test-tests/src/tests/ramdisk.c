
#include <sel4test/test.h>
#include <sel4test/macros.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/debug.h>

#include <sel4utils/thread.h>
#include <sel4gpi/debug.h>
#include "../test.h"
#include "../helpers.h"
#include <stdio.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4bench/arch/sel4bench.h>

int test_ramdisk(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    // Check that the ramdisk is running
    seL4_CPtr ramdisk_ep = env->ramdisk_endpoint;
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Call(ramdisk_ep, tag);

    assert(seL4_GetMR(0) == 42);
    
    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIRD001, "OSMO: Ensure that the ramdisk is functioning", test_ramdisk, true)