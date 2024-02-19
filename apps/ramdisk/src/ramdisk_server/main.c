/**
 * @file Entry point to start the ramdisk server in a new process
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <allocman/vka.h>
#include <allocman/bootstrap.h>

#include <sel4gpi/pd_obj.h>

#include <ramdisk_server.h>


#define APP_MALLOC_SIZE 1 * 1024
char __attribute__((aligned(PAGE_SIZE_4K))) morecore_area[APP_MALLOC_SIZE];
size_t morecore_size = APP_MALLOC_SIZE;
/* Pointer to free space in the morecore area. */
uintptr_t morecore_top = (uintptr_t) &morecore_area[APP_MALLOC_SIZE];

/* global static memory for init */
static sel4utils_alloc_data_t alloc_data;

/* allocator static pool */
#define ALLOCATOR_STATIC_POOL_SIZE ((1 << seL4_PageBits) * 20)
static char allocator_mem_pool[ALLOCATOR_STATIC_POOL_SIZE];

static void init_allocator(pd_env_t *env, pd_init_data_t *init_data)
{
    UNUSED int error;
    UNUSED reservation_t virtual_reservation;

    /* initialise allocator */
    allocman_t *allocator = bootstrap_use_current_1level(init_data->root_cnode,
                                                         init_data->cspace_size_bits, init_data->free_slots.start,
                                                         init_data->free_slots.end, ALLOCATOR_STATIC_POOL_SIZE,
                                                         allocator_mem_pool);
    if (allocator == NULL)
    {
        ZF_LOGF("Failed to bootstrap allocator");
    }
    allocman_make_vka(&env->vka, allocator);
}

int main(int argc, char **argv)
{
    printf("Ramdisk main!\n");

    pd_env_t env;
    pd_init_data_t *init_data;

    /* parse args */
    assert(argc == 2);
    init_data = (void *)atol(argv[1]);

    assert(init_data->n_init_caps >= 1);
    seL4_CPtr parent_ep = init_data->init_caps[0];

    /* init cspace allocator */
    init_allocator(&env, init_data);

    return ramdisk_server_start(&env.vka, 
                                init_data,
                                parent_ep);

}