/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <autoconf.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <arch_stdio.h>
#include <allocman/vka.h>
#include <allocman/bootstrap.h>

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4platsupport/timer.h>

#include <sel4utils/util.h>
#include <sel4utils/mapping.h>
#include <sel4utils/vspace.h>

#include <sel4test/test.h>

#include <vka/capops.h>

#include "helpers.h"
#include "test.h"
#include "init.h"
#include <rpc.pb.h>

//int *shared_addr_between_threads;

/* dummy global for libsel4muslcsys */
char _cpio_archive[1];
char _cpio_archive_end[1];

/* endpoint to call back to the test driver on */
static seL4_CPtr endpoint;
static seL4_CPtr gpi_endpoint;
static seL4_CPtr self_as_cap;

/* global static memory for init */
static sel4utils_alloc_data_t alloc_data;

/* dimensions of virtual memory for the allocator to use */
#define ALLOCATOR_VIRTUAL_POOL_SIZE ((1 << seL4_PageBits) * 4000)

/* allocator static pool */
#define ALLOCATOR_STATIC_POOL_SIZE ((1 << seL4_PageBits) * 20)
static char allocator_mem_pool1[ALLOCATOR_STATIC_POOL_SIZE];
static char allocator_mem_pool2[ALLOCATOR_STATIC_POOL_SIZE];


void thread_testing(void) {
    printf("Thread testing entering\n");

    int i = 0;
    while (i < 10){
        printf("Sleepign for a second\n");
        //sleep(1);
        i++;
    }
   // *shared_addr_between_threads = 77;
    printf("Thread testing leaving\n");
    while (1);
}


/* override abort, called by exit (and assert fail) */
void abort(void)
{
    /* send back a failure */
    seL4_MessageInfo_t info = seL4_MessageInfo_new(seL4_Fault_NullFault, 0, 0, 1);
    seL4_SetMR(0, -1);
    seL4_Send(endpoint, info);

    /* we should not get here */
    assert(0);
    while (1);
}

void __plat_putchar(int c);
static size_t write_buf(void *data, size_t count)
{
    char *buf = data;
    for (int i = 0; i < count; i++) {
        __plat_putchar(buf[i]);
    }
    return count;
}

static testcase_t *find_test(const char *name)
{
    testcase_t *test = sel4test_get_test(name);
    if (test == NULL) {
        ZF_LOGF("Failed to find test %s", name);
    }

    return test;
}

