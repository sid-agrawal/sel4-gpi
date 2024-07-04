#pragma once

#include <sel4/sel4.h>
#include <pb_common.h>
#include <sel4gpi/resource_types.h>

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
 * To be called by an RPC server, write an RPC reply to the IPC buffer
 * Note: You will still need to call seL4_Send with the provided message tag after this
 *
 * @param env the env structure, initialized by sel4gpi_rpc_env_init
 * @param msg a message structure to send, the type should correspond with the reply_desc
 * @param msg_info returns the MessageInfo to send by IPC
 * @return 0 on success, -1 if there was an error decoding the message
 */
int sel4gpi_rpc_reply(sel4gpi_rpc_env_t *env, void *msg, seL4_MessageInfo_t *msg_info);

/**
 * Check if the first received cap has the given GPI type
 * 
 * @param type the expected type of the first cap
 * @return true if the received cap is the expected type, false otherwise
 */
bool sel4gpi_rpc_check_cap(gpi_cap_t type);

/**
 * Check if the first 2 received caps have the given GPI types
 * 
 * @param type1 the expected type of the first cap
 * @param type2 the expected type of the second cap
 * @return true if the received caps have the expected types, false otherwise
 */
bool sel4gpi_rpc_check_caps_2(gpi_cap_t type1, gpi_cap_t type2);

/**
 * Check if the first 2 received caps have the given GPI types
 * 
 * @param type1 the expected type of the first cap
 * @param type2 the expected type of the second cap
 * @param type2 the expected type of the third cap
 * @return true if the received caps have the expected types, false otherwise
 */
bool sel4gpi_rpc_check_caps_3(gpi_cap_t type1, gpi_cap_t type2, gpi_cap_t type3);

/**
 * Prints an RPC request to standard output
 * 
 * @param env the RPC env
 * @param msg the message to print
 */
void sel4gpi_rpc_print_request(sel4gpi_rpc_env_t *env, void *msg);