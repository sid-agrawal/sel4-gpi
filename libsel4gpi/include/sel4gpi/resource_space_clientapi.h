
#include <stdint.h>
#include <sel4/sel4.h>

#include <sel4gpi/gpi_client.h>
#include <sel4gpi/resource_space_component.h>

// (XXX) Arya: Eventually this NSID should be removed
#define NSID_DEFAULT 1 // Default namespace ID

/**
 * Allocate a new resource space
 *
 * @param server_ep endpoint of the resource space component
 * @param free_slot slot to store the resource space capability in
 * @param resource_type resource type of the new resource space
 * @param client_ep endpoint of the server that will manager the resource space
 * @param ret_conn returns the initialized resource space client connection
 */
int resspc_client_connect(seL4_CPtr server_ep,
                          seL4_CPtr free_slot,
                          gpi_cap_t resource_type,
                          seL4_CPtr client_ep,
                          resspc_client_context_t *ret_conn);