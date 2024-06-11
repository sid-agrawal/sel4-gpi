#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4gpi/pd_utils.h>
#include <sel4utils/process.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/error_handle.h>

#include <sel4runtime.h>

#include <kvstore_shared.h>
#include <kvstore_client.h>
#include <kvstore_server.h>
#include <fs_client.h>

static kvstore_mode_t mode;
static seL4_CPtr server_ep;
static ads_client_context_t kvserv_ads;
static ads_client_context_t client_ads_conn;
static cpu_client_context_t self_cpu_conn;
extern global_xv6fs_client_context_t xv6fs_client;

int kvstore_client_swap_ads_lib(void)
{
    int error = 0;
    error = cpu_client_change_vspace(&self_cpu_conn, &kvserv_ads);
    if (error)
    {
        ZF_LOGE("failed to swap ADS to kvstore server");
        return error;
    }
    return error;
}

int kvstore_client_swap_ads_app(void)
{
    int error = 0;
    error = cpu_client_change_vspace(&self_cpu_conn, &client_ads_conn);
    if (error)
    {
        ZF_LOGE("failed to swap ADS to app");
        return error;
    }
    return error;
}

static int configure_separate_ads()
{
    int error = 0;

    /* new ADS*/

    seL4_CPtr free_slot;
    sel4gpi_runnable_t runnable = {0};

    seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
    GOTO_IF_COND(ads_rde == seL4_CapNull, "Can't make new ADS, no ADS RDE\n");

    pd_client_context_t self_pd_conn = sel4gpi_get_pd_conn();
    error = pd_client_next_slot(&self_pd_conn, &free_slot);
    GOTO_IF_ERR(error, "failed to allocate next slot");

    error = ads_component_client_connect(ads_rde, free_slot, &runnable.ads);
    GOTO_IF_ERR(error, "failed to allocate a new ADS");

    ads_config_t other_ads_cfg = {
        .code_shared = GPI_COPY,
        .stack_shared = GPI_SHARED,
        .stack_pages = DEFAULT_STACK_PAGES,
    };

    linked_list_t other_vmr_cfg = {0};
    int n_cfgs = 0;
    vmr_config_t heap_cfg = {
        .start = (void *)PD_HEAP_LOC,
        .region_pages = DEFAULT_HEAP_PAGES,
        .type = SEL4UTILS_RES_TYPE_HEAP,
        .share_mode = GPI_COPY};
    n_cfgs++;

    vmr_config_t osm_shared_data_cfg = {
        .start = sel4runtime_get_osm_shared_data(),
        .region_pages = 1,
        .type = SEL4UTILS_RES_TYPE_GENERIC,
        .share_mode = GPI_SHARED};
    n_cfgs++;

    vmr_config_t code_reg_cfg = {
        .start = 0,
        .region_pages = 0,
        .type = SEL4UTILS_RES_TYPE_CODE,
        .share_mode = GPI_SHARED};
    n_cfgs++;

    vmr_config_t data_reg_cfg = {
        .start = 0,
        .region_pages = 0,
        .type = SEL4UTILS_RES_TYPE_DATA,
        .share_mode = GPI_COPY};
    n_cfgs++;

    vmr_config_t stack_reg_cfg = {
        .start = 0,
        .region_pages = 0,
        .type = SEL4UTILS_RES_TYPE_STACK,
        .share_mode = GPI_SHARED};
    n_cfgs++;

    vmr_config_t ipc_buf_cfg = {
        .start = 0,
        .region_pages = 0,
        .type = SEL4UTILS_RES_TYPE_IPC_BUF,
        .share_mode = GPI_SHARED};
    n_cfgs++;

    linked_list_insert_many(&other_vmr_cfg, n_cfgs,
                            &code_reg_cfg, &data_reg_cfg, &heap_cfg,
                            &osm_shared_data_cfg, &stack_reg_cfg, &ipc_buf_cfg);

    other_ads_cfg.vmr_cfgs = &other_vmr_cfg;

    error = sel4gpi_ads_configure(&other_ads_cfg, &runnable, NULL, NULL, NULL, NULL, NULL, NULL);
    GOTO_IF_ERR(error, "Failed to configure other ADS\n");
    kvserv_ads = runnable.ads;

    linked_list_destroy(&other_vmr_cfg);

    self_cpu_conn = sel4gpi_get_cpu_conn();
    error = cpu_client_change_vspace(&self_cpu_conn, &kvserv_ads);
    GOTO_IF_ERR(error, "failed to swap ADS to kvstore server\n");

    // we need to clear the FS client instance, so the lib starts with a fresh one
    memset(&xv6fs_client, 0, sizeof(global_xv6fs_client_context_t));
    error = kvstore_server_init();
    GOTO_IF_ERR(error, "Failed to initialize kvstore\n");

    // need to set this variable twice since we're in a different vspace
    client_ads_conn = sel4gpi_get_ads_conn();
    self_cpu_conn = sel4gpi_get_cpu_conn();
    error = cpu_client_change_vspace(&self_cpu_conn, &client_ads_conn);
    FATAL_IF_ERR(error, "Failed to switch back to client ADS"); // fatal because we can't continue in the wrong ADS

err_goto:
    return error;
}

