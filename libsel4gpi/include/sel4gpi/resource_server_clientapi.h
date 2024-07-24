#include <stdint.h>

#include <sel4/sel4.h>
#include <sel4gpi/model_exporting.h>

/** @file
 * API for remote resource servers, which the RT or other PDs may use
 */

/**
 * Starts a resource server in a new PD
 * @param rde_type cap type of RDE to add, optional
 * @param rde_id space ID of RDE to add, optional
 * @param image_name name of the resource server's image
 * @param server_pd_cap returns the PD resource of the started server
 * @param space_id returns the default resource space ID of the started server
 */
int start_resource_server_pd(gpi_cap_t rde_type,
                             gpi_space_id_t rde_id,
                             char *image_name,
                             seL4_CPtr *server_pd_cap,
                             gpi_space_id_t *space_id);

/**
 * Starts a resource server in a new PD
 * Additionally, pass some seL4_Word arguments
 *
 * @param rde_type cap type of RDE to add, optional
 * @param rde_id space ID of RDE to add, optional
 * @param image_name name of the resource server's image
 * @param args the seL4_Word arguments to send
 * @param argc the number of arguments to send
 * @param server_pd_cap returns the PD resource of the started server
 * @param space_id returns the default resource space ID of the started server
 */
int start_resource_server_pd_args(gpi_cap_t rde_type,
                                  gpi_space_id_t rde_id,
                                  char *image_name,
                                  seL4_Word *args,
                                  uint32_t argc,
                                  seL4_CPtr *server_pd_cap,
                                  gpi_space_id_t *space_id);