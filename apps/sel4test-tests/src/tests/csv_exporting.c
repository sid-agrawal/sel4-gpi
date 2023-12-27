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

#include<sel4gpi/debug.h>
#include<sel4gpi/model_exporting.h>

#include <sel4bench/arch/sel4bench.h>

char *csv_buffer =
"RESOURCE_FROM,RESOURCE_TO,RES_TYPE,RES_ID,PD_NAME,PD_FROM,PD_TO,PD_ID,IS_MAPPED\n"
",V.2,,,,PD.2.0,,,FALSE\n"
",V.1,,,,PD.1.0,,,TRUE\n"
",,,,,PD.1.0,PD.0.0,,\n"
",,,,,PD.2.0,PD.0.0,,\n"
"V.1,P.1,,,,,,,\n"
"V.2,P.2,,,,,,,\n"
",,,,Proc2,,,PD.2.0,\n"
",,,,Proc1,,,PD.1.0,\n"
",,,,OS_0,,,PD.0.0,\n"
",,Virtual,V.2,,,,,\n"
",,Virtual,V.1,,,,,\n";


int test_model_state_export(env_t env)
{
    model_state_t model_state;
    init_model_state(&model_state);

    char output_buffer[10000];

    // printf("%s\n", output_buffer);

    add_resource(&model_state, "Virtual", "V.1");
    add_resource(&model_state, "Virtual", "V.2");

    add_pd(&model_state, "OS_0", "PD.0.0");
    add_pd(&model_state, "Proc1", "PD.1.0");
    add_pd(&model_state, "Proc2", "PD.2.0");

    add_resource_depends_on(&model_state, "V.2", "P.2");
    add_resource_depends_on(&model_state, "V.1", "P.1");

    add_pd_requestes(&model_state, "PD.2.0", "PD.0.0");
    add_pd_requestes(&model_state, "PD.1.0", "PD.0.0");

    add_has_access_to(&model_state, "PD.1.0", "V.1", true);
    add_has_access_to(&model_state, "PD.2.0", "V.2", false);

    export_model_state(&model_state, output_buffer, sizeof(output_buffer));
    if (strncmp(output_buffer, csv_buffer, sizeof(output_buffer)) != 0) {

        printf("Expected:\n%s\n", csv_buffer);
        printf("Got:\n%s\n", output_buffer);
        ZF_LOGF("CSV output does not match expected output");
    }

    return sel4test_get_result();
}

DEFINE_TEST(GPIMS001, "Test construction and exporting of model state", test_model_state_export, true)