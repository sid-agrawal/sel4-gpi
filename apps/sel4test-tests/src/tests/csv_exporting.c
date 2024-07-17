/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4test/test.h>
#include <sel4test/macros.h>
#include <sel4utils/thread.h>
#include "../test.h"
#include "../helpers.h"
#include <stdio.h>

#include <sel4gpi/debug.h>
#include <sel4gpi/model_exporting.h>

#include <sel4bench/arch/sel4bench.h>

char *csv_buffer =
    "NODE_TYPE,NODE_ID,DATA,EDGE_TYPE,EDGE_FROM,EDGE_TO,EXTRA\n"
    "PD,PD_0,ROOT_TASK,,,,\n"
    "PD,PD_1,Proc1,,,,\n"
    "PD,PD_2,Proc2,,,,\n"
    "RESOURCE,MO_1_1,MO,,,,\n"
    "RESOURCE,MO_1_2,MO,,,,\n"
    "RESOURCE,VMR_1_1,VMR,,,,\n"
    "RESOURCE,VMR_1_2,VMR,,,,\n"
    ",,,HOLD,PD_1,MO_1_1,\n"
    ",,,HOLD,PD_2,MO_1_2,\n"
    ",,,HOLD,PD_0,VMR_1_1,\n"
    ",,,HOLD,PD_0,VMR_1_2,\n"
    ",,,MAP,VMR_1_1,MO_1_1,\n"
    ",,,MAP,VMR_1_2,MO_1_2,\n"
    ",,MO,REQUEST,PD_1,PD_0,\n"
    ",,MO,REQUEST,PD_2,PD_0,\n";

int test_model_state_export(env_t env)
{
    model_state_t model_state_static;
    model_state_t *model_state = &model_state_static;

    init_model_state(model_state, NULL, 0);

    char output_buffer[10000];

    // printf("%s\n", output_buffer);

    // Add PD nodes
    gpi_model_node_t *root_node = get_root_node(model_state);
    gpi_model_node_t *pd1 = add_pd_node(model_state, "Proc1", 1, true);
    gpi_model_node_t *pd2 = add_pd_node(model_state, "Proc2", 2, true);

    // Add resource nodes
    int mo_space_id = 1;
    int pmr_space_id = 1;
    gpi_model_node_t *mo1 = add_resource_node(model_state, make_res_id(GPICAP_TYPE_MO, mo_space_id, 1), true);
    gpi_model_node_t *mo2 = add_resource_node(model_state, make_res_id(GPICAP_TYPE_MO, mo_space_id, 2), true);
    gpi_model_node_t *page1 = add_resource_node(model_state, make_res_id(GPICAP_TYPE_VMR, pmr_space_id, 1), true);
    gpi_model_node_t *page2 = add_resource_node(model_state, make_res_id(GPICAP_TYPE_VMR, pmr_space_id, 2), true);

    // Add hold edges
    add_edge(model_state, GPI_EDGE_TYPE_HOLD, pd1, mo1);
    add_edge(model_state, GPI_EDGE_TYPE_HOLD, pd2, mo2);
    add_edge(model_state, GPI_EDGE_TYPE_HOLD, root_node, page1);
    add_edge(model_state, GPI_EDGE_TYPE_HOLD, root_node, page2);

    // Add map edges
    add_edge(model_state, GPI_EDGE_TYPE_MAP, page1, mo1);
    add_edge(model_state, GPI_EDGE_TYPE_MAP, page2, mo2);

    // Add request edges
    add_request_edge(model_state, pd1, root_node, GPICAP_TYPE_MO);
    add_request_edge(model_state, pd2, root_node, GPICAP_TYPE_MO);

    // Add some duplicate nodes/edges
    pd1 = add_pd_node(model_state, "Proc1", 1, true);
    add_edge(model_state, GPI_EDGE_TYPE_MAP, page2, mo2);
    add_request_edge(model_state, pd1, root_node, GPICAP_TYPE_MO);

    export_model_state(model_state, output_buffer, sizeof(output_buffer));
    if (strncmp(output_buffer, csv_buffer, sizeof(output_buffer)) != 0)
    {

        printf("Expected:\n%s\n", csv_buffer);
        printf("Got:\n%s\n", output_buffer);
        ZF_LOGF("CSV output does not match expected output");
    }

    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIMS001, "Test construction and exporting of model state", test_model_state_export, true)