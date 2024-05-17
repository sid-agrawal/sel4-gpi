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
    RS_ERROR_DNE,                      // RR request resource no longer exists
    RS_ERROR_NS,                       // Namespace does not exist
    RS_NUM_ERRORS
};

// IPC Message register values for RSMSGREG_FUNC
enum rs_funcs
{
    RS_FUNC_GET_RR_REQ = 0,
    RS_FUNC_GET_RR_ACK,

    RS_FUNC_NEW_NS_REQ,
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

    /* Extract RR */
    RSMSGREG_EXTRACT_RR_REQ_SIZE = RSMSGREG_LABEL0,
    RSMSGREG_EXTRACT_RR_REQ_VADDR,
    RSMSGREG_EXTRACT_RR_REQ_SPACE,
    RSMSGREG_EXTRACT_RR_REQ_ID,
    RSMSGREG_EXTRACT_RR_REQ_PD_ID,
    RSMSGREG_EXTRACT_RR_REQ_RS_PD_ID,
    RSMSGREG_EXTRACT_RR_REQ_END,

    RSMSGREG_EXTRACT_RR_ACK_END = RSMSGREG_LABEL0,

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
 * Request a resource server to dump resource relations
 *
 * @param server_ep Unbadged ep of the resource server
 * @param space_id The space ID of the resource to dump relations for
 * @param res_id The ID of the resource to dump relations for
 * @param pd_id The id of the pd that has the resource (for the has_access_to row)
 * @param server_pd_id The id of the server pd
 * @param remote_vaddr location of shared memory in the resource server
 * @param local_vaddr location of shared memory in the caller
 * @param size size of shared memory
 * @param model_state_t Location of the resulting model state
 *                     (same as local_vaddr on success)
 * @return
 *      RS_NOERROR if dump completed successfully
 *      RS_ERROR_RR_SIZE if size was too small to write RR
 *      + Error codes for the respective resource server
 */
int resource_server_client_get_rr(seL4_CPtr server_ep,
seL4_Word space_id,
                           seL4_Word res_id,
                           seL4_Word pd_id,
                           seL4_Word server_pd_id,
                           void *remote_vaddr,
                           void *local_vaddr,
                           size_t size,
                           model_state_t **ret_state);

/**
 * Request a new namespace ID from a resource server
 *
 * @param server_ep the EP of the resource server
 * @param ns_id returns the newly allocated NS ID
 */
int resource_server_client_new_ns(seL4_CPtr server_ep,
                                  uint64_t *ns_id);