// siagraw:This new process also needs to have its own allocator for getting kernel
// objects like caps etc.....
// I have not delved deep into this for now.
static void init_allocator(env_t env, test_init_data_t *init_data)
{
    UNUSED int error;
    UNUSED reservation_t virtual_reservation;

    size_t cspace_size_bits;
    seL4_CPtr cnode_start, cnode_end;
    size_t factor = 2;
    cspace_size_bits = init_data->cspace_size_bits - factor;
    cnode_start = init_data->free_slots.start;
    cnode_end = init_data->free_slots.end >> factor;



    /* initialise allocator */
    allocman_t *allocator1 = bootstrap_use_current_1level(init_data->root_cnode,
                                                         cspace_size_bits, 
                                                         cnode_start,
                                                         cnode_end,
                                                         ALLOCATOR_STATIC_POOL_SIZE,
                                                         allocator_mem_pool1);
                                                         /*
    allocman_t *allocator1 = bootstrap_use_current_1level(init_data->root_cnode, 
                                                         cspace_size_bits, cnode_start, 
                                                         cnode_end, ALLOCATOR_STATIC_POOL_SIZE,
                                                         allocator_mem_pool1);
                                                         */

    printf("cspace details1: root: %ld\t size_bits: %ld\t start: %ld\t end:%ld\n",
           init_data->root_cnode, cspace_size_bits, cnode_start, cnode_end);
    if (allocator1 == NULL) {
        ZF_LOGF("Failed to bootstrap allocator");
    }
    allocman_make_vka(&env->vka, allocator1);

    /* fill the allocator with untypeds */
    seL4_CPtr slot;
    unsigned int size_bits_index;
    size_t size_bits;
    cspacepath_t path;
    size_t alloc1_size = 0;
    for (slot = init_data->untypeds.start, size_bits_index = 0;
         slot <= init_data->untypeds.end;
         slot++, size_bits_index++) {

        vka_cspace_make_path(&env->vka, slot, &path);
        /* allocman doesn't require the paddr unless we need to ask for phys addresses,
         * which we don't. */
        size_bits = init_data->untyped_size_bits_list[size_bits_index];
        error = allocman_utspace_add_uts(allocator1, 1, &path, &size_bits, NULL,
                                         ALLOCMAN_UT_KERNEL);
        
        
        if (error) {
            ZF_LOGF("Failed to add untyped objects to allocator");
        }
        printf("Added untyped to allocator: [%ld]: sz: %s\n", slot, 
               human_readable_size(1ULL<<size_bits));
        alloc1_size += 1ULL<<size_bits;

        if (alloc1_size > 400 * 1024 * 1024) {
            printf("Allocator1 is big enough");
            slot++;
            size_bits_index++;
            break;
        }
    }

    /* add any arch specific objects to the allocator */
    arch_init_allocator(env, init_data);

    /* create a vspace */
    void *existing_frames[init_data->stack_pages + 2];
    existing_frames[0] = (void *) init_data;
    existing_frames[1] = seL4_GetIPCBuffer();
    assert(init_data->stack_pages > 0);
    for (int i = 0; i < init_data->stack_pages; i++) {
        existing_frames[i + 2] = init_data->stack + (i * PAGE_SIZE_4K);
    }

    error = sel4utils_bootstrap_vspace(&env->vspace, &alloc_data, init_data->page_directory, &env->vka,
                                       NULL, NULL, existing_frames);

    /* switch the allocator to a virtual memory pool */
    void *vaddr;
    virtual_reservation = vspace_reserve_range(&env->vspace, ALLOCATOR_VIRTUAL_POOL_SIZE,
                                               seL4_AllRights, 1, &vaddr);
    if (virtual_reservation.res == 0) {
        ZF_LOGF("Failed to switch allocator to virtual memory pool");
    }

    bootstrap_configure_virtual_pool(allocator1, vaddr, ALLOCATOR_VIRTUAL_POOL_SIZE,
                                     env->page_directory);
    
    /* setup a secodn allocator */
    size_t cspace_size_bits2;
    seL4_CPtr cnode_start2, cnode_end2;
    cspace_size_bits2 = cspace_size_bits;
    cnode_start2 = cnode_end +1;
    cnode_end2 = cnode_start2 + (1 << cspace_size_bits2);

    printf("====cspace details2: root: %ld\t size_bits: %ld\t start: %lx\t end:%lx\n",
           init_data->root_cnode, cspace_size_bits2, cnode_start2, cnode_end2-1);
    allocman_t *allocator2 = bootstrap_use_current_1level(init_data->root_cnode,
                                                         cspace_size_bits2, 
                                                         cnode_start2,
                                                         cnode_end2,
                                                         ALLOCATOR_STATIC_POOL_SIZE,
                                                         allocator_mem_pool2);
    if (allocator2 == NULL) {
        ZF_LOGF("Failed to bootstrap allocator");
    }
    vka_t vka2;
    allocman_make_vka(&vka2, allocator1);



    // Add UTS
    for (;
         slot <= init_data->untypeds.end;
         slot++, size_bits_index++) {

        vka_cspace_make_path(&env->vka, slot, &path);
        /* allocman doesn't require the paddr unless we need to ask for phys addresses,
         * which we don't. */
        size_bits = init_data->untyped_size_bits_list[size_bits_index];
        error = allocman_utspace_add_uts(allocator2, 1, &path, &size_bits, NULL,
                                         ALLOCMAN_UT_KERNEL);
        
        
        if (error) {
            ZF_LOGF("Failed to add untyped objects to allocator");
        }
        printf("Added untyped to allocator2: [%ld]: sz: %s\n", slot, 
               human_readable_size(1ULL<<size_bits));
        alloc1_size += 1ULL<<size_bits;

    }

    /* switch the allocator to a virtual memory pool */
    virtual_reservation = vspace_reserve_range(&env->vspace, ALLOCATOR_VIRTUAL_POOL_SIZE,
                                               seL4_AllRights, 1, &vaddr);
    if (virtual_reservation.res == 0) {
        ZF_LOGF("Failed to switch allocator to virtual memory pool");
    }

    bootstrap_configure_virtual_pool(allocator2, vaddr, ALLOCATOR_VIRTUAL_POOL_SIZE,
                                     env->page_directory);

    for (int i = 0; i < 10; i++)
    {
        vka_object_t obj;
        error = vka_alloc_frame(&vka2, 21, &obj);
        if (error)
        {
            ZF_LOGF("Failed to allocate cslot for vka2");
        }
        else
        {
            printf("Allocated cslot for vka2: %ld\n", obj.cptr);
        }
    }
}

