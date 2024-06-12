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
#include <sel4gpi/gpi_client.h>

/**
 * @brief   Initialize the MO client.
 *
 * @param server_ep_cap Well known server endpoint cap.
 * @param num_pages number of pages in MO
 * @param ret_conn client's connection object
 * @return int 0 on success, -1 on failure.
 */
int mo_component_client_connect(seL4_CPtr server_ep_cap,
                                seL4_Word num_pages,
                                mo_client_context_t *ret_conn);

/**
 * @brief   Disconnect the MO client.
 *
 * @param conn
 * @return int 0 on success, -1 on failure.
 */
int mo_component_client_disconnect(mo_client_context_t *conn);
