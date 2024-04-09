#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4gpi/pd_utils.h>
#include <sel4utils/process.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>

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
    int error;
    int swap_err;

    seL4_CPtr self_pd_cap = sel4gpi_get_pd_cap();
    pd_client_context_t self_pd_conn = {.badged_server_ep_cspath.capPtr = self_pd_cap};
    seL4_CPtr slot;
    pd_client_next_slot(&self_pd_conn, &slot);

    seL4_CPtr ads_rde = sel4gpi_get_rde_by_ns_id(NSID_DEFAULT, GPICAP_TYPE_ADS);
    ads_client_context_t self_ads_conn = {.badged_server_ep_cspath.capPtr = sel4gpi_get_ads_cap()};
    swap_err = ads_client_shallow_copy(&self_ads_conn, slot, NULL, &kvserv_ads);
    if (swap_err)
    {
        ZF_LOGE("failed to make a new ADS for kvstore server");
        return swap_err;
    }

    self_cpu_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_cpu_cap();
    swap_err = cpu_client_change_vspace(&self_cpu_conn, &kvserv_ads);
    if (swap_err)
    {
        ZF_LOGE("failed to swap ADS to kvstore server");
        return swap_err;
    }

    // we need to clear the FS client instance, so the lib starts with a fresh one
    memset(&xv6fs_client, 0, sizeof(global_xv6fs_client_context_t));
    error = kvstore_server_init();
    ZF_LOGE_IF(error, "Failed to initialize kvstore");

    client_ads_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_ads_cap();
    // need to set this variable twice since we're in a different vspace
    self_cpu_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_cpu_cap();
    swap_err = cpu_client_change_vspace(&self_cpu_conn, &client_ads_conn);
    ZF_LOGF_IF(swap_err, "Failed to switch back to client ADS"); // fatal because we can't continue in the wrong ADS

    return swap_err;
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
