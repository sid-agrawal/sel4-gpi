/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Include Kconfig variables. */
#include <autoconf.h>
#include <sel4test-driver/gen_config.h>
#include <utils/util.h>

#include <sel4debug/register_dump.h>
#include <vka/capops.h>

#include "test.h"
#include "timer.h"
#include <sel4rpc/server.h>
#include <sel4gpi/ads_component.h>
#include <sel4gpi/mo_component.h>
#include <sel4gpi/cpu_component.h>
#include <sel4gpi/pd_component.h>
#include <sel4testsupport/testreporter.h>

#define QEMU_SERIAL_IRQ 33

/* Bootstrap test type. */
static inline void bootstrap_set_up_test_type(uintptr_t e)
{
    ZF_LOGD("setting up bootstrap test type\n");
}
static inline void bootstrap_tear_down_test_type(uintptr_t e)
{
    ZF_LOGD("tearing down bootstrap test type\n");
}
static inline void bootstrap_set_up(uintptr_t e)
{
    ZF_LOGD("set up bootstrap test\n");
}
static inline void bootstrap_tear_down(uintptr_t e)
{
    ZF_LOGD("tear down bootstrap test\n");
}
static inline test_result_t bootstrap_run_test(struct testcase *test, uintptr_t e)
{
    return test->function(e);
}

static DEFINE_TEST_TYPE(BOOTSTRAP, BOOTSTRAP,
                        bootstrap_set_up_test_type, bootstrap_tear_down_test_type,
                        bootstrap_set_up, bootstrap_tear_down, bootstrap_run_test);

/* Basic test type. Each test is launched as its own process. */
/* copy untyped caps into a processes cspace, return the cap range they can be found in */
static seL4_SlotRegion copy_untypeds_to_process(sel4utils_process_t *process,
                                                vka_object_t *untypeds,
                                                int num_untypeds,
                                                driver_env_t env)
{
    seL4_SlotRegion range = {0};

    for (int i = 0; i < num_untypeds; i++)
    {
        // printf("DRIVER: Adding untyped to process: cap: %lu ut: %lu sz: %s\n ",
        //        untypeds[i].cptr,
        //        untypeds[i].ut,
        //        human_readable_size(1ULL << untypeds[i].size_bits));
        seL4_CPtr slot = sel4utils_copy_cap_to_process(process, &env->vka, untypeds[i].cptr);

        /* set up the cap range */
        if (i == 0)
        {
            range.start = slot;
        }
        range.end = slot;
    }
    assert((range.end - range.start) + 1 == num_untypeds);
    return range;
}

static void handle_timer_requests(driver_env_t env, sel4test_output_t test_output)
{

    seL4_MessageInfo_t info;
    uint64_t timeServer_ns;
    seL4_Word timeServer_timeoutType;

    switch (test_output)
    {

    case SEL4TEST_TIME_TIMEOUT:

        timeServer_timeoutType = seL4_GetMR(1);
        timeServer_ns = sel4utils_64_get_mr(2);

        timeout(env, timeServer_ns, timeServer_timeoutType);

        info = seL4_MessageInfo_new(seL4_Fault_NullFault, 0, 0, 1);

        seL4_SetMR(0, 0);
        api_reply(env->reply.cptr, info);
        break;

    case SEL4TEST_TIME_TIMESTAMP:
        timeServer_ns = timestamp(env);
        sel4utils_64_set_mr(1, timeServer_ns);
        info = seL4_MessageInfo_new(seL4_Fault_NullFault, 0, 0, SEL4UTILS_64_WORDS + 1);
        seL4_SetMR(0, 0);
        api_reply(env->reply.cptr, info);
        break;

    case SEL4TEST_TIME_RESET:
        timer_reset(env);
        info = seL4_MessageInfo_new(seL4_Fault_NullFault, 0, 0, 1);
        seL4_SetMR(0, 0);
        api_reply(env->reply.cptr, info);
        break;

    default:
        ZF_LOGF("Invalid time request");
        break;
    }
}

/* This function waits on:
 * Timer interrupts (from hardware)
 * Requests from tests (sel4driver acts as a server)
 * Results from sel4test/tests
 */
