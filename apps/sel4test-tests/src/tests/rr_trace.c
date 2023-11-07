#include <sel4test/test.h>
#include <sel4test/macros.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/debug.h>

#include <sel4utils/thread.h>
#include <sel4gpi/debug.h>
#include "../test.h"
#include "../helpers.h"
#include <stdio.h>

#include<sel4gpi/pd_clientapi.h>
#include <sel4bench/arch/sel4bench.h>

int test_trace_rr(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    sel4bench_init();

}

DEFINE_TEST(GPIPD201, "OSMO: Trave Resource Relations", test_trace_rr, true)