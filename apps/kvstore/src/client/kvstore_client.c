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

static bool use_remote_server;
static seL4_CPtr server_ep;
ads_client_context_t kvserv_ads;

int kvstore_client_configure(bool n_use_remote_server, bool separate_ads, seL4_CPtr ep)
{
    int error = 0;
    use_remote_server = n_use_remote_server;

    if (use_remote_server)
    {
        // This PD will send kvstore requests to another PD
        server_ep = ep;
    }
    else
    {
        // This PD will be a local kvstore, initialize
        // make new ADS, swap here
        seL4_CPtr self_pd_cap = sel4gpi_get_pd_cap();
        pd_client_context_t self_pd_conn = {.badged_server_ep_cspath.capPtr = self_pd_cap};
        seL4_CPtr slot;
        pd_client_next_slot(&self_pd_conn, &slot);

        seL4_CPtr ads_rde = sel4gpi_get_rde_by_ns_id(NSID_DEFAULT, GPICAP_TYPE_ADS);
        ads_client_context_t self_ads_conn = {.badged_server_ep_cspath.capPtr = sel4gpi_get_ads_cap()};
        error = ads_client_shallow_copy(&self_ads_conn, slot, NULL, &kvserv_ads);
        if (error)
        {
            ZF_LOGE("failed to make a new ADS for kvstore server\n");
            return error;
        }

        cpu_client_context_t self_cpu_conn = {.badged_server_ep_cspath.capPtr = sel4gpi_get_cpu_cap()};
        error = cpu_client_change_vspace(&self_cpu_conn, &kvserv_ads);
        if (error)
        {
            ZF_LOGE("failed to swap ADS to kvstore server\n");
            return error;
        }

        // error = kvstore_server_init();
    }

    return error;
}

int kvstore_client_set(seL4_Word key, seL4_Word value)
{
    seL4_Error error;

    if (use_remote_server)
    {
        /* Send IPC to server */
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, KVMSGREG_SET_REQ_END);
        seL4_SetMR(KVMSGREG_FUNC, KV_FUNC_SET_REQ);
        seL4_SetMR(KVMSGREG_SET_REQ_KEY, key);
        seL4_SetMR(KVMSGREG_SET_REQ_VAL, value);
        tag = seL4_Call(server_ep, tag);
        error = seL4_MessageInfo_get_label(tag);
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

    if (use_remote_server)
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
    else
    {
        error = kvstore_server_get(key, value);
    }

    return error;
}
