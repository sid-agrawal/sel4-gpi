
#include <stdio.h>

#include <vka/capops.h>

#include <sel4test/test.h>
#include <sel4test/macros.h>
#include <sel4utils/thread.h>
#include <sel4gpi/debug.h>
#include "../test.h"
#include "../helpers.h"
#include "test_shared.h"
#include <sel4bench/arch/sel4bench.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/pd_utils.h>

#define SDDFBLK_APP "sddf_blk_server"


int test_blk(env_t env)
{
    seL4_Error error = 0;
    ZF_LOGI("############    In OSMOSIS TEST_BLK, BLK001   #################\n");
    
    // int start_ramdisk_pd(pd_client_context_t *ramdisk_pd,
    //                  gpi_obj_id_t *ramdisk_id)
    // {
    //     int error;
    //     error = start_resource_server_pd(GPICAP_TYPE_NONE, 0, RAMDISK_APP,
    //                                     ramdisk_pd, ramdisk_id);
    //     CHECK_ERROR(error, "failed to start ramdisk server\n");
    //     RAMDISK_PRINTF("Successfully started ramdisk server, resource space ID is %d\n",
    //                 (int)*ramdisk_id);
    //     return 0;
    // }

    printf("------------------STARTING SETUP: %s------------------\n", __func__);
    // start virt as a server
    int error;
    gpi_space_id_t sddf_blk_id;
    pd_client_context_t sddf_blk_pd;
    error = start_resource_server_pd(GPICAP_TYPE_NONE, 0, SDDFBLK_APP,
                                    sddf_blk_pd, sddf_blk_id);
    ZF_LOGI("error, %d", error);



    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(BLK001, "Ensure that the sddf blk is functioning", test_blk, true)