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
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/ads_component.h>
#include <sel4gpi/mo_component.h>
#include <sel4gpi/cpu_component.h>
#include <sel4gpi/pd_component.h>
#include <sel4gpi/endpoint_component.h>
#include <sel4gpi/error_handle.h>
#include <sel4testsupport/testreporter.h>

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
        
        // Enabled the #if to clear all untypeds before the test
        // This prevents a slowdown in tests beyond the first test, caused by the kernel zero-ing the untypeds
#if 0
        // fake retype to clear the untyped first
        cspacepath_t dest;
        int error = vka_cspace_alloc_path(&env->vka, &dest);
        assert(error == 0);

        error = vka_untyped_retype(&untypeds[i], seL4_UntypedObject, untypeds[i].size_bits, 1, &dest);
        assert(error == 0);

        // printf("DRIVER: Adding untyped to process: cap: %lu ut: %lu sz: %s\n ",
        //        untypeds[i].cptr,
        //        untypeds[i].ut,
        //        human_readable_size(1ULL << untypeds[i].size_bits));
        seL4_CPtr slot = sel4utils_copy_cap_to_process(process, &env->vka, dest.capPtr);
#else
        // printf("DRIVER: Adding untyped to process: cap: %lu ut: %lu sz: %s\n ",
        //        untypeds[i].cptr,
        //        untypeds[i].ut,
        //        human_readable_size(1ULL << untypeds[i].size_bits));
        seL4_CPtr slot = sel4utils_copy_cap_to_process(process, &env->vka, untypeds[i].cptr);
#endif

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
        info = api_recv(env->endpoint_in_driver, &badge, env->reply.cptr);
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
        if (seL4_MessageInfo_get_label(info))
        {
            sel4utils_print_fault_message(info, test->name);

            if (test->test_type == BASIC)
            {
                printf("Register of root thread in test (may not be the thread that faulted)\n");
                sel4debug_dump_registers(env->test_process.thread.tcb.cptr);
            }
            // (XXX) Arya: Can add a call to dump registers from the osm process as well

            result = FAILURE;
        }

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

    /* NOTE:
       The return from sel4utils_copy_cap_to_process is the slot in the cnode where the cap was placed
       in the child process' cspace
    */
    env->init->domain = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, simple_get_init_cap(&env->simple, seL4_CapDomain));
    env->init->asid_pool = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, simple_get_init_cap(&env->simple, seL4_CapInitThreadASIDPool));
    env->init->asid_ctrl = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, simple_get_init_cap(&env->simple, seL4_CapASIDControl));
    env->init->serial_irq_handler = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, env->serial_irq_handler);
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
    env->endpoint_in_driver = env->test_process.fault_endpoint.cptr;
    env->endpoint_in_test = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, env->test_process.fault_endpoint.cptr);

    /* get the benchmark IPC endpoint */
    // (XXX) Arya: This used to communicate with GPI server, but now we do not start it for basic tests
    //env->bench_endpoint_in_driver = pd_component_create_ipc_bench_ep();
    env->bench_endpoint_in_driver = env->test_process.fault_endpoint.cptr;
    env->bench_endpoint_in_test = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, env->bench_endpoint_in_driver);

    // Keep this one as the last COPY, so that  init->free_slot.start a few lines below stays valid.
    // See at label "Warning"
    seL4_CPtr free_slot_start = env->bench_endpoint_in_test + 1;

    /* copy the device frame, if any */
    if (env->init->device_frame_cap)
    {
        env->init->device_frame_cap = sel4utils_copy_cap_to_process(&(env->test_process), &env->vka, env->device_obj.cptr);
        free_slot_start = env->init->device_frame_cap + 1;
    }

    /* map the cap into remote vspace */
    env->init_vaddr = vspace_share_mem(&env->vspace, &(env->test_process).vspace, env->init, 1, PAGE_BITS_4K,
                                       seL4_AllRights, 1);

    assert(env->init_vaddr != 0);

Warning:

    /* WARNING: DO NOT COPY MORE CAPS TO THE PROCESS BEYOND THIS POINT,
     * AS THE SLOTS WILL BE CONSIDERED FREE AND OVERRIDDEN BY THE TEST PROCESS. */
    /* set up free slot range */
    env->init->cspace_size_bits = TEST_PROCESS_CSPACE_SIZE_BITS;
    env->init->free_slots.start = free_slot_start;
    printf("%s:%d: free_slot.start %ld\n", __FUNCTION__, __LINE__, env->init->free_slots.start);
    env->init->free_slots.end = (1u << TEST_PROCESS_CSPACE_SIZE_BITS);
    assert(env->init->free_slots.start < env->init->free_slots.end);
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
    seL4_Word argc = 4;
    char string_args[argc][WORD_STRING_SIZE];
    char *argv[argc];
    sel4utils_create_word_args(string_args, argv, argc,
                               BASIC,
                               env->endpoint_in_test,
                               env->init_vaddr,
                               env->bench_endpoint_in_test);

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
    vspace_unmap_pages(&(env->test_process).vspace, env->init_vaddr, 1, PAGE_BITS_4K, NULL);

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

