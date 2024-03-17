#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>

#include <kvstore_shared.h>
#include <kvstore_client.h>
#include <kvstore_server.h>

static bool use_remote_server;
static seL4_CPtr server_ep;

int kvstore_client_configure(bool n_use_remote_server, seL4_CPtr ep)
{
    int error = 0;
    use_remote_server = n_use_remote_server;

    if (use_remote_server) {
        // This PD will send kvstore requests to another PD
        server_ep = ep;
    } else {
        // This PD will be a local kvstore, initialize
        error = kvstore_server_init();
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