int kvstore_client_configure(kvstore_mode_t kvstore_mode, seL4_CPtr ep)
{
    int error = 0;
    mode = kvstore_mode;

    switch (kvstore_mode)
    {
    case SAME_THREAD:
        // This PD will be a local kvstore, initialize
        error = kvstore_server_init();
        break;
    case SEPARATE_ADS:
        error = configure_separate_ads();
        break;
    case SEPARATE_THREAD:
        error = kvstore_server_start_thread(&server_ep);
        break;
    case SEPARATE_PROC:
        // This PD will send kvstore requests to another PD
        server_ep = ep;
        break;
    }

    return error;
}

int kvstore_client_set(seL4_Word key, seL4_Word value)
{
    seL4_Error error;

    if (mode == SEPARATE_PROC || mode == SEPARATE_THREAD)
    {
        /* Send IPC to server */
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, KVMSGREG_SET_REQ_END);
        seL4_SetMR(KVMSGREG_FUNC, KV_FUNC_SET_REQ);
        seL4_SetMR(KVMSGREG_SET_REQ_KEY, key);
        seL4_SetMR(KVMSGREG_SET_REQ_VAL, value);
        tag = seL4_Call(server_ep, tag);
        error = seL4_MessageInfo_get_label(tag);
    }
    else if (mode == SEPARATE_ADS)
    {
        error = cpu_client_change_vspace(&self_cpu_conn, &kvserv_ads);
        if (error)
        {
            ZF_LOGE("failed to swap ADS to kvstore server");
            return error;
        }

        error = kvstore_server_set(key, value);
        ZF_LOGE_IF(error, "kvstore_server_set failed");

        // don't overwrite the error value from the actual server command
        int swap_err = cpu_client_change_vspace(&self_cpu_conn, &client_ads_conn);
        ZF_LOGF_IF(swap_err, "Failed to swap back to client ADS"); // fatal because we can't continue in the wrong ADS
    }
    else
    {
        error = kvstore_server_set(key, value);
    }

    return error;
}

int kvstore_client_get(seL4_Word key, seL4_Word *value)
{
    seL4_Error error;

    if (mode == SEPARATE_PROC || mode == SEPARATE_THREAD)
    {
        /* Send IPC to server */
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, KVMSGREG_GET_REQ_END);
        seL4_SetMR(KVMSGREG_FUNC, KV_FUNC_GET_REQ);
        seL4_SetMR(KVMSGREG_GET_REQ_KEY, key);
        tag = seL4_Call(server_ep, tag);
        error = seL4_MessageInfo_get_label(tag);

        if (error == 0)
        {
            *value = seL4_GetMR(KVMSGREG_GET_ACK_VAL);
        }
    }
    else if (mode == SEPARATE_ADS)
    {
        error = cpu_client_change_vspace(&self_cpu_conn, &kvserv_ads);
        if (error)
        {
            ZF_LOGE("failed to swap ADS to kvstore server");
            return error;
        }
        error = kvstore_server_get(key, value);
        ZF_LOGE_IF(error, "kvstore_server_get failed");

        // don't overwrite the error value from the actual server command
        int swap_err = cpu_client_change_vspace(&self_cpu_conn, &client_ads_conn);
        ZF_LOGF_IF(swap_err, "Failed to swap back to client ADS"); // fatal because we can't continue in the wrong ADS
    }
    else
    {
        error = kvstore_server_get(key, value);
    }

    return error;
}
