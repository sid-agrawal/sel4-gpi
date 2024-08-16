/**
 * @file API for allowing a thread to act as the parent to a ramdisk server
 * thread.
 *
 * Provides the APIs for spawning the server thread.
 */

#pragma once

#include <stdint.h>

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/resource_server_utils.h>

#include <sample_shared.h>

#define SAMPLE_SERVER_DEFAULT_PRIORITY (seL4_MaxPrio - 100)

/* Context of the server */
typedef struct _sample_server_context
{
    resource_server_context_t gen; ///< Generic resource server context
    resource_registry_t registry;  ///< Registry of sample resources
    /* INSERT HERE more server data */
} sample_server_context_t;

/* Node type for entries of the resource registry */
typedef struct _sample_resource_registry_entry
{
    resource_registry_node_t gen; ///< Generic resource registry node

    // Resource-specific data
    uint64_t x; ///< Dummy field
    /* INSERT HERE more resource fields, if needed */
} sample_resource_registry_entry_t;

/**
 * To be run once at the start of the sample server
 */
int sample_init();

/**
 * To handle client requests to the sample server
 *
 * @param msg_p pointer to the NanoPB RPC message request sent by the client
 * @param msg_reply_p pointer to the NanoPB RPC message reply
 *                    the request handler should fill this out, and it will be sent to the client
 * @param sender the sender's badge
 * @param cap the capability sent by the client, if there was one
 * @param need_new_recv_cap false by default, the handler should set this to true if a capability was received
 */
void sample_request_handler(void *msg_p,
                            void *msg_reply_p,
                            seL4_Word sender_badge,
                            seL4_CPtr cap,
                            bool *need_new_recv_cap);

/**
 * To handle root task requests to the sample server
 *
 * @param work the work message sent by the root task
 */
int sample_work_handler(PdWorkReturnMessage *work);

/**
 * Get the sample server's context in this address space
 */
sample_server_context_t *get_sample_server(void);