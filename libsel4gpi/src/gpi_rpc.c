#include <pb_encode.h>
#include <pb_decode.h>
#include <sel4nanopb/sel4nanopb.h>

#include <utils/zf_log.h>

#include <sel4gpi/gpi_rpc.h>

/**
 * @file
 * RPC functions for the client of a GPI server
 */

int sel4gpi_rpc_client_init(sel4gpi_rpc_client_t *client,
                            seL4_CPtr server_ep,
                            pb_msgdesc_t request_desc,
                            pb_msgdesc_t reply_desc)
{
    client->server_ep = server_ep;
    client->request_desc = request_desc;
    client->reply_desc = reply_desc;
    return 0;
}

int sel4gpi_rpc_server_init(sel4gpi_rpc_server_t *server,
                            pb_msgdesc_t request_desc,
                            pb_msgdesc_t reply_desc)
{
    server->request_desc = request_desc;
    server->reply_desc = reply_desc;
    return 0;
}

int sel4gpi_rpc_call_ep(sel4gpi_rpc_client_t *client, seL4_CPtr ep, void *msg,
                        int n_caps, seL4_CPtr *caps, void *reply)
{
    pb_ostream_t stream = pb_ostream_from_IPC(0);
    bool ret = pb_encode_delimited(&stream, &client->request_desc, msg);
    if (!ret)
    {
        ZF_LOGE("sel4gpi_rpc: Failed to encode message (%s)", PB_GET_ERROR(&stream));
        return -1;
    }

    size_t stream_size = stream.bytes_written / sizeof(seL4_Word);
    /* add an extra word if bytes_written is not divisible by sizeof(seL4_Word). */
    if (stream.bytes_written % sizeof(seL4_Word))
    {
        stream_size += 1;
    }

    /* set the caps to send */
    for (int i = 0; i < n_caps; i++)
    {
        seL4_SetCap(i, caps[i]);
    }

    /* make the call */
    seL4_Call(ep, seL4_MessageInfo_new(0, 0, n_caps, stream_size));

    pb_istream_t istream = pb_istream_from_IPC(0);
    ret = pb_decode_delimited(&istream, &client->reply_desc, reply);
    if (!ret)
    {
        ZF_LOGE("sel4gpi_rpc: Failed to decode server reply (%s)", PB_GET_ERROR(&stream));
        return -1;
    }

    return 0;
}

int sel4gpi_rpc_call(sel4gpi_rpc_client_t *client, void *msg, int n_caps, seL4_CPtr *caps, void *reply)
{
    return sel4gpi_rpc_call_ep(client, client->server_ep, msg, n_caps, caps, reply);
}

int sel4gpi_rpc_recv(sel4gpi_rpc_server_t *server, void *res)
{
    pb_istream_t stream = pb_istream_from_IPC(0);
    bool ret = pb_decode_delimited(&stream, &server->request_desc, res);
    if (!ret)
    {
        ZF_LOGE("Invalid protobuf stream (%s)", PB_GET_ERROR(&stream));
        return -1;
    }

    return 0;
}

int sel4gpi_rpc_reply(sel4gpi_rpc_server_t *server, void *msg, seL4_MessageInfo_t *msg_info)
{
    pb_ostream_t ostream = pb_ostream_from_IPC(0);

    bool ret = pb_encode_delimited(&ostream, &server->reply_desc, msg);
    if (!ret)
    {
        ZF_LOGE("Failed to encode reply (%s)", PB_GET_ERROR(&ostream));
        return -1;
    }

    size_t size = ostream.bytes_written / sizeof(seL4_Word);
    if (ostream.bytes_written % sizeof(seL4_Word))
    {
        size++;
    }

    *msg_info = seL4_MessageInfo_new(0, 0, 0, size);

    return 0;
}