static int sel4test_driver_wait(driver_env_t env, struct testcase *test)
{
    seL4_MessageInfo_t info;
    sel4test_output_t test_output;
    int result = SUCCESS;
    seL4_Word badge = 0;
    sel4rpc_server_env_t rpc_server;

    // siagraw: we start and rpc server to listen for requests form the test binary.
    sel4rpc_server_init(&rpc_server, &env->vka, sel4rpc_default_handler, env,
                        &env->reply, &env->simple);

    while (1)
    {
        /* wait for tests to finish or fault, receive test request or report result */
        info = api_recv(env->test_process.fault_endpoint.cptr, &badge, env->reply.cptr);
        test_output = seL4_GetMR(0);

        /* FIXME: Assumptions made at the time of writing this code:
         * 1) fault sync EP cap has a badge of 0
         * 2) notification_cap bound to sel4test-driver TCB, and has a non zero badge.
         * 3) sel4test-driver only sets up and expects timer interrupts. If, in the
         * future, other types of interrupts are to be handled, the following code would
         * be wrong, and would need refactoring.
         *
         * For now, assume it is a timer interrupt, handle it and signal any test processes
         * that might be waiting on it.
         */
        if (badge != 0)
        {
            assert(config_set(CONFIG_HAVE_TIMER));
        }

        if (config_set(CONFIG_HAVE_TIMER) && badge != 0)
        {
            /* handle timer interrupts in hardware */
            handle_timer_interrupts(env, badge);
            /* Driver does extra work to check whether timeout succeeded and signals
             * clients/tests
             */
            int error = tm_update(&env->tm);
            ZF_LOGF_IF(error, "Failed to update time manager");
            continue;
        }

        if (sel4test_isTimerRPC(test_output))
        {

            if (config_set(CONFIG_HAVE_TIMER))
            {
                handle_timer_requests(env, test_output);
                continue;
            }
            else
            {
                ZF_LOGF("Requesting a timer service from sel4test-driver while there is no"
                        "supported HW timer.");
            }
        }
        else if (test_output == SEL4TEST_PROTOBUF_RPC)
        {
            sel4rpc_server_recv(&rpc_server);
            continue;
        }

        result = test_output;
        // if (seL4_MessageInfo_get_label(info) != seL4_Fault_NullFault)
        // {
        sel4utils_print_fault_message(info, test->name);
        printf("Register of root thread in test (may not be the thread that faulted)\n");
        sel4debug_dump_registers(env->test_process.thread.tcb.cptr);
        result = FAILURE;
        // }

        if (config_set(CONFIG_HAVE_TIMER))
        {
            timer_cleanup(env);
        }

        return result;
    }
}

void basic_set_up(uintptr_t e)
{
    int error;
    driver_env_t env = (driver_env_t)e;

    sel4utils_process_config_t config = process_config_default_simple(&env->simple, TESTS_APP, env->init->priority);
    config = process_config_mcp(config, seL4_MaxPrio);
    config = process_config_auth(config, simple_get_tcb(&env->simple));
    config = process_config_create_cnode(config, TEST_PROCESS_CSPACE_SIZE_BITS);
    error = sel4utils_configure_process_custom(&(env->test_process), &env->vka, &env->vspace, config);
    assert(error == 0);

    /* set up caps about the process */
    env->init->stack_pages = CONFIG_SEL4UTILS_STACK_SIZE / PAGE_SIZE_4K;
    env->init->stack = env->test_process.thread.stack_top - CONFIG_SEL4UTILS_STACK_SIZE;
    env->init->page_directory = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, env->test_process.pd.cptr);
    env->init->root_cnode = SEL4UTILS_CNODE_SLOT;
    env->init->tcb = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, env->test_process.thread.tcb.cptr);
    if (config_set(CONFIG_HAVE_TIMER))
    {
        env->init->timer_ntfn = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, env->timer_notify_test.cptr);
    }

    /* Get an IRQ Handler for serial device */
    cspacepath_t path;
    error = vka_cspace_alloc_path(&env->vka, &path);
    ZF_LOGF_IFERR(error, "Failed to allocate path for IRQ handler\n");

    error = simple_get_IRQ_handler(&env->simple, QEMU_SERIAL_IRQ, path);
    ZF_LOGF_IFERR(error, "Failed to make QEMU UART IRQ Handler\n");

    /* NOTE:
       The return from sel4utils_copy_cap_to_process is the slot in the cnode where the cap was placed
       in the child process' cspace
    */
    env->init->domain = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, simple_get_init_cap(&env->simple, seL4_CapDomain));
    env->init->asid_pool = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, simple_get_init_cap(&env->simple, seL4_CapInitThreadASIDPool));
    env->init->asid_ctrl = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, simple_get_init_cap(&env->simple, seL4_CapASIDControl));
    env->init->irq_handler = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, path.capPtr);
