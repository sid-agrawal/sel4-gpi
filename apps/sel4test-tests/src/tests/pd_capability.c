/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4/sel4.h>
#include <sel4test/test.h>
#include <sel4test/macros.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/debug.h>

#include <vka/capops.h>

#include <sel4utils/thread.h>
#include <sel4gpi/debug.h>
#include "../test.h"
#include "../helpers.h"
#include <stdio.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/cpu_clientapi.h>
#include <sel4bench/arch/sel4bench.h>
#include <utils/uthash.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/pd_creation.h>
#include <sel4runtime.h>
#include "test_shared.h"

#include <sample_client.h>

#define TEST_LOG(msg, ...)                                  \
    do                                                      \
    {                                                       \
        printf("%s(): " msg "\n", __func__, ##__VA_ARGS__); \
    } while (0)

int test_new_process_osmosis_shmem(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    sel4gpi_runnable_t runnable = {0};
    pd_config_t *proc_cfg = sel4gpi_configure_process("hello", DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES, &runnable);
    test_assert(proc_cfg != NULL);

    ep_client_context_t ep_conn;
    error = sel4gpi_alloc_endpoint(&ep_conn);
    test_error_eq(error, 0);
    test_assert(ep_conn.raw_endpoint != seL4_CapNull);

    seL4_CPtr slot;
    error = pd_client_send_cap(&runnable.pd, ep_conn.ep, &slot);
    test_error_eq(error, 0);

    error = sel4gpi_prepare_pd(proc_cfg, &runnable, 1, &slot);
    test_error_eq(error, 0);

    error = sel4gpi_start_pd(&runnable);
    test_error_eq(error, 0);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    seL4_Recv(ep_conn.raw_endpoint, NULL);

    pd_client_context_t self_pd = sel4gpi_get_pd_conn();

    mo_client_context_t shared_mo;
    error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO), 1, MO_PAGE_BITS, &shared_mo);
    test_error_eq(error, 0);

    error = pd_client_send_cap(&runnable.pd, shared_mo.ep, &slot);
    test_error_eq(error, 0);

    // Print model state
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();
    extract_model(&pd_conn);

    seL4_SetMR(0, slot);
    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_ReplyRecv(ep_conn.raw_endpoint, tag, NULL);

    sel4gpi_config_destroy(proc_cfg);

    // Cleanup the PD
    // The PD exits by itself, no need to terminate
    // test_error_eq(maybe_terminate_pd(&runnable.pd), 0);

    return sel4test_get_result();
}

DEFINE_TEST_OSM(GPIPD001,
            "OSMO: Ensure that as new process works w/ SHMEM",
            test_new_process_osmosis_shmem,
            true)

int test_pd_dump(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    // Check that PD dump works on self, more than once
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();

    error = pd_client_dump(&pd_conn, NULL, 0);
    test_assert(error == 0);

    error = pd_client_dump(&pd_conn, NULL, 0);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}

DEFINE_TEST_OSM(GPIPD002, "Test PD dump", test_pd_dump, true)

int test_sample_resource_server(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);
    
    // Start the sample server
    pd_client_context_t sample_server_pd;
    gpi_space_id_t sample_space_id;
    error = start_sample_server_proc(&sample_server_pd, &sample_space_id);
    test_assert(error == 0);

    // Find the RDE
    seL4_CPtr sample_server_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(SAMPLE_RESOURCE_TYPE_NAME));

    // Allocate a sample resource
    sample_client_context_t conn;
    error = sample_client_alloc(sample_server_ep, &conn);
    test_assert(error == 0);

    // Invoke a sample resource
    uint64_t x = 42; // Dummy arguments
    uint64_t y = 24;
    char response[40];
    error = sample_client_invoke(&conn, x, y, response);
    test_assert(error == 0);
    test_assert(strcmp(response, "0x42") == 0);

    // Free a sample resource
    error = sample_client_free(&conn);
    test_assert(error == 0);

    // Check that the freed resource can't be used
    printf("The next line should print 'Attempted to invoke a null cap'\n");
    error = sample_client_invoke(&conn, x, y, response);
    test_assert(error == seL4_InvalidArgument); // Should be invalid arg, since the cap is null

    // Terminate the sample server
    error = pd_client_terminate(&sample_server_pd);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIPD003, "Test starting a sample resource server", test_sample_resource_server, true)

int test_send_resource(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);
    
    // Start the sample server
    pd_client_context_t sample_server_pd;
    gpi_space_id_t sample_space_id;
    error = start_sample_server_proc(&sample_server_pd, &sample_space_id);
    test_assert(error == 0);

    // Find the RDE
    seL4_CPtr sample_server_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(SAMPLE_RESOURCE_TYPE_NAME));

    // Allocate a sample resource
    sample_client_context_t sample_res;
    error = sample_client_alloc(sample_server_ep, &sample_res);
    test_assert(error == 0);

    // Allocate a core resource
    mo_client_context_t mo;
    error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO), 1, MO_PAGE_BITS, &mo);
    test_assert(error == 0);

    // Send the core resource
    error = pd_client_send_cap(&sample_server_pd, mo.ep, NULL);
    test_assert(error == 0);

    // Send the non-core resource
    error = pd_client_send_cap(&sample_server_pd, sample_res.ep, NULL);
    test_assert(error == 0);

    // Terminate the sample server
    error = pd_client_terminate(&sample_server_pd);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIPD004, "Test sending resources to a PD", test_send_resource, true)