static uint8_t cnode_size_bits(void *data)
{
    test_init_data_t *init = (test_init_data_t *) data;
    return init->cspace_size_bits;
}

static seL4_CPtr sched_ctrl(void *data, int core)
{
    return ((test_init_data_t *) data)->sched_ctrl + core;
}

static int core_count(UNUSED void *data)
{
    return ((test_init_data_t *) data)->cores;
}

void init_simple(env_t env, test_init_data_t *init_data)
{
    /* minimal simple implementation */
    env->simple.data = (void *) init_data;
    env->simple.arch_simple.data = (void *) init_data;
    env->simple.init_cap = sel4utils_process_init_cap;
    env->simple.cnode_size = cnode_size_bits;
    env->simple.sched_ctrl = sched_ctrl;
    env->simple.core_count = core_count;

    arch_init_simple(env, &env->simple);
}
// each invocation of this binary has 2 args:
// endpoint to communicate back to driver
// init data which has the name of the test run.

_Thread_local int counter = 0;
int main(int argc, char **argv)
{
    sel4muslcsys_register_stdio_write_fn(write_buf);


   //int x = 0x55;
   // shared_addr_between_threads = &x;
   // printf("address of x is %p\n", shared_addr_between_threads);

    test_init_data_t *init_data;
    struct env env;

    /* parse args */
    assert(argc == 4);
    endpoint = (seL4_CPtr) atoi(argv[0]);

    /* read in init data */
    init_data = (void *) atol(argv[1]);

    self_as_cap = (seL4_CPtr) atoi(argv[2]);
    gpi_endpoint = (seL4_CPtr) atoi(argv[3]);

    /* configure env */
    env.cspace_root = init_data->root_cnode;
    env.page_directory = init_data->page_directory;
    env.endpoint = endpoint;
    env.self_as_cptr = self_as_cap;
    env.gpi_endpoint = gpi_endpoint;
    env.priority = init_data->priority;
    env.cspace_size_bits = init_data->cspace_size_bits;
    env.tcb = init_data->tcb;
    env.domain = init_data->domain;
    env.asid_pool = init_data->asid_pool;
    env.asid_ctrl = init_data->asid_ctrl;
    env.sched_ctrl = init_data->sched_ctrl;
#ifdef CONFIG_IOMMU
    env.io_space = init_data->io_space;
#endif
#ifdef CONFIG_TK1_SMMU
    env.io_space_caps = init_data->io_space_caps;
#endif
    env.cores = init_data->cores;
    env.num_regions = init_data->num_elf_regions;
    memcpy(env.regions, init_data->elf_regions, sizeof(sel4utils_elf_region_t) * env.num_regions);

    env.timer_notification.cptr = init_data->timer_ntfn;

    env.device_frame = init_data->device_frame_cap;

    /* initialse cspace, vspace and untyped memory allocation */
    init_allocator(&env, init_data);
    printf("%s %d self_as_cptr is %ld: ", __FUNCTION__, __LINE__, self_as_cap);
    debug_cap_identify("test-main", self_as_cap);

    printf("%s %d ads_endpoint is %ld: ", __FUNCTION__, __LINE__, gpi_endpoint);
    debug_cap_identify("test-main", gpi_endpoint);
    /* initialise simple */
    init_simple(&env, init_data);

    /* initialise rpc client */
    sel4rpc_client_init(&env.rpc_client, env.endpoint, SEL4TEST_PROTOBUF_RPC);

    /* find the test */
    testcase_t *test = find_test(init_data->name);

    /* run the test */
    sel4test_reset();
    test_result_t result = SUCCESS;
    if (test) {
        printf("Running test %s (%s)\n", test->name, test->description);
        result = test->function((uintptr_t)&env);
    } else {
        result = FAILURE;
        ZF_LOGF("Cannot find test %s\n", init_data->name);
    }


    printf("Test %s %s\n", init_data->name, result == SUCCESS ? "passed" : "failed");
    /* send our result back */
    seL4_MessageInfo_t info = seL4_MessageInfo_new(seL4_Fault_NullFault, 0, 0, 1);
    seL4_SetMR(0, result);
    seL4_Send(endpoint, info);

    /* It is expected that we are torn down by the test driver before we are
     * scheduled to run again after signalling them with the above send.
     */
    assert(!"unreachable");


    return 0;
}