void osm_set_up(uintptr_t e)
{
    int error = 0;
    driver_env_t env = (driver_env_t)e;

    // Create the init data MO
    mo_t *osm_init_mo;
    error = mo_component_allocate_rt(1, &osm_init_mo);
    assert(error == 0);

    // Create the PD, ADS, and CPU
    pd_t *pd;
    error = pd_component_allocate(get_gpi_server()->rt_pd_id, osm_init_mo, &pd, NULL);
    assert(error == 0);
    env->test_pd = pd;

    ads_t *ads;
    seL4_CPtr ads_slot_in_test;
    error = ads_component_allocate(pd->id, &ads, &ads_slot_in_test);
    assert(error == 0);

    cpu_t *cpu;
    seL4_CPtr cpu_slot_in_test;
    error = cpu_component_allocate(pd->id, &cpu, NULL);
    assert(error == 0);
    env->test_cpu = cpu;

    // Set the PD's core caps
    error = pd_send_cap(pd, 0,
                        gpi_new_badge(GPICAP_TYPE_PD, 0, pd->id, get_pd_component()->space_id, pd->id),
                        NULL, false, true);
    assert(error == 0);

    error = pd_set_core_cap(pd,
                            gpi_new_badge(GPICAP_TYPE_ADS, 0, pd->id, get_ads_component()->space_id, ads->id),
                            ads_slot_in_test);
    assert(error == 0);

    error = pd_set_core_cap(pd,
                            gpi_new_badge(GPICAP_TYPE_CPU, 0, pd->id, get_cpu_component()->space_id, cpu->id),
                            cpu_slot_in_test);
    assert(error == 0);

    // Load the test image in the ADS
    void *entry_pt;
    error = ads_component_load_elf(ads, pd, TESTS_APP, &entry_pt);
    assert(error == 0);

    // Load the init data in the ADS
    void *osm_init_vaddr;
    error = ads_attach(ads, pd->pd_vka, NULL, osm_init_mo, 1, seL4_ReadWrite,
                       &osm_init_vaddr, SEL4UTILS_RES_TYPE_SHARED_FRAMES);
    assert(error == 0);

    // Decrement init data refcount since it was allocated by RT
    error = resource_component_dec(get_mo_component(), osm_init_mo->id);
    assert(error == 0);

    // Create the IPC buffer
    mo_t *ipc_buf_mo;
    void *ipc_buf_vaddr;
    error = mo_component_allocate_rt(1, &ipc_buf_mo);
    assert(error == 0);

    // Attach the IPC buffer
    error = ads_attach(ads, pd->pd_vka, NULL, ipc_buf_mo, 1, seL4_ReadWrite,
                       &ipc_buf_vaddr, SEL4UTILS_RES_TYPE_IPC_BUF);
    assert(error == 0);

    // Decrement IPC buf refcount since it was allocated by RT
    error = resource_component_dec(get_mo_component(), ipc_buf_mo->id);
    assert(error == 0);

    // Create the stack
    int stack_n_pages = CONFIG_SEL4UTILS_STACK_SIZE / SIZE_BITS_TO_BYTES(MO_PAGE_BITS);
    mo_t *stack_mo;
    error = mo_component_allocate_rt(stack_n_pages, &stack_mo);
    assert(error == 0);

    // Attach stack to test process with guard page
    void *stack_top;

    attach_node_t *stack_attach_node;
    error = ads_reserve(ads, NULL, stack_n_pages + 1, MO_PAGE_BITS, SEL4UTILS_RES_TYPE_STACK,
                        1, seL4_ReadWrite, &stack_attach_node);
    assert(error == 0);

    error = ads_attach_to_res(ads, pd->pd_vka, stack_attach_node, SIZE_BITS_TO_BYTES(MO_PAGE_BITS), stack_mo);
    assert(error == 0);

    stack_top = stack_attach_node->vaddr + CONFIG_SEL4UTILS_STACK_SIZE;

    // Decrement stack refcount since it was allocate by RT
    error = resource_component_dec(get_mo_component(), stack_mo->id);
    assert(error == 0);

    // Create the fault endpoint
    ep_t *ep;
    seL4_CPtr fault_ep_resource_in_test;
    error = ep_component_allocate(pd->id, &env->endpoint_in_test, &fault_ep_resource_in_test, &ep);
    assert(error == 0);
    env->endpoint_in_driver = ep->endpoint_in_RT.cptr;

    // Configure the CPU
    error = cpu_component_configure(cpu, ads, pd,
                                    api_make_guard_skip_word(seL4_WordBits - PD_CSPACE_SIZE_BITS), 0,
                                    ipc_buf_mo, ipc_buf_vaddr);
    assert(error == 0);

    /* Set the benchmark IPC endpoint, same as the PD ep */
    env->bench_endpoint_in_test = pd->shared_data->pd_conn.ep;

    cspacepath_t timer_ntfn_in_PD = {0};
    if (config_set(CONFIG_HAVE_TIMER))
    {
        error = resource_component_transfer_cap(get_gpi_server()->server_vka,
                                                pd->pd_vka,
                                                env->timer_notify_test.cptr,
                                                &timer_ntfn_in_PD,
                                                false,
                                                0);
        WARN_IF_COND(error, "Failed to copy timer notification to PD, sleep requests will fail\n");

        error = tm_alloc_id_at(&env->tm, TIMER_ID);
        WARN_IF_COND(error, "Failed to alloc time id %d, sleep requests will fail", TIMER_ID);
    }

    // Configure the PD runtime
    int argc = 4;
    seL4_Word args[4] = {OSM, env->endpoint_in_test, timer_ntfn_in_PD.capPtr, env->bench_endpoint_in_test};

    error = pd_component_runtime_setup(pd, ads, cpu,
                                       PdSetupType_PD_RUNTIME_SETUP,
                                       argc, args,
                                       stack_top,
                                       entry_pt,
                                       ipc_buf_vaddr,
                                       osm_init_vaddr);
    assert(error == 0);

    // Add the basic RDEs
    rde_type_t resspc_type = {.type = GPICAP_TYPE_RESSPC};
    error = pd_add_rde(pd, resspc_type, "RESSPC", RESSPC_SPACE_ID, get_gpi_server()->server_ep_obj.cptr);
    assert(error == 0);

    rde_type_t ads_type = {.type = GPICAP_TYPE_ADS};
    error = pd_add_rde(pd, ads_type, "ADS", get_ads_component()->space_id, get_gpi_server()->server_ep_obj.cptr);
    assert(error == 0);

    rde_type_t cpu_type = {.type = GPICAP_TYPE_CPU};
    error = pd_add_rde(pd, cpu_type, "CPU", get_cpu_component()->space_id, get_gpi_server()->server_ep_obj.cptr);
    assert(error == 0);

    rde_type_t mo_type = {.type = GPICAP_TYPE_MO};
    error = pd_add_rde(pd, mo_type, "MO", get_mo_component()->space_id, get_gpi_server()->server_ep_obj.cptr);
    assert(error == 0);

    rde_type_t pd_type = {.type = GPICAP_TYPE_PD};
    error = pd_add_rde(pd, pd_type, "PD", get_pd_component()->space_id, get_gpi_server()->server_ep_obj.cptr);
    assert(error == 0);

    rde_type_t ep_type = {.type = GPICAP_TYPE_EP};
    error = pd_add_rde(pd, ep_type, "EP", get_ep_component()->space_id, get_gpi_server()->server_ep_obj.cptr);
}

test_result_t osm_run_test(struct testcase *test, uintptr_t e)
{
    int error;
    driver_env_t env = (driver_env_t)e;

    /* copy test name */
    char *test_name_dest = env->test_pd->shared_data->test_name;
    strncpy(test_name_dest, test->name, TEST_NAME_MAX);
    /* ensure string is null terminated */
    test_name_dest[TEST_NAME_MAX - 1] = '\0';
#ifdef CONFIG_DEBUG_BUILD
    pd_set_name(env->test_pd, test->name);
#endif

    /* start the process */
    error = cpu_start(env->test_cpu);

    ZF_LOGF_IF(error != 0, "Failed to start test process!");

    /* wait on it to finish or fault, report result */
    int result = sel4test_driver_wait(env, test);

    test_assert(result == SUCCESS);

    return result;
}

void osm_tear_down(uintptr_t e)
{
    int error;
    driver_env_t env = (driver_env_t)e;
    cpu_component_stop(env->test_cpu->id);
    error = pd_component_terminate(env->test_pd->id);
    assert(error == 0);
}

DEFINE_TEST_TYPE(OSM, OSM, NULL, NULL, osm_set_up, osm_tear_down, osm_run_test);