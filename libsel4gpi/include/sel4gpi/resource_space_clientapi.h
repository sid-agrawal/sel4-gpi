
#include <stdint.h>
#include <sel4/sel4.h>

#include <sel4gpi/gpi_client.h>
#include <sel4gpi/resource_space_component.h>
#include <sel4gpi/endpoint_clientapi.h>

/**
 * Allocate a new resource space
 *
 * @param server_ep endpoint of the resource space component
 * @param resource_type name of the resource type of the new resource space
 * @param resource_server_ep tracked endpoint of the server that will manage the resource space,
 *                           the RESSPC component will never distribute this endpoint as a regular one,
 *                           only as a badged resource endpoint
 * @param client_id PD ID of the client that should receive an RDE for the new space
 * @param ret_conn returns the initialized resource space client connection
 * @return 0 on success, error otherwise
 */
int resspc_client_connect(seL4_CPtr server_ep,
                          char *resource_type,
                          ep_client_context_t *resource_server_ep,
                          seL4_CPtr client_id,
                          resspc_client_context_t *ret_conn);

/**
 * Add a map connection from one resource space to another
 * This allows us to map a resource in the first space to a resource in the second space
 *
 * @param conn the resource space connection
 * @param space_id unique id of the resource space to map this space to
 * @return 0 on success, error otherwise
 */
int resspc_client_map_space(resspc_client_context_t *conn,
                            seL4_Word space_id);

/**
 * Create a new resource in a resource space
 *
 * @param conn the resource space connection
 * @param resource_id unique id of the new resource within the resource space
 * @return 0 on success, error otherwise
 */
int resspc_client_create_resource(resspc_client_context_t *conn,
                                  seL4_Word resource_id);