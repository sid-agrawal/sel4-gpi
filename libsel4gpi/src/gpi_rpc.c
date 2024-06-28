#include <pb_encode.h>
#include <pb_decode.h>
#include <sel4nanopb/sel4nanopb.h>

#include <utils/zf_log.h>

#include <sel4gpi/gpi_rpc.h>

/**
 * @file
 * Functions for sending/receiving RPC messages
 */

int sel4gpi_rpc_env_init(sel4gpi_rpc_env_t *env,
                         pb_msgdesc_t *request_desc,
                         pb_msgdesc_t *reply_desc)
{
    env->request_desc = request_desc;
    env->reply_desc = reply_desc;
    return 0;
}

int sel4gpi_rpc_call(sel4gpi_rpc_env_t *env, seL4_CPtr ep, void *msg,
                     int n_caps, seL4_CPtr *caps, void *reply)
{
    pb_ostream_t stream = pb_ostream_from_IPC(0);
    // Check not overwriting caps
    bool ret = pb_encode_delimited(&stream, env->request_desc, msg);
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
    ret = pb_decode_delimited(&istream, env->reply_desc, reply);
    if (!ret)
    {
        ZF_LOGE("sel4gpi_rpc: Failed to decode server reply (%s)", PB_GET_ERROR(&stream));
        return -1;
    }

    return 0;
}

int sel4gpi_rpc_recv(sel4gpi_rpc_env_t *env, void *res)
{
    pb_istream_t stream = pb_istream_from_IPC(0);
    bool ret = pb_decode_delimited(&stream, env->request_desc, res);
    if (!ret)
    {
        ZF_LOGE("Invalid protobuf stream (%s)", PB_GET_ERROR(&stream));
        return -1;
    }

    return 0;
}

int sel4gpi_rpc_recv_reply(sel4gpi_rpc_env_t *env, void *res)
{
    pb_istream_t stream = pb_istream_from_IPC(0);
    bool ret = pb_decode_delimited(&stream, env->reply_desc, res);
    if (!ret)
    {
        ZF_LOGE("Invalid protobuf stream (%s)", PB_GET_ERROR(&stream));
        return -1;
    }

    return 0;
}

int sel4gpi_rpc_reply(sel4gpi_rpc_env_t *env, void *msg, seL4_MessageInfo_t *msg_info)
{
    pb_ostream_t ostream = pb_ostream_from_IPC(0);

    bool ret = pb_encode_delimited(&ostream, env->reply_desc, msg);
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