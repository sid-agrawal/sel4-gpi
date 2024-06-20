#pragma once

#include <sel4/sel4.h>

#include <pb_common.h>

/**
 * @file
 * RPC functions for the client of a GPI server
 */

typedef struct sel4gpi_rpc_client_env
{
    seL4_CPtr server_ep;       ///< Endpoint of the RPC server
    pb_msgdesc_t request_desc; ///< Message description for this RPC protocol's requests
    pb_msgdesc_t reply_desc;   ///< Message description for this RPC protocol's replies
} sel4gpi_rpc_client_t;

typedef struct sel4gpi_rpc_server_env
{
    pb_msgdesc_t request_desc; ///< Message description for this RPC protocol's requests
    pb_msgdesc_t reply_desc;   ///< Message description for this RPC protocol's replies
} sel4gpi_rpc_server_t;

/**
 * Initialize the client side of an RPC protocol
 *
 * @param client the client structure to initialize
 * @param server_ep the endpoint of the RPC server
 * @param request_desc generated message description for this RPC protocol's requests
 * @param reply_desc generated message description for this RPC protocol's replies
 */
int sel4gpi_rpc_client_init(sel4gpi_rpc_client_t *client,
                            seL4_CPtr server_ep,
                            pb_msgdesc_t request_desc,
                            pb_msgdesc_t reply_desc);

/**
 * Initialize the server side of an RPC protocol
 *
 * @param server the server structure to initialize
 * @param request_desc generated message description for this RPC protocol's requests
 * @param reply_desc generated message description for this RPC protocol's replies
 */
int sel4gpi_rpc_server_init(sel4gpi_rpc_server_t *server,
                            pb_msgdesc_t request_desc,
                            pb_msgdesc_t reply_desc);

/**
 * Send a message to an RPC server
 *
 * @param client the client structure, initialized by sel4gpi_rpc_client_init
 * @param msg the message to send, the type should correspond with the request_desc
 * @param n_caps number of caps to send
 * @param caps an array of capabilities to send
 * @param reply returns the reply message, the type should correspond with the reply_desc
 * @return 0 on success, -1 if there was an error encoding the message / decoding the reply
 */
int sel4gpi_rpc_call(sel4gpi_rpc_client_t *client, void *msg, int n_caps, seL4_CPtr *caps, void *reply);

/**
 * Send a message to an RPC server using a particular endpoint
 *
 * @param client the client structure, initialized by sel4gpi_rpc_client_init
 * @param ep the endpoint to send to (usually a particular badged version of the server EP)
 * @param msg the message to send, the type should correspond with the request_desc
 * @param n_caps number of caps to send
 * @param caps an array of capabilities to send
 * @param reply returns the reply message, the type should correspond with the reply_desc
 * @return 0 on success, -1 if there was an error encoding the message / decoding the reply
 */
int sel4gpi_rpc_call_ep(sel4gpi_rpc_client_t *client, seL4_CPtr ep, void *msg,
                        int n_caps, seL4_CPtr *caps, void *reply);

/**
 * To be called by an RPC server, parse an RPC message from the IPC buffer
 *
 * @param server the server structure, initialized by sel4gpi_rpc_server_init
 * @param res a message structure to be filled out, the type will correspond with the request_desc
 * @return 0 on success, -1 if there was an error decoding the message
 */
int sel4gpi_rpc_recv(sel4gpi_rpc_server_t *server, void *res);

/**
 * To be called by an RPC server, write an RPC reply to the IPC buffer
 * Note: You will still need to call seL4_Send with the provided message tag after this
 *
 * @param server the server structure, initialized by sel4gpi_rpc_server_init
 * @param msg a message structure to send, the type should correspond with the reply_desc
 * @param msg_info returns the MessageInfo to send by IPC
 * @return 0 on success, -1 if there was an error decoding the message
 */
int sel4gpi_rpc_reply(sel4gpi_rpc_server_t *server, void *msg, seL4_MessageInfo_t *msg_info);

// (XXX) Arya: Will need a way to call on a badged endpoint