#ifdef CONFIG_IOMMU
    env->init->io_space = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, simple_get_init_cap(&env->simple, seL4_CapIOSpace));
#endif /* CONFIG_IOMMU */
#ifdef CONFIG_TK1_SMMU
    env->init->io_space_caps = arch_copy_iospace_caps_to_process(&(env->test_process), &env);
#endif
    env->init->cores = simple_get_core_count(&env->simple);
    /* copy the sched ctrl caps to the remote process */
    if (config_set(CONFIG_KERNEL_MCS))
    {
        seL4_CPtr sched_ctrl = simple_get_sched_ctrl(&env->simple, 0);
        env->init->sched_ctrl = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, sched_ctrl);
        for (int i = 1; i < env->init->cores; i++)
        {
            sched_ctrl = simple_get_sched_ctrl(&env->simple, i);
            sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, sched_ctrl);
        }
    }
    /* setup data about untypeds */
    env->init->untypeds = copy_untypeds_to_process(&(env->test_process),
                                                   env->untypeds,
                                                   env->num_untypeds,
                                                   env);
    /* copy the fault endpoint - we wait on the endpoint for a message
     * or a fault to see when the test finishes */
    env->endpoint = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, env->test_process.fault_endpoint.cptr);

    // For the child's as cap in the child
    // First forge a cap to the child's vspace
    seL4_CPtr child_as_cap_in_parent;
    error = forge_ads_cap_from_vspace(&env->test_process.vspace, &env->vka, &child_as_cap_in_parent);
    if (error)
    {
        ZF_LOGF("Failed to forge child's as cap");
    }
    // ads_component_registry_entry_t *head = get_ads_component()->client_registry;

    env->child_ads_cptr_in_child = sel4utils_copy_cap_to_process(&(env->test_process),
                                                                 &env->vka, child_as_cap_in_parent);

    assert(env->child_ads_cptr_in_child != 0);

    /* Forge MO caps for the ADS */
    seL4_CPtr mo_caps[MAX_MO_CHILD];
    uint32_t ret_num_mo;

    error = forge_mo_caps_from_vspace(
        &env->test_process.vspace,
        &env->vka,
        &ret_num_mo,
        mo_caps);
    assert(error == 0);

    for (int i = 0; i < ret_num_mo; i++)
    {
        ZF_LOGE("MO CAP[%d]: %lu", i, mo_caps[i]);
        env->child_mo_cptr_in_child[i] = sel4utils_copy_cap_to_process(
            &(env->test_process),
            &env->vka,
            mo_caps[i]);
        assert(env->child_mo_cptr_in_child[i] != 0);
    }

    // Here, do the same for the CPU cap too
    seL4_CPtr child_cpu_cap_in_parent;
    error = forge_cpu_cap_from_tcb(&env->test_process, &env->vka, &child_cpu_cap_in_parent);
    if (error)
    {
        ZF_LOGF("Failed to forge child's CPU cap");
    }
    // cpu_component_registry_entry_t *head_cpu = get_cpu_component()->client_registry;

    env->child_cpu_cptr_in_child = sel4utils_copy_cap_to_process(&(env->test_process),
                                                                 &env->vka, child_cpu_cap_in_parent);
    assert(env->child_cpu_cptr_in_child != 0);

    // Here, do the same for the CPU cap too
    // We have a cathc 22 here. The

    //--------------------------------------------------------------------
#define PD_FORGE 1
#ifdef PD_FORGE
    seL4_CPtr child_pd_cap_in_parent;
    error = forge_pd_cap_from_init_data(env->init, &env->vka, &child_pd_cap_in_parent);
    if (error)
    {
        ZF_LOGF("Failed to forge child's PD cap");
    }
    env->child_pd_cptr_in_child = sel4utils_copy_cap_to_process(&(env->test_process),
                                                                &env->vka, child_pd_cap_in_parent);
    assert(env->child_pd_cptr_in_child != 0);
    //--------------------------------------------------------------------
#endif

    // For the ads-server
    env->gpi_endpoint_in_child = sel4utils_copy_cap_to_process(&(env->test_process),
                                                               &env->vka, env->gpi_endpoint_in_parent);

    // For the ads-server
    env->ramdisk_endpoint_in_child = sel4utils_copy_cap_to_process(&(env->test_process),
                                                                   &env->vka, env->ramdisk_endpoint_in_parent);

    // Keep this one as the last COPY, so that  init->free_slot.start a few lines below stays valid.
    // See at label "Warning"
    seL4_CPtr free_slot_start = env->ramdisk_endpoint_in_child + 1;

    /* copy the device frame, if any */
    if (env->init->device_frame_cap)
    {
        env->init->device_frame_cap = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, env->device_obj.cptr);
        free_slot_start = env->init->device_frame_cap + 1;
    }

    /* map the cap into remote vspace */
    env->remote_vaddr = vspace_share_mem(&env->vspace, &(env->test_process).vspace, env->init, 1, PAGE_BITS_4K,
                                         seL4_AllRights, 1);

    assert(env->remote_vaddr != 0);

