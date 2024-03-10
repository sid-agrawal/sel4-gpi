#pragma once

#include <sys/types.h>
#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/mo_component.h>

typedef struct _mo_client_context
{
   cspacepath_t badged_server_ep_cspath;
   // cspacepath_t public_server_ep_cspath;
   uint64_t id; // Needed only for RR dump
} mo_client_context_t;

/**
 * @brief   Initialize the MO client.
 *
 * @param server_ep_cap Well known server endpoint cap.
 * @param free_slot allocated cap slot for the MO
 * @param num_pages number of pages in MO
 * @param ret_conn client's connection object
 * @return int 0 on success, -1 on failure.
 */
int mo_component_client_connect(seL4_CPtr server_ep_cap,
                                seL4_CPtr free_slot,
                                seL4_Word num_pages,
                                mo_client_context_t *ret_conn);

/**
 * @brief   Disconnect the MO client.
 *
 * @param conn
 * @return int 0 on success, -1 on failure.
 */
int mo_component_client_disconnect(mo_client_context_t *conn);
