#pragma once

#include <sel4/sel4.h>

#include <pb_common.h>

/**
 * @file
 * Functions for sending/receiving RPC messages with protobuf
 */

typedef struct sel4gpi_rpc_env
{
    pb_msgdesc_t *request_desc; ///< Message description for this RPC protocol's requests
    pb_msgdesc_t *reply_desc;   ///< Message description for this RPC protocol's replies
} sel4gpi_rpc_env_t;

/**
 * Initialize the client or server side of an RPC protocol
 *
 * @param client the structure to initialize
 * @param request_desc generated message description for this RPC protocol's requests
 * @param reply_desc generated message description for this RPC protocol's replies
 */
int sel4gpi_rpc_env_init(sel4gpi_rpc_env_t *client,
                         pb_msgdesc_t *request_desc,
                         pb_msgdesc_t *reply_desc);

/**
 * Send a message to an RPC server
 *
 * @param client the client structure, initialized by sel4gpi_rpc_env_init
 * @param ep the endpoint to send to
 * @param msg the message to send, the type should correspond with the request_desc
 * @param n_caps number of caps to send
 * @param caps an array of capabilities to send
 * @param reply returns the reply message, the type should correspond with the reply_desc
 * @return 0 on success, -1 if there was an error encoding the message / decoding the reply
 */
int sel4gpi_rpc_call(sel4gpi_rpc_env_t *client, seL4_CPtr ep, void *msg,
                     int n_caps, seL4_CPtr *caps, void *reply);

/**
 * To be called by an RPC server, parse an RPC message from the IPC buffer
 *
 * @param env the env structure, initialized by sel4gpi_rpc_env_init
 * @param res a message structure to be filled out, the type will correspond with the request_desc
 * @return 0 on success, -1 if there was an error decoding the message
 */
int sel4gpi_rpc_recv(sel4gpi_rpc_env_t *env, void *res);

/**
 * To be called by an RPC client, parse an RPC reply from the IPC buffer
 * Note: not to be used with sel4gpi_rpc_call, only to be used to individually parse a reply
 *
 * @param env the env structure, initialized by sel4gpi_rpc_env_init
 * @param res a message structure to be filled out, the type will correspond with the request_desc
 * @return 0 on success, -1 if there was an error decoding the message
 */
int sel4gpi_rpc_recv_reply(sel4gpi_rpc_env_t *env, void *res);

/**
 * To be called by an RPC server, write an RPC reply to the IPC buffer
 * Note: You will still need to call seL4_Send with the provided message tag after this
 *
 * @param env the env structure, initialized by sel4gpi_rpc_env_init
 * @param msg a message structure to send, the type should correspond with the reply_desc
 * @param msg_info returns the MessageInfo to send by IPC
 * @return 0 on success, -1 if there was an error decoding the message
 */
int sel4gpi_rpc_reply(sel4gpi_rpc_env_t *env, void *msg, seL4_MessageInfo_t *msg_info);