Warning:

    /* WARNING: DO NOT COPY MORE CAPS TO THE PROCESS BEYOND THIS POINT,
     * AS THE SLOTS WILL BE CONSIDERED FREE AND OVERRIDDEN BY THE TEST PROCESS. */
    /* set up free slot range */
    env->init->cspace_size_bits = TEST_PROCESS_CSPACE_SIZE_BITS;
    env->init->free_slots.start = free_slot_start;
    printf("%s:%d: free_slot.start %ld\n", __FUNCTION__, __LINE__, env->init->free_slots.start);
    env->init->free_slots.end = (1u << TEST_PROCESS_CSPACE_SIZE_BITS);
    assert(env->init->free_slots.start < env->init->free_slots.end);

#ifdef PD_FORGE
    update_forged_pd_cap_from_init_data(env->init, env->child_pd_cptr_in_child);
#endif
}

test_result_t basic_run_test(struct testcase *test, uintptr_t e)
{
    int error;
    driver_env_t env = (driver_env_t)e;

    /* copy test name */
    strncpy(env->init->name, test->name, TEST_NAME_MAX);
    /* ensure string is null terminated */
    env->init->name[TEST_NAME_MAX - 1] = '\0';
#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugNameThread(env->test_process.thread.tcb.cptr, env->init->name);
#endif

    /* set up args for the test process */
    seL4_Word argc = 7;
    char string_args[argc][WORD_STRING_SIZE];
    char *argv[argc];
    sel4utils_create_word_args(string_args, argv, argc,
                               env->endpoint,
                               env->remote_vaddr,
                               env->child_ads_cptr_in_child,
                               env->child_cpu_cptr_in_child,
                               env->child_pd_cptr_in_child,
                               env->gpi_endpoint_in_child,
                               env->ramdisk_endpoint_in_child);

    int num_res;
    /* spawn the process */
    error = sel4utils_spawn_process_v(&(env->test_process), &env->vka, &env->vspace,
                                      argc, argv, 1);
    ZF_LOGF_IF(error != 0, "Failed to start test process!");

    if (config_set(CONFIG_HAVE_TIMER))
    {
        error = tm_alloc_id_at(&env->tm, TIMER_ID);
        ZF_LOGF_IF(error != 0, "Failed to alloc time id %d", TIMER_ID);
    }

    /* wait on it to finish or fault, report result */
    int result = sel4test_driver_wait(env, test);

    test_assert(result == SUCCESS);

    return result;
}

void basic_tear_down(uintptr_t e)
{
    driver_env_t env = (driver_env_t)e;
    /* unmap the env->init data frame */
    vspace_unmap_pages(&(env->test_process).vspace, env->remote_vaddr, 1, PAGE_BITS_4K, NULL);

    /* reset all the untypeds for the next test */
    for (int i = 0; i < env->num_untypeds; i++)
    {
        cspacepath_t path;
        vka_cspace_make_path(&env->vka, env->untypeds[i].cptr, &path);
        vka_cnode_revoke(&path);
    }

    /* destroy the process */
    sel4utils_destroy_process(&(env->test_process), &env->vka);
}

DEFINE_TEST_TYPE(BASIC, BASIC, NULL, NULL, basic_set_up, basic_tear_down, basic_run_test);

#if 0
void start_thread_stack(vka_t *vka, vspace_t *current, vspace_t *target, vspace_t *sibling,
                        seL4_CNode cspace)
{

    sel4utils_thread_entry_fn ep = (sel4utils_thread_entry_fn)0x405d88;
    sel4utils_thread_t thread_data;
    int err = sel4utils_configure_thread(vka,
                                         current,
                                         sibling, // target,
                                         0,
                                         cspace,
                                         0,
                                         &thread_data);
    if (err)
    {
        ZF_LOGF("Failed to configure thread");
    }

    // sel4utils_walk_vspace(target, vka);

    err = sel4utils_start_thread(&thread_data,
                                 ep,
                                 NULL, NULL,
                                 true);
    if (err)
    {
        ZF_LOGF("Failed to start thread");
    }
}
#endif