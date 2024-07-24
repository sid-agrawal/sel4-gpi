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
#include <sel4gpi/mo_client_context.h>

/**
 * @brief   Initialize the MO client.
 *
 * @param server_ep_cap Well known server endpoint cap.
 * @param num_pages number of pages in MO
 * @param page_bits size bits of an individual page
 * @param ret_conn[out] client's connection object
 * @return int 0 on success, 1 on failure.
 */
int mo_component_client_connect(seL4_CPtr server_ep_cap,
                                uint32_t num_pages,
                                size_t page_bits,
                                mo_client_context_t *ret_conn);

/**
 * @brief Allocate a new MO at the given physical address
 *
 * @param server_ep_cap the MO RDE
 * @param num_pages number of pages in MO
 * @param page_bits size bits of an individual page
 * @param paddr physical address to allocate at
 * @param ret_conn returns the new MO context
 * @return int 0 on success, 1 on failure
 */
int mo_component_client_connect_paddr(seL4_CPtr server_ep_cap,
                                      uint32_t num_pages,
                                      size_t page_bits,
                                      uintptr_t paddr,
                                      mo_client_context_t *ret_conn);

/**
 * @brief   Disconnect the MO client.
 *
 * @param conn
 * @return int 0 on success, -1 on failure.
 */
int mo_component_client_disconnect(mo_client_context_t *conn);
