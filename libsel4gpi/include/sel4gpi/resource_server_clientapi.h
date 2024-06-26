#include <stdint.h>

#include <sel4/sel4.h>
#include <sel4gpi/model_exporting.h>

/** @file
 * API for remote resource servers, which the RT or other PDs may use
 */

// IPC values returned in the "label" message header.
enum rs_errors
{
    RS_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    RS_ERROR_RR_SIZE = seL4_NumErrors, // RR request shared memory is too small
    RS_ERROR_DNE,                      // Resource does not exist
    RS_ERROR_NS,                       // Namespace does not exist
    RS_NUM_ERRORS
};

// IPC Message register values for RSMSGREG_FUNC
enum rs_funcs
{
    RS_FUNC_NEW_NS_REQ = 0,
    RS_FUNC_NEW_NS_ACK,

    RS_FUNC_END,
};

// Message registers for all remote resource server requests
enum rs_msgregs
{
    /* These are fixed headers in every message. */
    RSMSGREG_FUNC = 0,

    /* This is a convenience label for IPC MessageInfo length. */
    RSMSGREG_LABEL0,

    /* New NS */
    RSMSGREG_NEW_NS_REQ_END = RSMSGREG_LABEL0,
    RSMSGREG_NEW_NS_ACK_ID = RSMSGREG_LABEL0,
    RSMSGREG_NEW_NS_ACK_END,
};

/**
 * Starts a resource server in a new PD
 * @param rde_type cap type of RDE to add, optional
 * @param rde_id space ID of RDE to add, optional
 * @param image_name name of the resource server's image
 * @param server_pd_cap returns the PD resource of the started server
 * @param space_id returns the default resource space ID of the started server
 */
int start_resource_server_pd(gpi_cap_t rde_type,
                             uint64_t rde_id,
                             char *image_name,
                             seL4_CPtr *server_pd_cap,
                             uint64_t *space_id);

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
                                  uint64_t rde_id,
                                  char *image_name,
                                  seL4_Word *args,
                                  uint argc,
                                  seL4_CPtr *server_pd_cap,
                                  uint64_t *space_id);

/**
 * Request a new namespace ID from a resource server
 *
 * @param server_ep the EP of the resource server
 * @param ns_id returns the newly allocated NS ID
 * @return RS_NOERROR on success, error otherwise
 */
int resource_server_client_new_ns(seL4_CPtr server_ep,
                                  uint64_t *ns_id);